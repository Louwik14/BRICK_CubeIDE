#include "TRXBassDrum.h"
#include "DrumUiAbstraction.h"
#if MD_DRUM_HAS_DESKTOP_UI
#include "imgui.h"
#endif
#include <cmath>
#include <algorithm>

constexpr float kSampleRate = 48000.0f;
constexpr uint16_t kNoiseAttackSamples = 479U;

void TRXBassDrum::Init() {
    phase = env = rampEnv = 0.0f;
    prevSample = 0.0f;
    attackSamplesRemaining = 0U;
    UpdateDerived();
}

void TRXBassDrum::Trigger() {
    env = 1.0f;
    rampEnv = 1.0f;
    phase = 0.0f;
    attackSamplesRemaining = kNoiseAttackSamples;
    UpdateDerived();
}

float TRXBassDrum::Process() {
    if (env <= 0.0001f) return 0.0f;

    // Envelope decay
    env *= envDecayCoef;
    rampEnv *= rampDecayCoef;

    // Frequency modulation
    phase += pitchPhaseInc + (rampPhaseIncScale * rampEnv);
    if (phase > 1.0f) phase -= 1.0f;

    float sineOut = sine(phase * 2.0f * M_PI);
    float value = sineOut * env * start;

    // Add noise burst
    if (noise > 0.0f && attackSamplesRemaining > 0U) {
        value += noise * ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * env;
        --attackSamplesRemaining;
    }

    // Soft clip
    if (clip > 0.0f) {
        value = std::tanh(value * driveGain);
    }

    return value;
}

void TRXBassDrum::UpdateDerived() {
    const float safeDecay = std::max(decay, 0.0001f);
    const float safeRampDecay = std::max(rampDecay, 0.0001f);
    envDecayCoef = std::exp(-1.0f / (safeDecay * kSampleRate));
    rampDecayCoef = std::exp(-1.0f / (safeRampDecay * kSampleRate));

    pitchPhaseInc = pitch / kSampleRate;
    rampPhaseIncScale = (ramp * 1000.0f) / kSampleRate;
    const float harmonicDrive = harmonics * 2.0f;
    driveGain = 1.0f + (clip * 5.0f) + harmonicDrive;
}

void TRXBassDrum::RenderControls() {
#if MD_DRUM_HAS_DESKTOP_UI

    ImGui::SliderFloat("Pitch", &pitch, 20.0f, 120.0f);
    ImGui::SliderFloat("Decay", &decay, 0.01f, 2.0f);
    ImGui::SliderFloat("Ramp", &ramp, 0.0f, 1.0f);
    ImGui::SliderFloat("Ramp Decay", &rampDecay, 0.01f, 1.0f);
    ImGui::SliderFloat("Start", &start, 0.0f, 2.0f);
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
