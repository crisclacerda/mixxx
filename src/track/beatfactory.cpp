#include <QtDebug>
#include <QStringList>

#include "track/beatgrid.h"
#include "track/beatmap.h"
#include "track/beatfactory.h"
#include "track/beatutils.h"

mixxx::BeatsPointer BeatFactory::loadBeatsFromByteArray(const Track& track,
        QString beatsVersion,
        QString beatsSubVersion,
        const QByteArray& beatsSerialized) {
    if (beatsVersion == BEAT_GRID_1_VERSION ||
        beatsVersion == BEAT_GRID_2_VERSION) {
        mixxx::BeatGrid* pGrid = new mixxx::BeatGrid(track, 0, beatsSerialized);
        pGrid->setSubVersion(beatsSubVersion);
        qDebug() << "Successfully deserialized BeatGrid";
        return mixxx::BeatsPointer(pGrid, &BeatFactory::deleteBeats);
    } else if (beatsVersion == BEAT_MAP_VERSION) {
        mixxx::BeatMap* pMap = new mixxx::BeatMap(track, 0, beatsSerialized);
        pMap->setSubVersion(beatsSubVersion);
        qDebug() << "Successfully deserialized BeatMap";
        return mixxx::BeatsPointer(pMap, &BeatFactory::deleteBeats);
    }
    qDebug() << "BeatFactory::loadBeatsFromByteArray could not parse serialized beats.";
    return mixxx::BeatsPointer();
}

mixxx::BeatsPointer BeatFactory::makeBeatGrid(
        const Track& track, double dBpm, double dFirstBeatSample) {
    mixxx::BeatGrid* pGrid = new mixxx::BeatGrid(track, 0);
    pGrid->setGrid(dBpm, dFirstBeatSample);
    return mixxx::BeatsPointer(pGrid, &BeatFactory::deleteBeats);
}

// static
QString BeatFactory::getPreferredVersion(
        const bool bEnableFixedTempoCorrection) {
    if (bEnableFixedTempoCorrection) {
        return BEAT_GRID_2_VERSION;
    }
    return BEAT_MAP_VERSION;
}

QString BeatFactory::getPreferredSubVersion(
        const bool bEnableFixedTempoCorrection,
        const bool bEnableOffsetCorrection,
        const int iMinBpm,
        const int iMaxBpm,
        const QHash<QString, QString> extraVersionInfo) {
    const char* kSubVersionKeyValueSeparator = "=";
    const char* kSubVersionFragmentSeparator = "|";
    QStringList fragments;

    // min/max BPM limits only apply to fixed-tempo assumption
    if (bEnableFixedTempoCorrection) {
        fragments << QString("min_bpm%1%2")
                             .arg(kSubVersionKeyValueSeparator,
                                     QString::number(iMinBpm));
        fragments << QString("max_bpm%1%2")
                             .arg(kSubVersionKeyValueSeparator,
                                     QString::number(iMaxBpm));
    }

    QHashIterator<QString, QString> it(extraVersionInfo);
    while (it.hasNext()) {
        it.next();
        if (it.key().contains(kSubVersionKeyValueSeparator) ||
                it.key().contains(kSubVersionFragmentSeparator) ||
                it.value().contains(kSubVersionKeyValueSeparator) ||
                it.value().contains(kSubVersionFragmentSeparator)) {
            qDebug() << "ERROR: Your analyzer key/value contains invalid "
                        "characters:"
                     << it.key() << ":" << it.value() << "Skipping.";
            continue;
        }
        fragments << QString("%1%2%3").arg(
                it.key(), kSubVersionKeyValueSeparator, it.value());
    }
    if (bEnableFixedTempoCorrection && bEnableOffsetCorrection) {
        fragments << QString("offset_correction%1%2")
                             .arg(kSubVersionKeyValueSeparator,
                                     QString::number(1));
    }

    fragments << QString("rounding%1%2")
                         .arg(kSubVersionKeyValueSeparator,
                                 QString::number(0.05));

    std::sort(fragments.begin(), fragments.end());
    return (fragments.size() > 0) ? fragments.join(kSubVersionFragmentSeparator)
                                  : "";
}

mixxx::BeatsPointer BeatFactory::makePreferredBeats(const Track& track,
        QVector<double> beats,
        const QHash<QString, QString> extraVersionInfo,
        const bool bEnableFixedTempoCorrection,
        const bool bEnableOffsetCorrection,
        const int iSampleRate,
        const int iTotalSamples,
        const int iMinBpm,
        const int iMaxBpm) {
    const QString version = getPreferredVersion(bEnableFixedTempoCorrection);
    const QString subVersion = getPreferredSubVersion(bEnableFixedTempoCorrection,
                                                      bEnableOffsetCorrection,
                                                      iMinBpm, iMaxBpm,
                                                      extraVersionInfo);

    BeatUtils::printBeatStatistics(beats, iSampleRate);
    if (version == BEAT_GRID_2_VERSION) {
        double globalBpm = BeatUtils::calculateBpm(beats, iSampleRate, iMinBpm, iMaxBpm);
        double firstBeat = BeatUtils::calculateFixedTempoFirstBeat(
            bEnableOffsetCorrection,
            beats, iSampleRate, iTotalSamples, globalBpm);
        mixxx::BeatGrid* pGrid = new mixxx::BeatGrid(track, iSampleRate);
        // firstBeat is in frames here and setGrid() takes samples.
        pGrid->setGrid(globalBpm, firstBeat * 2);
        pGrid->setSubVersion(subVersion);
        return mixxx::BeatsPointer(pGrid, &BeatFactory::deleteBeats);
    } else if (version == BEAT_MAP_VERSION) {

        auto fixedBeats = BeatUtils::ironBeatmap(beats, iSampleRate, iMinBpm, iMaxBpm);
        for (auto beat : fixedBeats) {
            std::cerr << std::fixed << beat << std::endl;
        }
        auto pMap = new mixxx::BeatMap(track, iSampleRate, fixedBeats);
        pMap->setSubVersion(subVersion);
        return mixxx::BeatsPointer(pMap,&BeatFactory::deleteBeats);
        
    } else {
        qDebug() << "ERROR: Could not determine what type of beatgrid to create.";
        return mixxx::BeatsPointer();
    }
}

void BeatFactory::deleteBeats(mixxx::Beats* pBeats) {
    // BeatGrid/BeatMap objects have no parent and live in the same thread as
    // their associated TIO. QObject::deleteLater does not have the desired
    // effect when the QObject's thread does not have an event loop (i.e. when
    // the main thread has already shut down) so we delete the BeatMap/BeatGrid
    // directly when its reference count drops to zero.
    delete pBeats;
}
