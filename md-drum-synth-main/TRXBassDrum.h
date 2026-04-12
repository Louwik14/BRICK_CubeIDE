#pragma once
#include "DrumModel.h"
#include <cstdint>

class TRXBassDrum : public DrumModel {
public:
    void Init() override;
    void Trigger() override;
    float Process() override;
    void RenderControls() override;

    void saveParameters(std::ostream& os) const override {
        os << pitch << ' ' << decay << ' ' << ramp << ' ' << rampDecay << ' '
           << attack << ' ' << noise << ' ' << harmonics << ' ' << clip << '\n';
    }

    void loadParameters(std::istream& is) override {
        is >> pitch >> decay >> ramp >> rampDecay >> attack >> noise >> harmonics >> clip;
    }

    bool SetParamByIndex(uint8_t index, float value) override {
        switch (index) {
            case 0U: pitch = value; UpdateDerived(); return true;
            case 1U: decay = value; UpdateDerived(); return true;
            case 2U: ramp = value; UpdateDerived(); return true;
            case 3U: rampDecay = value; UpdateDerived(); return true;
            case 4U: attack = value; UpdateDerived(); return true;
            case 5U: noise = value; UpdateDerived(); return true;
            case 6U: harmonics = value; UpdateDerived(); return true;
            case 7U: clip = value; UpdateDerived(); return true;
            default: return false;
        }
    }

private:
    // User parameters
    float pitch = 50.0f;       // Base pitch in Hz
    float decay = 0.4f;        // Envelope decay time
    float ramp = 0.3f;         // Frequency ramp amount
    float rampDecay = 0.1f;    // Ramp decay time
    float attack = 1.0f;       // Attack control (mapped to attack time)
    float noise = 0.0f;        // Noise at attack
    float harmonics = 0.0f;    // Adds clipped harmonic content
    float clip = 0.0f;         // Soft clipping amount

    // Internal state
    float phase = 0.0f;
    float env = 0.0f;
    float rampEnv = 0.0f;
    float attackEnv = 0.0f;
    float prevSample = 0.0f;
    uint16_t attackSamplesRemaining = 0U;
    uint8_t noiseHoldCounter = 0U;
    float noiseSample = 0.0f;
    float prevNoiseSample = 0.0f;

    // Precomputed derivatives (updated on trigger / parameter change)
    float envDecayCoef = 1.0f;
    float rampDecayCoef = 1.0f;
    float attackRiseCoef = 1.0f;
    float pitchPhaseInc = 0.0f;
    float rampPhaseIncScale = 0.0f;
    float driveGain = 1.0f;

    // Helpers
    void UpdateDerived();
    float sine(float x);
};
