/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, Compressor/Expander reset/update.
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), branches EF_COMPRESSOR et EF_EXPANDER.
 * Effets couverts: Compressor, Expander.
 * Dependances conservees: formules log10/pow/attack/release de Drumboy.
 * Dependances non resolues: genTransition.active.
 * Statut: extraction legerement isolee, compilable seule.
 */

#include "dynamics_dsp.h"
#include <cmath>

namespace drumboy_fx {

static const float kThresholdData[] = {
    -24.0f, -23.0f, -22.0f, -21.0f, -20.0f, -19.0f, -18.0f, -17.0f, -16.0f,
    -15.0f, -14.0f, -13.0f, -12.0f, -11.0f, -10.0f, -9.0f, -8.0f, -7.0f,
    -6.0f, -5.0f, -4.0f, -3.0f, -2.0f, -1.0f, 0.0f};
static const float kRateData[] = {
    1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f,
    11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f, 17.0f, 18.0f, 19.0f, 20.0f,
    21.0f, 22.0f, 23.0f, 24.0f, 25.0f, 26.0f, 27.0f, 28.0f, 29.0f, 30.0f};
static const float kAttackTimeData[] = {
    0.001f, 0.002f, 0.003f, 0.004f, 0.005f, 0.006f, 0.007f, 0.008f, 0.009f,
    0.010f, 0.020f, 0.030f, 0.040f, 0.050f, 0.060f, 0.070f, 0.080f, 0.090f, 0.100f};
static const float kReleaseTimeData[] = {
    0.010f, 0.020f, 0.030f, 0.040f, 0.050f, 0.060f, 0.070f, 0.080f, 0.090f,
    0.100f, 0.200f, 0.300f, 0.400f, 0.500f, 0.600f, 0.700f, 0.800f, 0.900f, 1.000f};
static const float kEffectMixData[] = {
    0.00f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f, 0.45f,
    0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.00f};

void compressorReset(CompressorState& compressor, int32_t sampleRate) {
    compressor.active = false;
    compressor.aThreshold = 18;
    compressor.bRate = 3;
    compressor.cAttackTime = 0;
    compressor.dReleaseTime = 9;
    compressor.eMix = 20;
    compressor.gainSmoothPrev = 0.0f;
    compressorUpdate(compressor, sampleRate);
}

void compressorUpdate(CompressorState& compressor, int32_t sampleRate) {
    compressor.threshold = kThresholdData[compressor.aThreshold];
    compressor.rate = kRateData[compressor.bRate];
    compressor.attackTime = kAttackTimeData[compressor.cAttackTime];
    compressor.releaseTime = kReleaseTimeData[compressor.dReleaseTime];
    compressor.wetFloat = kEffectMixData[compressor.eMix];
    compressor.dryFloat = 1.0f - compressor.wetFloat;
    compressor.attackAlpha = std::exp(-std::log(9.0f) / (sampleRate * compressor.attackTime));
    compressor.releaseAlpha = std::exp(-std::log(9.0f) / (sampleRate * compressor.releaseTime));
}

int32_t compressorProcessSample(CompressorState& compressor, int32_t input, int32_t int24Max) {
    if (!compressor.active) {
        return input;
    }

    float inFloat = static_cast<float>(input) / int24Max;
    float inAbs = std::fabs(inFloat);
    float inDb = 20.0f * std::log10(inAbs / 1.0f);
    if (inDb < -96.0f) {
        inDb = -96.0f;
    }

    float gain = inDb;
    if (inDb > compressor.threshold) {
        gain = compressor.threshold + (inDb - compressor.threshold) / compressor.rate;
    }
    float gainDb = gain - inDb;
    float gainSmooth;
    if (gainDb < compressor.gainSmoothPrev) {
        gainSmooth = ((1.0f - compressor.attackAlpha) * gainDb) + (compressor.attackAlpha * compressor.gainSmoothPrev);
    } else {
        gainSmooth = ((1.0f - compressor.releaseAlpha) * gainDb) + (compressor.releaseAlpha * compressor.gainSmoothPrev);
    }
    compressor.gainSmoothPrev = gainSmooth;

    float amp = std::pow(10.0f, gainSmooth / 20.0f);
    return static_cast<int32_t>((amp * input * compressor.wetFloat) + (input * compressor.dryFloat));
}

void expanderReset(ExpanderState& expander, int32_t sampleRate) {
    expander.active = false;
    expander.aThreshold = 12;
    expander.bRate = 3;
    expander.cAttackTime = 0;
    expander.dReleaseTime = 13;
    expander.eMix = 20;
    expander.gainSmoothPrev = -144.0f;
    expanderUpdate(expander, sampleRate);
}

void expanderUpdate(ExpanderState& expander, int32_t sampleRate) {
    expander.threshold = kThresholdData[expander.aThreshold];
    expander.rate = kRateData[expander.bRate];
    expander.attackTime = kAttackTimeData[expander.cAttackTime];
    expander.releaseTime = kReleaseTimeData[expander.dReleaseTime];
    expander.wetFloat = kEffectMixData[expander.eMix];
    expander.dryFloat = 1.0f - expander.wetFloat;
    expander.attackAlpha = std::exp(-std::log(9.0f) / (sampleRate * expander.attackTime));
    expander.releaseAlpha = std::exp(-std::log(9.0f) / (sampleRate * expander.releaseTime));
}

int32_t expanderProcessSample(ExpanderState& expander, int32_t input, int32_t int24Max) {
    if (!expander.active) {
        return input;
    }

    float inFloat = static_cast<float>(input) / int24Max;
    float inAbs = std::fabs(inFloat);
    float inDb = 20.0f * std::log10(inAbs / 1.0f);
    if (inDb < -144.0f) {
        inDb = -144.0f;
    }

    float gain = inDb;
    if (inDb <= expander.threshold) {
        gain = expander.threshold + (inDb - expander.threshold) * expander.rate;
    }
    float gainDb = gain - inDb;
    float gainSmooth;
    if (gainDb > expander.gainSmoothPrev) {
        gainSmooth = ((1.0f - expander.attackAlpha) * gainDb) + (expander.attackAlpha * expander.gainSmoothPrev);
    } else {
        gainSmooth = ((1.0f - expander.releaseAlpha) * gainDb) + (expander.releaseAlpha * expander.gainSmoothPrev);
    }
    expander.gainSmoothPrev = gainSmooth;

    float amp = std::pow(10.0f, gainSmooth / 20.0f);
    return static_cast<int32_t>((amp * input * expander.wetFloat) + (input * expander.dryFloat));
}

} // namespace drumboy_fx

