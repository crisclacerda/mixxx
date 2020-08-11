#include "analyzer/analyzerrhythm.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <QtDebug>
#include <unordered_set>

#include "analyzer/constants.h"
#include "track/beatfactory.h"
#include "track/beatmap.h"
#include "track/track.h"
#include "engine/engine.h" // Included to get mixxx::kEngineChannelCount
#include "analyzer/analyzerrhythmstats.h"

namespace {

// This determines the resolution of the resulting BeatMap.
// ~12 ms (86 Hz) is a fair compromise between accuracy and analysis speed,
// also matching the preferred window/step sizes from BeatTrack VAMP.
constexpr float kStepSecs = 0.0113378684807;
// results in 43 Hz @ 44.1 kHz / 47 Hz @ 48 kHz / 47 Hz @ 96 kHz
constexpr int kMaximumBinSizeHz = 50; // Hz
// This is a quick hack to make a beatmap with only downbeats - will affect the bpm
constexpr bool useDownbeatOnly = false;
// The range of bpbs considered for detection, lower is included, higher excluded [)
constexpr int kLowerBeatsPerBar = 4;
constexpr int kHigherBeatsPerBar = 5;
// The number of types of detection functions
constexpr int kDfTypes = 5;
// tempogram resolution constants
constexpr float kNoveltyCurveMinDB = -54.0;
constexpr float kNoveltyCurveCompressionConstant = 400.0;
constexpr int kTempogramLog2WindowLength = 12;
constexpr int kTempogramLog2HopSize = 8;
constexpr int kTempogramLog2FftLength = 12;
constexpr float kNoveltyCurveHop = 512.0;
constexpr float kNoveltyCurveWindow = 1024.0;

DFConfig makeDetectionFunctionConfig(int stepSize, int windowSize) {
    // These are the defaults for the VAMP beat tracker plugin
    DFConfig config;
    config.DFType = dfAll - dfBroadBand;
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
          m_iMaxBpm(9999),
          m_noveltyCurveMinV(pow(10,kNoveltyCurveMinDB/20.0)) {
}

inline int AnalyzerRhythm::stepSize() {
    return m_iSampleRate * kStepSecs;
}

inline int AnalyzerRhythm::windowSize() {
    return MathUtilities::nextPowerOfTwo(m_iSampleRate / kMaximumBinSizeHz);
}

bool AnalyzerRhythm::initialize(TrackPointer pTrack, int sampleRate, int totalSamples) {
    if (totalSamples == 0 or !shouldAnalyze(pTrack)) {
        return false;
    }

    m_iSampleRate = sampleRate;
    m_iTotalSamples = totalSamples;
    m_iMaxSamplesToProcess = m_iTotalSamples;
    m_iCurrentSample = 0;

    // decimation factor aims at resampling to c. 3KHz; must be power of 2
    int factor = MathUtilities::nextPowerOfTwo(m_iSampleRate / 3000);
    m_downbeat = std::make_unique<DownBeat>(
            m_iSampleRate, factor, stepSize());
    
    m_fft = std::make_unique<FFTReal>(windowSize());
    m_fftRealOut = new double[windowSize()];
    m_fftImagOut = new double[windowSize()];

    m_window = new Window<double>(HammingWindow, windowSize());
    m_pDetectionFunction = std::make_unique<DetectionFunction>(
            makeDetectionFunctionConfig(stepSize(), windowSize()));
    
    qDebug() << "input sample rate is " << m_iSampleRate << ", step size is " << stepSize();

    m_onsetsProcessor.initialize(
            windowSize(), stepSize(), [this](double* pWindow, size_t) {
                DFresults onsets;
                m_window->cut(pWindow);
                m_fft->forward(pWindow, m_fftRealOut, m_fftImagOut);
                onsets = m_pDetectionFunction->processFrequencyDomain(m_fftRealOut, m_fftImagOut);
                m_detectionResults.push_back(onsets);
                return true;
            });

    m_downbeatsProcessor.initialize(
            windowSize(), stepSize(), [this](double* pWindow, size_t) {
                m_downbeat->pushAudioBlock(reinterpret_cast<float*>(pWindow));
                return true;
            });

    m_noveltyCurveProcessor.initialize(
        kNoveltyCurveWindow, kNoveltyCurveHop, [this](double* pWindow, size_t) {
            int n = kNoveltyCurveWindow;
            double *in = pWindow;
            //calculate magnitude of FrequencyDomain input
            std::vector<float> fftCoefficients;
            for (int i = 0; i < n; i++){
                float magnitude = in[i];
                magnitude = magnitude > m_noveltyCurveMinV ? magnitude : m_noveltyCurveMinV;
                fftCoefficients.push_back(magnitude);
            }
            m_spectrogram.push_back(fftCoefficients);
            return true;
        });
    
    return true;
}

void AnalyzerRhythm::setTempogramParameters() {
    m_tempogramWindowLength = pow(2,kTempogramLog2WindowLength);
    m_tempogramHopSize = pow(2,kTempogramLog2HopSize);
    m_tempogramFftLength = pow(2,kTempogramLog2FftLength);

    m_tempogramMinBPM = 60;
    m_tempogramMaxBPM = 180;
    m_tempogramInputSampleRate = m_iSampleRate / kNoveltyCurveHop;
}


bool AnalyzerRhythm::shouldAnalyze(TrackPointer pTrack) const {
    bool bpmLock = pTrack->isBpmLocked();
    if (bpmLock) {
        qDebug() << "Track is BpmLocked: Beat calculation will not start";
        return true;
    }
    mixxx::BeatsPointer pBeats = pTrack->getBeats();
    if (!pBeats) {
        return true;
    }
    else if (!mixxx::Bpm::isValidValue(pBeats->getBpm())) {
        qDebug() << "Re-analyzing track with invalid BPM despite preference settings.";
        return true;
    } else {
        qDebug() << "Track already has beats, and won't re-analyze";
        return false;
    }
    return true;
}

bool AnalyzerRhythm::processSamples(const CSAMPLE *pIn, const int iLen) {

    m_iCurrentSample += iLen;
    if (m_iCurrentSample > m_iMaxSamplesToProcess) {
        return true; // silently ignore all remaining samples
    }
    bool onsetReturn = m_onsetsProcessor.processStereoSamples(pIn, iLen);
    bool downbeatsReturn = m_downbeatsProcessor.processStereoSamples(pIn, iLen);
    bool noveltyCurvReturn = m_noveltyCurveProcessor.processStereoSamples(pIn, iLen);
    return onsetReturn & downbeatsReturn & noveltyCurvReturn;
}

void AnalyzerRhythm::cleanup() {
    m_resultBeats.clear();
    m_detectionResults.clear();
    m_pDetectionFunction.reset();
    m_noveltyCurve.clear();
    delete m_window;
    delete [] m_fftImagOut;
    delete [] m_fftRealOut;
}

std::vector<double> AnalyzerRhythm::computeBeats() {
    std::vector<std::vector<double>> allBeats(kDfTypes);
    for (int dfType = 0; dfType < 1; dfType += 1) {
        int nonZeroCount = m_noveltyCurve.size();
        while (nonZeroCount > 0 && m_noveltyCurve[nonZeroCount - 1] <= 0.0) {
            --nonZeroCount;
        }

        std::vector<double> noteOnsets;
        std::vector<double> beatPeriod;
        std::vector<double> tempi;
        const auto required_size = std::max(0, nonZeroCount - 2);
        noteOnsets.reserve(required_size);
        beatPeriod.reserve(required_size);

        // skip first 2 results as it might have detect noise as onset
        // that's how vamp does and seems works best this way
        for (int i = 0; i < nonZeroCount; ++i) {
            noteOnsets.push_back(m_noveltyCurve[i]);
            beatPeriod.push_back(0.0);
            
        }
        
        TempoTrackV2 tt(m_iSampleRate, stepSize());
        tt.calculateBeatPeriod(noteOnsets, beatPeriod, tempi);
        //qDebug() << beatPeriod.size() << tempi.size();
        //qDebug() << tempi;

        tt.calculateBeats(noteOnsets, beatPeriod, allBeats[dfType]);
        //qDebug() << allBeats[dfType].size();
    }
    // Let's compare all beats positions and use the "best" one
    /*
    double maxAgreement = 0.0;
    int maxAgreementIndex = 0;
    for (int thisOne = 0; thisOne < kDfTypes; thisOne += 1) {
        double agreementPercentage;
        int agreement = 0;
        int maxPossibleAgreement = 1;
        std::unordered_set<double> thisOneAsSet(allBeats[thisOne].begin(), allBeats[thisOne].end());
        for (int theOther = 0; theOther < kDfTypes; theOther += 1) {
            if (thisOne == theOther) {
                continue;
            }
            for (size_t beat = 0; beat < allBeats[theOther].size(); beat += 1) {
                if(thisOneAsSet.find(allBeats[theOther][beat]) != thisOneAsSet.end()) {
                    agreement += 1;
                }
                maxPossibleAgreement += 1;
            }
        }
        agreementPercentage = agreement / static_cast<double>(maxPossibleAgreement);
        qDebug() << thisOne << "agreementPercentage is" << agreementPercentage;
        if (agreementPercentage > maxAgreement) {
            maxAgreement = agreementPercentage;
            maxAgreementIndex = thisOne;
        }
    }
    */
    return allBeats[0];
}

std::vector<double> AnalyzerRhythm::computeBeatsSpectralDifference(std::vector<double> &beats) {
    size_t downLength = 0;
    const float *downsampled = m_downbeat->getBufferedAudio(downLength);

    std::vector<int> downbeats;
    m_downbeat->findDownBeats(downsampled, downLength, beats, m_downbeats);
    std::vector<double> beatsSpecDiff;
    m_downbeat->getBeatSD(beatsSpecDiff);
    return beatsSpecDiff;
}

// This naive approach for bpb did't work
// but that's how QM Lib compute the downbeat
// leaving the outer for for now as it might be useful later
std::tuple<int, int> AnalyzerRhythm::computeMeter(std::vector<double> &beatsSD) {
    int candidateDownbeatPosition = 0;
    int candidateBeatsPerBar = 0;
    std::vector<std::vector<double>> specDiffSeries(kHigherBeatsPerBar - kLowerBeatsPerBar);
    // let's considers all bpb candidates
    for (candidateBeatsPerBar = kLowerBeatsPerBar;
            candidateBeatsPerBar < kHigherBeatsPerBar;
            candidateBeatsPerBar += 1) {
        specDiffSeries[candidateBeatsPerBar - kLowerBeatsPerBar]
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
                    specDiffSeries[candidateBeatsPerBar - kLowerBeatsPerBar]
                            [candidateDownbeatPosition] += beatsSD[barBegin];
                    count += 1;
                }
            }
            specDiffSeries[candidateBeatsPerBar - kLowerBeatsPerBar]
                    [candidateDownbeatPosition] /= count;
        }
    }
    // here we find the series with the largest mean spec diff
    int bestBpb = 0, bestDownbeatPos = 0;
    double value = 0;
    for (int i = 0; i < static_cast<int>(specDiffSeries.size()); i += 1) {
        for (int j = 0; j < static_cast<int>(specDiffSeries[i].size()); j += 1) {
            if (specDiffSeries[i][j] > value) {
                value = specDiffSeries[i][j];
                bestBpb = i;
                bestDownbeatPos = j;
            }
        }
    }
    return std::make_tuple(bestBpb + kLowerBeatsPerBar, bestDownbeatPos);
}



