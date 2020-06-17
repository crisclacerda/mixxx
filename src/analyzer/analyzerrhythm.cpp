#include "analyzer/analyzerrhythm.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <QtDebug>

#include "analyzer/constants.h"
#include "track/beatfactory.h"
#include "track/beatmap.h"
#include "track/beatutils.h"
#include "track/track.h"

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


AnalyzerRhythm::AnalyzerRhythm(UserSettingsPointer pConfig)
        : m_iSampleRate(0),
          m_iTotalSamples(0),
          m_iMaxSamplesToProcess(0),
          m_iCurrentSample(0),
          m_iMinBpm(0),
          m_iMaxBpm(9999) {
}

bool AnalyzerRhythm::initialize(TrackPointer pTrack, int sampleRate, int totalSamples) {
    if (totalSamples == 0) {
        return false;
    }

    bool bpmLock = pTrack->isBpmLocked();
    if (bpmLock) {
        qDebug() << "Track is BpmLocked: Beat calculation will not start";
        return false;
    }

    m_iSampleRate = sampleRate;
    m_iTotalSamples = totalSamples;
    m_iMaxSamplesToProcess = m_iTotalSamples;
    m_iCurrentSample = 0;

    m_detectionResults.clear();
    m_stepSize = m_iSampleRate * kStepSecs;
    m_windowSize = MathUtilities::nextPowerOfTwo(m_iSampleRate / kMaximumBinSizeHz);
    m_pDetectionFunction = std::make_unique<DetectionFunction>(
            makeDetectionFunctionConfig(m_stepSize, m_windowSize));
    // decimation factor aims at resampling to c. 3KHz; must be power of 2
    int factor = MathUtilities::nextPowerOfTwo(m_iSampleRate / 3000);
    m_downbeat = std::make_unique<DownBeat>(
            m_iSampleRate, factor, m_stepSize);

    qDebug() << "input sample rate is " << m_iSampleRate << ", step size is " << m_stepSize;

    m_processor.initialize(
            m_windowSize, m_stepSize, [this](double* pWindow, size_t) {
                m_detectionResults.push_back(
                        m_pDetectionFunction->processTimeDomain(pWindow));
                m_downbeat->pushAudioBlock(reinterpret_cast<float*>(pWindow));
                return true;
            });
    return true;
}

bool AnalyzerRhythm::shouldAnalyze(TrackPointer pTrack) const {

    bool bpmLock = pTrack->isBpmLocked();
    if (bpmLock) {
        qDebug() << "Track is BpmLocked: Beat calculation will not start";
        return false;
    }
    return true;
}

bool AnalyzerRhythm::processSamples(const CSAMPLE *pIn, const int iLen) {

    m_iCurrentSample += iLen;
    if (m_iCurrentSample > m_iMaxSamplesToProcess) {
        return true; // silently ignore all remaining samples
    }
    return m_processor.processStereoSamples(pIn, iLen);;
}

void AnalyzerRhythm::cleanup() {
    //cleanup
    ;
}

std::vector<double> AnalyzerRhythm::computeBeats() {

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
    return beats;
}

std::vector<double> AnalyzerRhythm::computeBeatsSpectralDifference(std::vector<double> &beats) {
    size_t downLength = 0;
    const float *downsampled = m_downbeat->getBufferedAudio(downLength);
    return m_downbeat->beatsSD(downsampled, downLength, beats);
}

// This naive approach for bpb did't work
// but that's how QM Lib compute the downbeat
// leaving the outer for for now as it might be useful later
std::tuple<int, int> AnalyzerRhythm::computeMeter(std::vector<double> &beatsSD) {
    // lower is included, higher excluded [)
    int lowerBeatsPerBar = 4, higherBeatsPerBar = 5;
    int candidateDownbeatPosition = 0, candidateBeatsPerBar = 0;
    std::vector<std::vector<double>> specDiffSeries(higherBeatsPerBar - lowerBeatsPerBar);
    // let's considers all bpb candidates
    for (candidateBeatsPerBar = lowerBeatsPerBar;
            candidateBeatsPerBar < higherBeatsPerBar;
            candidateBeatsPerBar += 1) {
        specDiffSeries[candidateBeatsPerBar - lowerBeatsPerBar]
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
                    specDiffSeries[candidateBeatsPerBar - lowerBeatsPerBar]
                            [candidateDownbeatPosition] += beatsSD[barBegin];
                    count += 1;
                }
            }
            specDiffSeries[candidateBeatsPerBar - lowerBeatsPerBar]
                    [candidateDownbeatPosition] /= count;
        }
    }
    // here we find the series with largest spec diff
    int bestBpb = 0, bestDownbeatPos = 0;
    double value = 0;
    for (int i = 0; i < static_cast<int>(specDiffSeries.size()); i += 1) {
        for (int j = 0; j < static_cast<int>(specDiffSeries[i].size()); j += 1) {
            if (specDiffSeries[i][j] > value) {
                value = specDiffSeries[i][j];
                bestBpb = i;
                bestDownbeatPos = j;
            }
            //qDebug() << specDiffSeries[i + lowerBeatsPerBar][j];
        }
    }
    return std::make_tuple(bestBpb + lowerBeatsPerBar, bestDownbeatPos);
}

void AnalyzerRhythm::storeResults(TrackPointer pTrack) {
    m_processor.finalize();
    auto beats = this->computeBeats();
    auto beatsSD = this->computeBeatsSpectralDifference(beats);
    auto [bpb, firstDownbeat] = this->computeMeter(beatsSD);
    //qDebug() << beatsSD;
    //qDebug() << bpb;
    //qDebug() << firstDownbeat;

    constexpr bool useDownbeatOnly = true;
    size_t downbeatPositions = 0;
    // convert beats positions from df increments to frams
    for (size_t i = 0; i < beats.size(); ++i) {
        double result = (beats.at(i) * m_stepSize) - m_stepSize / 2;
        if (useDownbeatOnly) {
            if ((i + firstDownbeat) % bpb == 0) {
                m_resultBeats.push_back(result);
                downbeatPositions++;
            }
        } else {
            m_resultBeats.push_back(result);
        }
    }
    //auto [m_resultBeats, tempos] = BeatUtils::FixBeatmap(m_resultBeats) #2847
    // TODO(Cristiano&Harshit) THIS IS WHERE A BEAT VECTOR IS CREATED
    auto pBeatMap = new mixxx::BeatMap(*pTrack, m_iSampleRate, m_resultBeats);
    auto pBeats = mixxx::BeatsPointer(pBeatMap, &BeatFactory::deleteBeats);
    pTrack->setBeats(pBeats);
}
