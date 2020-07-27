#include "waveform/renderers/waveformrenderbeat.h"

#include <QDomNode>
#include <QPaintEvent>
#include <QPainter>

#include "track/beats.h"
#include "track/track.h"
#include "util/frameadapter.h"
#include "util/painterscope.h"
#include "waveform/renderers/waveformbeat.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

namespace {
constexpr int kMaxZoomFactorToDisplayBeats = 15;
}

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
    //         << "firstDisplayedPosition" << firstDisplayedPosition
    //         << "lastDisplayedPosition" << lastDisplayedPosition;

    std::unique_ptr<mixxx::Beats::iterator> it(trackBeats->findBeats(
            mixxx::FramePos(firstDisplayedPosition * trackSamples / 2.0),
            mixxx::FramePos(lastDisplayedPosition * trackSamples / 2.0)));

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
    QList<WaveformBeat> beatsOnScreen;

    while (it->hasNext()) {
        auto beat = it->next();
        double beatSamplePosition = framePosToSamplePos(beat.getFramePosition());
        int beatPixelPositionInWidgetSpace = qRound(
                m_waveformRenderer->transformSamplePositionInRendererWorld(
                        beatSamplePosition));

        // If we don't have enough space, double the size.
        if (beatCount >= m_beats.size()) {
            m_beats.resize(m_beats.size() * 2);
        }

        auto* waveformBeat = &m_beats[beatCount];
        waveformBeat->setPositionPixels(beatPixelPositionInWidgetSpace);
        waveformBeat->setBeatGridMode(m_waveformRenderer->beatGridMode());
        waveformBeat->setBeat(beat);
        waveformBeat->setOrientation(orientation);
        waveformBeat->setLength((orientation == Qt::Horizontal) ? rendererHeight : rendererWidth);
        waveformBeat->setVisible(beat.getType() == mixxx::Beat::DOWNBEAT ||
                (beat.getType() == mixxx::Beat::BEAT &&
                        m_waveformRenderer->getZoomFactor() <
                                kMaxZoomFactorToDisplayBeats));
        beatsOnScreen.append(*waveformBeat);
        beatCount++;
    }

    // Make sure to use constData to prevent detaches!
    for (int i = 0; i < beatCount; i++) {
        const auto currentBeat = m_beats.constData() + i;
        currentBeat->draw(painter);
    }
    m_waveformRenderer->setBeatsOnScreen(beatsOnScreen);
}
