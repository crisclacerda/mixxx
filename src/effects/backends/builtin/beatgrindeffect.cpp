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
      "Stores the input signal in a temporary buffer and loops it"));
    pManifest->setMetaknobDefault(db2ratio(-3.0));

    EffectManifestParameterPointer delay = pManifest->addParameter();
    delay->setId("loop_length");
    delay->setName(QObject::tr("Length"));
    delay->setShortName(QObject::tr("Length"));
    delay->setDescription(QObject::tr(
        "Length of the loop\n"
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
    feedback->setId("amplitude");
    feedback->setName(QObject::tr("Amplitude"));
    feedback->setShortName(QObject::tr("Amplitude"));
    feedback->setDescription(QObject::tr(
        "Volume of loop"));
    feedback->setValueScaler(EffectManifestParameter::ValueScaler::LINEAR);
    feedback->setSemanticHint(EffectManifestParameter::SemanticHint::UNKNOWN);
    feedback->setUnitsHint(EffectManifestParameter::UnitsHint::UNKNOWN);
    feedback->setRange(0.00, db2ratio(-3.0), 1.00);

    EffectManifestParameterPointer send = pManifest->addParameter();
    send->setId("dry_wet");
    send->setName(QObject::tr("Dry/Wet"));
    send->setShortName(QObject::tr("Dry/Wet"));
    send->setDescription(QObject::tr(
        "How much of the dry signal or the loop"));
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
    m_pDelayParameter = parameters.value("loop_length");
    m_pQuantizeParameter = parameters.value("quantize");
    m_pSendParameter = parameters.value("dry_wet");
    m_pFeedbackParameter = parameters.value("amplitude");
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
        gs.isRecording = true;
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
    
    for (unsigned int frame = 0;
            frame < bufferParameters.samplesPerBuffer();
            frame += bufferParameters.channelCount()) {
        CSAMPLE_GAIN send_ramped = send.getNext();
        CSAMPLE_GAIN feedback_ramped = feedback.getNext();    

        for (int channel = 0; channel < bufferParameters.channelCount(); channel++) {
            if (gs.isRecording) {
                if (gs.write_position >= gs.delay_buf.size()) {
                    gs.isRecording = false;
                } else {
                    gs.delay_buf[gs.write_position] = pInput[frame + channel];
                    gs.write_position += 1;
                }
            }
            if (gs.write_position >= delay_samples) {
                if (gs.read_position >= delay_samples) {
                    gs.read_position = 0;
                }
                gs.bufferedSample[channel] = gs.delay_buf[gs.read_position];
                gs.read_position += 1;
            }
            pOutput[frame + channel] = SampleUtil::clampSample(
                pInput[frame + channel] * send_ramped +
                gs.bufferedSample[channel] * feedback_ramped);
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
