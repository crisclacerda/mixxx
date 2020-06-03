#include "waveform/renderers/waveformrenderbeat.h"

#include <QDomNode>
#include <QPaintEvent>
#include <QPainter>

#include "control/controlobject.h"
#include "track/beats.h"
#include "track/track.h"
#include "util/painterscope.h"
#include "waveform/renderers/waveformbeat.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"
#include "widget/wwidget.h"

WaveformRenderBeat::WaveformRenderBeat(WaveformWidgetRenderer* waveformWidgetRenderer)
        : WaveformRendererAbstract(waveformWidgetRenderer) {
    m_beats.resize(128);
}

WaveformRenderBeat::~WaveformRenderBeat() {
}

void WaveformRenderBeat::setup(const QDomNode& node, const SkinContext& context) {
    m_beatColor.setNamedColor(context.selectString(node, "BeatColor"));
    m_beatColor = WSkinColor::getCorrectColor(m_beatColor).toRgb();
}

void WaveformRenderBeat::draw(QPainter* painter, QPaintEvent* /*event*/) {
    TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();

    if (!trackInfo)
        return;

    mixxx::BeatsPointer trackBeats = trackInfo->getBeats();

    if (!trackBeats)
        return;

    int alpha = m_waveformRenderer->beatGridAlpha();
    if (alpha == 0)
        return;
    m_beatColor.setAlphaF(alpha/100.0);

    const int trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0) {
        return;
    }

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition();
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition();

    // qDebug() << "trackSamples" << trackSamples
    //          << "firstDisplayedPosition" << firstDisplayedPosition
    //          << "lastDisplayedPosition" << lastDisplayedPosition;

    std::unique_ptr<mixxx::BeatIterator> it(trackBeats->findBeats(
            firstDisplayedPosition * trackSamples,
            lastDisplayedPosition * trackSamples));

    // if no beat do not waste time saving/restoring painter
    if (!it || !it->hasNext()) {
        return;
    }

    PainterScope PainterScope(painter);

    painter->setRenderHint(QPainter::Antialiasing);

    QPen beatPen(m_beatColor);
    beatPen.setWidthF(std::max(1.0, scaleFactor()));
    painter->setPen(beatPen);

    const Qt::Orientation orientation = m_waveformRenderer->getOrientation();
    const float rendererWidth = m_waveformRenderer->getWidth();
    const float rendererHeight = m_waveformRenderer->getHeight();

    int beatCount = 0;
    QMap<BeatPointer, float> beatPositions;

    while (it->hasNext()) {
        auto beat = it->next();
        // TODO: Wrap into a utility function/class to convert frame to sample
        double beatPosition = beat->frame_position() * 2;
        double xBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(beatPosition);

        xBeatPoint = qRound(xBeatPoint);

        // If we don't have enough space, double the size.
        if (beatCount >= m_beats.size()) {
            m_beats.resize(m_beats.size() * 2);
        }

        m_beats[beatCount].setPosition(xBeatPoint);
        m_beats[beatCount].setType(beat->type());

        if (orientation == Qt::Horizontal) {
            m_beats[beatCount].setLength(rendererHeight);
        } else {
            m_beats[beatCount].setLength(rendererWidth);
            m_beats[beatCount].setOrientation(Qt::Vertical);
        }
        beatCount++;
        beatPositions[beat] = xBeatPoint;
    }

    // Make sure to use constData to prevent detaches!
    for (int i = 0; i < beatCount; i++) {
        const auto currentBeat = m_beats.constData() + i;
        currentBeat->draw(painter);
    }
    m_waveformRenderer->setBeatPositions(beatPositions);
}
