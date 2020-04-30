#include "effects/backends/builtin/beatgrindeffect.h"

#include <QtDebug>

#include "util/sample.h"
#include "util/math.h"
#include "util/rampingvalue.h"


// static
QString BeatGrindEffect::getId() {
    return "org.mixxx.effects.beatgrind";
}

// static
EffectManifestPointer BeatGrindEffect::getManifest() {
    EffectManifestPointer pManifest(new EffectManifest());

    pManifest->setAddDryToWet(true);
    pManifest->setEffectRampsFromDry(true);

    pManifest->setId(getId());
    pManifest->setName(QObject::tr("BeatGrind"));
    pManifest->setShortName(QObject::tr("BeatGrind"));
    pManifest->setAuthor("The Mixxx Team");
    pManifest->setVersion("1.0");
    pManifest->setDescription(QObject::tr(
      "Stores the input signal in a temporary buffer and outputs it after a short time"));
    pManifest->setMetaknobDefault(db2ratio(-3.0));

    EffectManifestParameterPointer delay = pManifest->addParameter();
    delay->setId("delay_time");
    delay->setName(QObject::tr("Time"));
    delay->setShortName(QObject::tr("Time"));
    delay->setDescription(QObject::tr(
        "Delay time\n"
        "1/8 - 2 beats if tempo is detected\n"
        "1/8 - 2 seconds if no tempo is detected"));
    delay->setValueScaler(EffectManifestParameter::ValueScaler::LINEAR);
    delay->setSemanticHint(EffectManifestParameter::SemanticHint::UNKNOWN);
    delay->setUnitsHint(EffectManifestParameter::UnitsHint::BEATS);
    delay->setRange(0.0, 0.5, 2.0);

    EffectManifestParameterPointer quantize = pManifest->addParameter();
    quantize->setId("quantize");
    quantize->setName(QObject::tr("Quantize"));
    quantize->setShortName(QObject::tr("Quantize"));
    quantize->setDescription(QObject::tr(
        "Round the Time parameter to the nearest 1/4 beat."));
    quantize->setValueScaler(EffectManifestParameter::ValueScaler::TOGGLE);
    quantize->setSemanticHint(EffectManifestParameter::SemanticHint::UNKNOWN);
    quantize->setUnitsHint(EffectManifestParameter::UnitsHint::UNKNOWN);
    quantize->setRange(0, 1, 1);

    EffectManifestParameterPointer feedback = pManifest->addParameter();
    feedback->setId("feedback_amount");
    feedback->setName(QObject::tr("Feedback"));
    feedback->setShortName(QObject::tr("Feedback"));
    feedback->setDescription(QObject::tr(
        "Amount the echo fades each time it loops"));
    feedback->setValueScaler(EffectManifestParameter::ValueScaler::LINEAR);
    feedback->setSemanticHint(EffectManifestParameter::SemanticHint::UNKNOWN);
    feedback->setUnitsHint(EffectManifestParameter::UnitsHint::UNKNOWN);
    feedback->setRange(0.00, db2ratio(-3.0), 1.00);

    EffectManifestParameterPointer send = pManifest->addParameter();
    send->setId("send_amount");
    send->setName(QObject::tr("Send"));
    send->setShortName(QObject::tr("Send"));
    send->setDescription(QObject::tr(
        "How much of the signal to send into the delay buffer"));
    send->setValueScaler(EffectManifestParameter::ValueScaler::LINEAR);
    send->setSemanticHint(EffectManifestParameter::SemanticHint::UNKNOWN);
    send->setUnitsHint(EffectManifestParameter::UnitsHint::UNKNOWN);
    send->setDefaultLinkType(EffectManifestParameter::LinkType::LINKED);
    send->setRange(0.0, db2ratio(-3.0), 1.0);

    EffectManifestParameterPointer triplet = pManifest->addParameter();
    triplet->setId("triplet");
    triplet->setName(QObject::tr("Triplets"));
    triplet->setShortName(QObject::tr("Triplets"));
    triplet->setDescription(QObject::tr(
        "When the Quantize parameter is enabled, divide rounded 1/4 beats of Time parameter by 3."));
    triplet->setValueScaler(EffectManifestParameter::ValueScaler::TOGGLE);
    triplet->setSemanticHint(EffectManifestParameter::SemanticHint::UNKNOWN);
    triplet->setUnitsHint(EffectManifestParameter::UnitsHint::UNKNOWN);
    triplet->setRange(0, 0, 1);

    return pManifest;
}