int AnalyzerRhythm::computeNoveltyCurve() {
    NoveltyCurveProcessor nc(static_cast<float>(m_iSampleRate), kNoveltyCurveWindow, kNoveltyCurveCompressionConstant);
    m_noveltyCurve = nc.spectrogramToNoveltyCurve(m_spectrogram);
    return m_spectrogram.size();
}

void AnalyzerRhythm::computeTempogramByDFT() {
    auto hannWindow = std::vector<float>(m_tempogramWindowLength, 0.0);
    WindowFunction::hanning(&hannWindow[0], m_tempogramWindowLength);
    SpectrogramProcessor spectrogramProcessor(m_tempogramWindowLength,
            m_tempogramFftLength, m_tempogramHopSize);
    Spectrogram tempogramDFT = spectrogramProcessor.process(
            &m_noveltyCurve[0], m_noveltyCurve.size(), &hannWindow[0]);
    // convert y axis to bpm
    int tempogramMinBin = (std::max(static_cast<int>(floor(((m_tempogramMinBPM/60.0)
            /m_tempogramInputSampleRate)*m_tempogramFftLength)), 0));
    int tempogramMaxBin = (std::min(static_cast<int>(ceil(((m_tempogramMaxBPM/60.0)
            /m_tempogramInputSampleRate)*m_tempogramFftLength)), m_tempogramFftLength/2));
    int binCount = tempogramMaxBin - tempogramMinBin + 1;
    float highest;
    float bestBpm;
    int bin;
    for (int block = 0; block < tempogramDFT.size(); block++) {
        // dft
        //qDebug() << "block" << block;
        //qDebug() << "DFT tempogram";
        highest = .0;
        bestBpm = .0;
        bin = 0;
        for (int k = tempogramMinBin; k <= tempogramMaxBin; k++){
            float w = (k/static_cast<float>(m_tempogramFftLength))*(m_tempogramInputSampleRate);
            float bpm = w*60;
            //qDebug() << "bin, bpm and value"<< bin++ << bpm << tempogramDFT[block][k];
            if (tempogramDFT[block][k] > highest) {
                highest = tempogramDFT[block][k];
                bestBpm = bpm;
            }
        }
        qDebug() << "best bpm at block" << bestBpm << block;
    }
}

