/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, sections
 *   "Effect-Overdrive Constants" et "Effect-Distortion Constants".
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), branches EF_OVERDRIVE et EF_DISTORTION.
 * Effets couverts: Overdrive, Distortion.
 * Dependances conservees: gain/threshold dB, waveshaper, filtre tone biquad,
 *   dry/wet.
 * Dependances non resolues: transitions Drumboy EffectGenTransition.
 * Statut: extraction legerement isolee, compilable seule.
 */

#pragma once

#include <cstdint>

namespace drumboy_fx {

struct DriveFilterState {
    float a0 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    int32_t dataIn[3] = {0, 0, 0};
    int32_t dataOut[3] = {0, 0, 0};
};

struct OverdriveState {
    bool active = false;
    uint8_t aGain = 15;
    uint8_t bThreshold = 18;
    uint8_t cTone = 31;
    uint8_t dDry = 5;
    uint8_t eWet = 15;
    float gaindB = 15.0f;
    float gain = 5.623f;
    float thresholddB = -6.0f;
    float threshold = 0.501f;
    float tone = 4000.0f;
    float dryFloat = 0.25f;
    float wetFloat = 0.75f;
    DriveFilterState filter;
};

struct DistortionState {
    bool active = false;
    uint8_t aGain = 15;
    uint8_t bThreshold = 18;
    uint8_t cTone = 31;
    uint8_t dDry = 5;
    uint8_t eWet = 15;
    float gaindB = 15.0f;
    float gain = 5.623f;
    float thresholddB = -6.0f;
    float threshold = 0.501f;
    float tone = 4000.0f;
    float dryFloat = 0.25f;
    float wetFloat = 0.75f;
    DriveFilterState filter;
};

void overdriveReset(OverdriveState& overdrive, int32_t sampleRate = 44100);
void overdriveUpdate(OverdriveState& overdrive, int32_t sampleRate = 44100);
void overdriveCleanMemory(OverdriveState& overdrive);
int32_t overdriveProcessSample(OverdriveState& overdrive, int32_t input, int32_t int24Max = 8388607);

void distortionReset(DistortionState& distortion, int32_t sampleRate = 44100);
void distortionUpdate(DistortionState& distortion, int32_t sampleRate = 44100);
void distortionCleanMemory(DistortionState& distortion);
int32_t distortionProcessSample(DistortionState& distortion, int32_t input, int32_t int24Max = 8388607);

} // namespace drumboy_fx