void BeatGrindEffect::loadEngineEffectParameters(
        const QMap<QString, EngineEffectParameterPointer>& parameters) {
    m_pDelayParameter = parameters.value("delay_time");
    m_pQuantizeParameter = parameters.value("quantize");
    m_pSendParameter = parameters.value("send_amount");
    m_pFeedbackParameter = parameters.value("feedback_amount");
    m_pTripletParameter = parameters.value("triplet");
}

void BeatGrindEffect::processChannel(
        BeatGrindState* pGroupState,
        const CSAMPLE* pInput,
        CSAMPLE* pOutput,
        const mixxx::EngineParameters& bufferParameters,
        const EffectEnableState enableState,
        const GroupFeatureState& groupFeatures) {
    BeatGrindState& gs = *pGroupState;
    // The minimum of the parameter is zero so the exact center of the knob is 1 beat.

    double send_current = m_pSendParameter->value();
    double feedback_current = m_pFeedbackParameter->value();
    double period = m_pDelayParameter->value();

    if (enableState == EffectEnableState::Enabling) {
        gs.write_position = 0;
        gs.read_position = 0;
    }

    int delay_frames;
    if (groupFeatures.has_beat_length_sec) {
        // period is a number of beats
        if (m_pQuantizeParameter->toBool()) {
            period = std::max(roundToFraction(period, 4), 1/8.0);
            if (m_pTripletParameter->toBool()) {
                period /= 3.0;
            }
        } else if (period < 1/8.0) {
            period = 1/8.0;
        }
        delay_frames = period * groupFeatures.beat_length_sec * bufferParameters.sampleRate();
    } else {
        // period is a number of seconds
        period = std::max(period, 1/8.0);
        delay_frames = period * bufferParameters.sampleRate();
    }
    VERIFY_OR_DEBUG_ASSERT(delay_frames > 0) {
        delay_frames = 1;
    }

    int delay_samples = delay_frames * bufferParameters.channelCount();
    VERIFY_OR_DEBUG_ASSERT(delay_samples <= gs.delay_buf.size()) {
        delay_samples = gs.delay_buf.size();
    }

    RampingValue<CSAMPLE_GAIN> send(send_current, gs.prev_send,
                                    bufferParameters.framesPerBuffer());
    // Feedback the delay buffer and then add the new input.

    RampingValue<CSAMPLE_GAIN> feedback(feedback_current, gs.prev_feedback,
                                        bufferParameters.framesPerBuffer());

    qDebug() << "loop samples" << delay_samples;
    qDebug() << "write pos" << gs.write_position;
    qDebug() << "read pos" << gs.read_position;
    
    for (unsigned int i = 0;
            i < bufferParameters.samplesPerBuffer();
            i += bufferParameters.channelCount()) {
        CSAMPLE_GAIN send_ramped = send.getNext();
        CSAMPLE_GAIN feedback_ramped = feedback.getNext();
        auto bufferedSample = 
            std::make_unique<CSAMPLE []>(bufferParameters.channelCount());

        for (int channel = 0; channel < bufferParameters.channelCount(); channel++) {
            if (gs.isRecording) {
                if (gs.write_position >= gs.delay_buf.size()) {
                    gs.isRecording = false;
                }
                else {
                    gs.delay_buf[gs.write_position++] = pInput[i + channel];
                }
            }
            if (gs.write_position >= delay_samples) {
                if(gs.read_position >= delay_samples) {
                    gs.read_position = 0;
                }
                    bufferedSample[channel] = gs.delay_buf[gs.read_position++];
            }
            pOutput[i + channel] = SampleUtil::clampSample(
                pInput[i + channel] * send_ramped +
                bufferedSample[channel] * feedback_ramped);
        }
    }
    // The ramping of the send parameter handles ramping when enabling, so
    // this effect must handle ramping to dry when disabling itself (instead
    // of being handled by EngineEffect::process).
    if (enableState == EffectEnableState::Disabling) {
        SampleUtil::applyRampingGain(pOutput, 1.0, 0.0, bufferParameters.samplesPerBuffer());
        gs.delay_buf.clear();
        gs.prev_send = 0;
    } else {
        gs.prev_send = send_current;
    }
    gs.prev_feedback = feedback_current;
}
