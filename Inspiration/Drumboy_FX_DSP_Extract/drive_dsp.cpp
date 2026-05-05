/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, Overdrive/Distortion
 *   reset(), update(), calculateFilterCoef(), cleanMemory().
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), branches EF_OVERDRIVE et EF_DISTORTION.
 * Effets couverts: Overdrive, Distortion.
 * Dependances conservees: waveshaper cubique overdrive, waveshaper atanf distortion,
 *   filtre tone low-pass biquad identique.
 * Dependances non resolues: genTransition.active.
 * Statut: extraction legerement isolee, compilable seule.
 */

#include "drive_dsp.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace drumboy_fx {

static const float kGainData[] = {
    0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f,
    10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f, 17.0f, 18.0f, 19.0f,
    20.0f, 21.0f, 22.0f, 23.0f, 24.0f};
static const float kThresholdData[] = {
    -24.0f, -23.0f, -22.0f, -21.0f, -20.0f, -19.0f, -18.0f, -17.0f, -16.0f,
    -15.0f, -14.0f, -13.0f, -12.0f, -11.0f, -10.0f, -9.0f, -8.0f, -7.0f,
    -6.0f, -5.0f, -4.0f, -3.0f, -2.0f, -1.0f, 0.0f};
static const float kToneData[] = {
    10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 150, 200, 250, 300, 350, 400,
    450, 500, 550, 600, 650, 700, 750, 800, 850, 900, 950, 1000, 2000, 3000,
    4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 13000, 14000,
    15000, 16000, 17000, 18000, 19000, 20000};
static const float kEffectMixData[] = {
    0.00f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f, 0.45f,
    0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.00f};

static void calculateToneFilter(DriveFilterState& filter, float tone, int32_t sampleRate) {
    float Q = 0.707f;
    float K = std::tan(static_cast<float>(M_PI) * tone / sampleRate);
    float norm = 1.0f / (1.0f + K / Q + K * K);
    filter.a0 = K * K * norm;
    filter.a1 = 2.0f * filter.a0;
    filter.a2 = filter.a0;
    filter.b1 = 2.0f * (K * K - 1.0f) * norm;
    filter.b2 = (1.0f - K / Q + K * K) * norm;
}

static int32_t processToneFilter(DriveFilterState& filter, int32_t filterInput) {
    filter.dataIn[0] = filterInput;
    filter.dataOut[0] = static_cast<int32_t>(
        (filter.a0 * filter.dataIn[0]) + (filter.a1 * filter.dataIn[1]) + (filter.a2 * filter.dataIn[2]) -
        (filter.b1 * filter.dataOut[1]) - (filter.b2 * filter.dataOut[2]));

    filter.dataIn[2] = filter.dataIn[1];
    filter.dataIn[1] = filter.dataIn[0];
    filter.dataOut[2] = filter.dataOut[1];
    filter.dataOut[1] = filter.dataOut[0];
    return filter.dataOut[0];
}

static void clearFilter(DriveFilterState& filter) {
    filter.dataIn[0] = filter.dataIn[1] = filter.dataIn[2] = 0;
    filter.dataOut[0] = filter.dataOut[1] = filter.dataOut[2] = 0;
}

void overdriveReset(OverdriveState& overdrive, int32_t sampleRate) {
    overdrive.active = false;
    overdrive.aGain = 15;
    overdrive.bThreshold = 18;
    overdrive.cTone = 31;
    overdrive.dDry = 5;
    overdrive.eWet = 15;
    clearFilter(overdrive.filter);
    overdriveUpdate(overdrive, sampleRate);
}

void overdriveUpdate(OverdriveState& overdrive, int32_t sampleRate) {
    overdrive.gaindB = kGainData[overdrive.aGain];
    overdrive.thresholddB = kThresholdData[overdrive.bThreshold];
    overdrive.tone = kToneData[overdrive.cTone];
    overdrive.gain = std::pow(10.0f, overdrive.gaindB / 20.0f);
    overdrive.threshold = std::pow(10.0f, overdrive.thresholddB / 20.0f);
    overdrive.dryFloat = kEffectMixData[overdrive.dDry];
    overdrive.wetFloat = kEffectMixData[overdrive.eWet];
    calculateToneFilter(overdrive.filter, overdrive.tone, sampleRate);
}

void overdriveCleanMemory(OverdriveState& overdrive) {
    clearFilter(overdrive.filter);
}

int32_t overdriveProcessSample(OverdriveState& overdrive, int32_t input, int32_t int24Max) {
    if (!overdrive.active) {
        return input;
    }

    double inDouble = (static_cast<double>(input) / int24Max) * overdrive.gain;
    double outDouble;
    if (inDouble < -overdrive.threshold) {
        outDouble = -2.0 * overdrive.threshold / 3.0;
    } else if (inDouble > overdrive.threshold) {
        outDouble = 2.0 * overdrive.threshold / 3.0;
    } else {
        outDouble = inDouble - (inDouble * inDouble * inDouble) / 3.0;
    }
    if (outDouble > 1.0) {
        outDouble = 1.0;
    }
    if (outDouble < -1.0) {
        outDouble = -1.0;
    }

    int32_t filterInput = static_cast<int32_t>(outDouble * int24Max);
    int32_t filterOutput = processToneFilter(overdrive.filter, filterInput);
    return static_cast<int32_t>((filterOutput * overdrive.wetFloat) + (input * overdrive.dryFloat));
}

void distortionReset(DistortionState& distortion, int32_t sampleRate) {
    distortion.active = false;
    distortion.aGain = 15;
    distortion.bThreshold = 18;
    distortion.cTone = 31;
    distortion.dDry = 5;
    distortion.eWet = 15;
    clearFilter(distortion.filter);
    distortionUpdate(distortion, sampleRate);
}

void distortionUpdate(DistortionState& distortion, int32_t sampleRate) {
    distortion.gaindB = kGainData[distortion.aGain];
    distortion.thresholddB = kThresholdData[distortion.bThreshold];
    distortion.tone = kToneData[distortion.cTone];
    distortion.gain = std::pow(10.0f, distortion.gaindB / 20.0f);
    distortion.threshold = std::pow(10.0f, distortion.thresholddB / 20.0f);
    distortion.dryFloat = kEffectMixData[distortion.dDry];
    distortion.wetFloat = kEffectMixData[distortion.eWet];
    calculateToneFilter(distortion.filter, distortion.tone, sampleRate);
}

void distortionCleanMemory(DistortionState& distortion) {
    clearFilter(distortion.filter);
}

int32_t distortionProcessSample(DistortionState& distortion, int32_t input, int32_t int24Max) {
    if (!distortion.active) {
        return input;
    }

    float inFloat = (static_cast<float>(input) / int24Max) * distortion.gain;
    float outFloat = (2.0f / distortion.gain) * std::atan(distortion.gain * inFloat) * distortion.threshold;
    int32_t filterInput = static_cast<int32_t>(outFloat * int24Max);
    int32_t filterOutput = processToneFilter(distortion.filter, filterInput);
    return static_cast<int32_t>((filterOutput * distortion.wetFloat) + (input * distortion.dryFloat));
}

} // namespace drumboy_fx