void AnalyzerRhythm::computeTempogramByACF() {
    // Compute acf tempogram
    AutocorrelationProcessor autocorrelationProcessor(m_tempogramWindowLength, m_tempogramHopSize);
    Spectrogram tempogramACF = autocorrelationProcessor.process(&m_noveltyCurve[0], m_noveltyCurve.size());
    // Convert y axis to bpm
    int tempogramMinLag = std::max(static_cast<int>(ceil((60/ (kNoveltyCurveHop * m_tempogramMaxBPM))
                *m_iSampleRate)), 0);
    int tempogramMaxLag = std::min(static_cast<int>(floor((60/ (kNoveltyCurveHop * m_tempogramMinBPM))
                *m_iSampleRate)), m_tempogramWindowLength-1);
    qDebug() << tempogramMinLag << tempogramMaxLag;
    float highest;
    float bestBpm;
    int bin;
    for (int block = 0; block < tempogramACF.size(); block++) {
        //qDebug() << "block" << block;
        //qDebug() << "ACF tempogram";
        highest = .0;
        bestBpm = .0;
        bin = 0;
        for (int lag = tempogramMaxLag; lag >= tempogramMinLag; lag--) {
            float bpm = 60/(kNoveltyCurveHop * (lag/static_cast<float>(m_iSampleRate)));
            //qDebug() << "bin, bpm and value"<< bin++ << bpm << tempogramACF[block][lag];
            if (tempogramACF[block][lag] > highest) {
                highest = tempogramACF[block][lag];
                bestBpm = bpm;
            }
        }
        qDebug() << "best bpm at block" << bestBpm << block;
    }
}


