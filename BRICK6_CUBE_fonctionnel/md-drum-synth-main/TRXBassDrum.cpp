#include "TRXBassDrum.h"
#include "DrumUiAbstraction.h"
#if MD_DRUM_HAS_DESKTOP_UI
#include "imgui.h"
#endif
#include <cmath>
#include <algorithm>
#include <cstdlib>

constexpr float kSampleRate = 48000.0f;
constexpr uint16_t kNoiseAttackSamples = 479U;
constexpr uint8_t kNoiseHoldSamples = 6U;
constexpr uint8_t kRetrigBlendSamples = 16U;
constexpr float kSweepPitchRatio = 6.0f;

void TRXBassDrum::Init() {
    phase = env = rampEnv = attackEnv = 0.0f;
    prevSample = 0.0f;
    retrigStartSample = 0.0f;
    retrigBlendSamplesRemaining = 0U;
    attackSamplesRemaining = 0U;
    noiseHoldCounter = 0U;
    noiseSample = 0.0f;
    prevNoiseSample = 0.0f;
    UpdateDerived();
}

void TRXBassDrum::Trigger() {
    const uint8_t wasActive = (env > 0.0001f) ? 1U : 0U;
    retrigStartSample = prevSample;
    retrigBlendSamplesRemaining = wasActive ? kRetrigBlendSamples : 0U;

    env = 1.0f;
    rampEnv = 1.0f;
    attackEnv = 0.0f;
    phase = 0.0f;
    attackSamplesRemaining = kNoiseAttackSamples;
    noiseHoldCounter = 0U;
    noiseSample = 0.0f;
    prevNoiseSample = 0.0f;
    UpdateDerived();
}

float TRXBassDrum::Process() {
    if (env <= 0.0001f) {
        prevSample = 0.0f;
        return 0.0f;
    }

    // Envelope decay
    env *= envDecayCoef;
    rampEnv *= rampDecayCoef;
    attackEnv += (1.0f - attackEnv) * (1.0f - attackRiseCoef);

    // Frequency modulation
    phase += pitchPhaseInc + (rampPhaseIncScale * rampEnv);
    if (phase > 1.0f) phase -= 1.0f;

    float sineOut = sine(phase * 2.0f * M_PI);
    const float tonalEnv = env * attackEnv;
    float value = sineOut * tonalEnv;

    // Add harmonic distortion
    if (harmonics > 0.0f) {
        value += harmonics * std::tanh(sineOut * 3.0f) * tonalEnv;
    }

    // Add noise burst
    if (noise > 0.0f && attackSamplesRemaining > 0U) {
        if (noiseHoldCounter == 0U) {
            noiseSample = ((rand() / (float)RAND_MAX) * 2.0f - 1.0f);
            noiseHoldCounter = kNoiseHoldSamples;
        }
        --noiseHoldCounter;
        const float shapedNoise = noiseSample - prevNoiseSample;
        prevNoiseSample = noiseSample;
        value += noise * shapedNoise * env;
        --attackSamplesRemaining;
    }

    // Soft clip
    if (clip > 0.0f) {
        value = std::tanh(value * driveGain);
    }

    if (retrigBlendSamplesRemaining > 0U) {
        const float blend = static_cast<float>(retrigBlendSamplesRemaining) /
                            static_cast<float>(kRetrigBlendSamples);
        value += (retrigStartSample - value) * blend;
        --retrigBlendSamplesRemaining;
    }

    prevSample = value;
    return value;
}

void TRXBassDrum::UpdateDerived() {
    const float safeDecay = std::max(decay, 0.0001f);
    const float safeRampDecay = std::max(rampDecay, 0.0001f);
    const float attackTime = 0.0002f + (std::max(attack, 0.0f) * 0.01f);
    envDecayCoef = std::exp(-1.0f / (safeDecay * kSampleRate));
    rampDecayCoef = std::exp(-1.0f / (safeRampDecay * kSampleRate));
    attackRiseCoef = std::exp(-1.0f / (attackTime * kSampleRate));

    pitchPhaseInc = pitch / kSampleRate;
    rampPhaseIncScale = (ramp * pitch * kSweepPitchRatio) / kSampleRate;
    driveGain = 1.0f + (clip * 5.0f);
}

void TRXBassDrum::RenderControls() {
#if MD_DRUM_HAS_DESKTOP_UI

    ImGui::SliderFloat("Pitch", &pitch, 20.0f, 120.0f);
    ImGui::SliderFloat("Decay", &decay, 0.01f, 2.0f);
    ImGui::SliderFloat("Ramp", &ramp, 0.0f, 1.0f);
    ImGui::SliderFloat("Ramp Decay", &rampDecay, 0.01f, 1.0f);
    ImGui::SliderFloat("Attack", &attack, 0.0f, 2.0f);
    ImGui::SliderFloat("Noise", &noise, 0.0f, 1.0f);
    ImGui::SliderFloat("Harmonics", &harmonics, 0.0f, 1.0f);
    ImGui::SliderFloat("Clip", &clip, 0.0f, 1.0f);

#else
    /* Desktop UI disabled in embedded DSP builds. */
#endif
}


float TRXBassDrum::sine(float x) {
    return std::sin(x); // Replace with lookup if performance becomes a concern
}
