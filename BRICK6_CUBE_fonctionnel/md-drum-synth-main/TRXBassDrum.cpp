#include "TRXBassDrum.h"
#include "DrumUiAbstraction.h"
#if MD_DRUM_HAS_DESKTOP_UI
#include "imgui.h"
#endif
#include <cmath>
#include <algorithm>

constexpr float kSampleRate = 48000.0f;
constexpr float kSweepPitchRatio = 6.0f;
constexpr float kTickTransientTime = 0.00025f;

void TRXBassDrum::Init() {
    phase = env = rampEnv = attackEnv = 0.0f;
    prevSample = 0.0f;
    tickTransient = 0.0f;
    UpdateDerived();
}

void TRXBassDrum::Trigger() {
    const float preTriggerSample = prevSample;
    env = 1.0f;
    rampEnv = 1.0f;
    attackEnv = 0.0f;
    phase = 0.0f;
    const float tickGain = std::clamp(tick, 0.0f, 1.0f) * 2.0f;
    tickTransient = (-preTriggerSample) * (1.0f - tickGain);
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

    // Control natural trigger discontinuity (no white-noise burst).
    value += tickTransient;
    tickTransient *= tickTransientDecayCoef;

    // Soft clip
    if (clip > 0.0f) {
        value = std::tanh(value * driveGain);
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
    tickTransientDecayCoef = std::exp(-1.0f / (kTickTransientTime * kSampleRate));
}

void TRXBassDrum::RenderControls() {
#if MD_DRUM_HAS_DESKTOP_UI

    ImGui::SliderFloat("Pitch", &pitch, 20.0f, 120.0f);
    ImGui::SliderFloat("Decay", &decay, 0.01f, 2.0f);
    ImGui::SliderFloat("Ramp", &ramp, 0.0f, 1.0f);
    ImGui::SliderFloat("Ramp Decay", &rampDecay, 0.01f, 1.0f);
    ImGui::SliderFloat("Attack", &attack, 0.0f, 2.0f);
    ImGui::SliderFloat("Tick", &tick, 0.0f, 1.0f);
    ImGui::SliderFloat("Harmonics", &harmonics, 0.0f, 1.0f);
    ImGui::SliderFloat("Clip", &clip, 0.0f, 1.0f);

#else
    /* Desktop UI disabled in embedded DSP builds. */
#endif
}


float TRXBassDrum::sine(float x) {
    return std::sin(x); // Replace with lookup if performance becomes a concern
}