void AnalyzerRhythm::storeResults(TrackPointer pTrack) {
    m_onsetsProcessor.finalize();
    m_downbeatsProcessor.finalize();
    m_noveltyCurveProcessor.finalize();

    setTempogramParameters();
    computeNoveltyCurve();    
    //for (auto nc : m_noveltyCurve) {qDebug() << nc;}
    computeTempogramByACF();
    computeTempogramByDFT();
    auto beats = computeBeats();
    auto beatsSpecDiff = computeBeatsSpectralDifference(beats);
    auto [bpb, firstDownbeat] = computeMeter(beatsSpecDiff);
    
    // convert beats positions from df increments to frams
    for (size_t i = 0; i < beats.size(); ++i) {
        double result = (beats.at(i) * kNoveltyCurveHop) - (kNoveltyCurveHop / mixxx::kEngineChannelCount);
        m_resultBeats.push_back(result);
    }
    // TODO(Cristiano&Harshit) THIS IS WHERE A BEAT VECTOR IS CREATED
    auto pBeatMap = new mixxx::BeatMap(*pTrack, m_iSampleRate, m_resultBeats);
    auto pBeats = mixxx::BeatsPointer(pBeatMap, &BeatFactory::deleteBeats);
    pTrack->setBeats(pBeats);
}
