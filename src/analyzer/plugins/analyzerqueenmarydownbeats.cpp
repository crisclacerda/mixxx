#include <dsp/onsets/DetectionFunction.h>
#include <dsp/tempotracking/TempoTrackV2.h>
#include <dsp/tempotracking/DownBeat.h>

// Class header comes after library includes here since our preprocessor
// definitions interfere with qm-dsp's headers.
#include "analyzer/plugins/analyzerqueenmarydownbeats.h"

#include "analyzer/constants.h"

namespace mixxx {
namespace {

// This determines the resolution of the resulting BeatMap.
// ~12 ms (86 Hz) is a fair compromise between accuracy and analysis speed,
// also matching the preferred window/step sizes from BeatTrack VAMP.
// For a 44.1 kHz track, we go in 512 sample steps
// TODO: kStepSecs and the waveform sample rate of 441
// (defined in AnalyzerWaveform::initialize) do not align well and thus
// generate interference. Currently we are at this odd factor: 441 * 0.01161 = 5.12.
// This should be adjusted to be an integer.
constexpr float kStepSecs = 0.01161;
// results in 43 Hz @ 44.1 kHz / 47 Hz @ 48 kHz / 47 Hz @ 96 kHz
constexpr int kMaximumBinSizeHz = 50; // Hz

DFConfig makeDetectionFunctionConfig(int stepSize, int windowSize) {
    // These are the defaults for the VAMP beat tracker plugin we used in Mixxx
    // 2.0.
    DFConfig config;
    config.DFType = DF_COMPLEXSD;
    config.stepSize = stepSize;
    config.frameLength = windowSize;
    config.dbRise = 3;
    config.adaptiveWhitening = 0;
    config.whiteningRelaxCoeff = -1;
    config.whiteningFloor = -1;
    return config;
}

} // namespace

AnalyzerQueenMaryDownbeats::AnalyzerQueenMaryDownbeats()
        : m_iSampleRate(0),
            m_windowSize(0),	
            m_stepSize(0) {
}

AnalyzerQueenMaryDownbeats::~AnalyzerQueenMaryDownbeats() {
}

bool AnalyzerQueenMaryDownbeats::initialize(int samplerate) {
    m_detectionResults.clear();
    m_iSampleRate = samplerate;
    m_stepSize = m_iSampleRate * kStepSecs;
    m_windowSize = MathUtilities::nextPowerOfTwo(m_iSampleRate / kMaximumBinSizeHz);
    m_pDetectionFunction = std::make_unique<DetectionFunction>(
            makeDetectionFunctionConfig(m_stepSize, m_windowSize));
    // decimation factor aims at resampling to c. 3KHz; must be power of 2
    int factor = MathUtilities::nextPowerOfTwo(m_iSampleRate / 3000);
    m_downbeat = std::make_unique<DownBeat>(
            m_iSampleRate, factor, m_stepSize);

    qDebug() << "input sample rate is " << m_iSampleRate << ", step size is " << m_stepSize;

    m_helper.initialize(
            m_windowSize, m_stepSize, [this](double* pWindow, size_t) {
                // TODO(rryan) reserve?
                m_detectionResults.push_back(
                        m_pDetectionFunction->processTimeDomain(pWindow));
                m_downbeat->pushAudioBlock(reinterpret_cast<float*>(pWindow));
                return true;
            });
    
    return true;
}

bool AnalyzerQueenMaryDownbeats::processSamples(const CSAMPLE* pIn, const int iLen) {
    DEBUG_ASSERT(iLen % kAnalysisChannels == 0);
    if (!m_pDetectionFunction) {
        return false;
    }

    return m_helper.processStereoSamples(pIn, iLen);
    
}

bool AnalyzerQueenMaryDownbeats::finalize() {
    m_helper.finalize();

    int nonZeroCount = m_detectionResults.size();
    while (nonZeroCount > 0 && m_detectionResults.at(nonZeroCount - 1) <= 0.0) {
        --nonZeroCount;
    }

    std::vector<double> df;
    std::vector<double> beatPeriod;
    std::vector<double> tempi;
    const auto required_size = std::max(0, nonZeroCount - 2);
    df.reserve(required_size);
    beatPeriod.reserve(required_size);

    // skip first 2 results as it might have detect noise as onset
    // that's how vamp does and seems works best this way
    for (int i = 2; i < nonZeroCount; ++i) {
        df.push_back(m_detectionResults.at(i));
        beatPeriod.push_back(0.0);
    }

    TempoTrackV2 tt(m_iSampleRate, m_stepSize);
    tt.calculateBeatPeriod(df, beatPeriod, tempi);

    std::vector<double> beats;
    tt.calculateBeats(df, beatPeriod, beats);

    std::vector<int> downbeats;
    size_t downLength = 0;
    const float *downsampled = m_downbeat->getBufferedAudio(downLength);
    m_downbeat->findDownBeats(downsampled, downLength, beats, downbeats);
    std::vector<double> beatsSD;
    m_downbeat->getBeatSD(beatsSD);
    qDebug() << beatsSD;
    // for now we assume that are 3, 4 or 5 beats in a bar
    constexpr int lowerBeatsPerBar = 3, higherBeatsPerBar = 6;
    int candidateDownbeatPosition = 0, candidateBeatsPerBar = 0;
    std::vector<std::vector<double>> beatsSpecDiffs(higherBeatsPerBar - lowerBeatsPerBar);
    // let's considers all bpb candidates
    for (candidateBeatsPerBar = lowerBeatsPerBar;
            candidateBeatsPerBar < higherBeatsPerBar;
            candidateBeatsPerBar += 1) {
        beatsSpecDiffs[candidateBeatsPerBar - lowerBeatsPerBar]
                = std::vector<double>(candidateBeatsPerBar, 0);
        // and all downbeats position candidates
        for (candidateDownbeatPosition = 0;
                candidateDownbeatPosition < candidateBeatsPerBar;
                candidateDownbeatPosition += 1) {
            int count = 0;
            // to compute the mean spec diff of all possible measures
            for (int barBegin = candidateDownbeatPosition - 1;
                    barBegin < static_cast<int>(beatsSD.size());
                    barBegin += candidateBeatsPerBar) {
                if (barBegin >= 0) {
                    beatsSpecDiffs[candidateBeatsPerBar - lowerBeatsPerBar]
                            [candidateDownbeatPosition] += beatsSD[barBegin];
                    count += 1;
                }
            }
            beatsSpecDiffs[candidateBeatsPerBar - lowerBeatsPerBar]
                    [candidateDownbeatPosition] /= count;
        }
    }
    int bestBpb = 0, bestDownbeatPos = 0;
    double value = 0;
    for (int i = 0; i < static_cast<int>(beatsSpecDiffs.size()); i += 1) {
        for (int j = 0; j < static_cast<int>(beatsSpecDiffs[i].size()); j += 1) {
            if (beatsSpecDiffs[i][j] > value) {
                value = beatsSpecDiffs[i][j];
                bestBpb = i;
                bestDownbeatPos = j;
            }
            qDebug() << beatsSpecDiffs[i][j];
        }
    }
    qDebug() << bestBpb + lowerBeatsPerBar << bestDownbeatPos + 1;
    int downbeatPositions = 0;
    m_resultBeats.reserve(downbeats.size());
    for (int i = 0; i < static_cast<int>(beats.size()); ++i) {
        if (i == downbeats[downbeatPositions] and downbeatPositions < static_cast<int>(downbeats.size())) {
            double result = (beats.at(i) * m_stepSize) - m_stepSize / 2;
            m_resultBeats.push_back(result);
            downbeatPositions++;
        }
    }
    m_pDetectionFunction.reset();
    return true;
}

} // namespace mixxx
