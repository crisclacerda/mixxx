#ifndef BITCRUSHEREFFECT_H
#define BITCRUSHEREFFECT_H

#include <QMap>

#include "effects/effectprocessor.h"
#include "engine/effects/engineeffect.h"
#include "engine/effects/engineeffectparameter.h"
#include "util/class.h"
#include "util/types.h"

struct BitCrusherGroupState : public EffectState {
    // Default accumulator to 1 so we immediately pick an input value.
    BitCrusherGroupState(const mixxx::EngineParameters& bufferParameters)
            : EffectState(bufferParameters),
              hold_l(0),
              hold_r(0),
              accumulator(1) {
    }
    CSAMPLE hold_l, hold_r;
    // Accumulated fractions of a samplerate period.
    CSAMPLE accumulator;
};

class BitCrusherEffect : public EffectProcessorImpl<BitCrusherGroupState> {
  public:
    BitCrusherEffect(EngineEffect* pEffect);
    virtual ~BitCrusherEffect();

    static QString getId();
    static EffectManifestPointer getManifest();

    // See effectprocessor.h
    void processChannel(const ChannelHandle& handle,
                        BitCrusherGroupState* pState,
                        const CSAMPLE* pInput, CSAMPLE *pOutput,
                        const mixxx::EngineParameters& bufferParameters,
                        const EffectEnableState enableState,
                        const GroupFeatureState& groupFeatureState);

  private:
    QString debugString() const {
        return getId();
    }

    EngineEffectParameterPointer m_pBitDepthParameter;
    EngineEffectParameterPointer m_pDownsampleParameter;

    DISALLOW_COPY_AND_ASSIGN(BitCrusherEffect);
};

#endif /* BITCRUSHEREFFECT_H */