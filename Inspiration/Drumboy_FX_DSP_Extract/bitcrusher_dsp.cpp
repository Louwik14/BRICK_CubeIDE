/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, Bitcrusher::reset() / update().
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), branche EF_BITCRUSHER.
 * Effet couvert: Bitcrusher.
 * Dependances conservees: logique BIT_CLIP / BIT_FOLD, masque 24-bit, sampleCounter.
 * Dependances non resolues: genTransition.active.
 * Statut: extraction legerement isolee, compilable seule.
 */

#include "bitcrusher_dsp.h"
#include <cmath>

namespace drumboy_fx {

static const uint8_t kResolutionData[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
static const uint8_t kSampleRateData[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
static const float kThresholdData[] = {
    -24.0f, -23.0f, -22.0f, -21.0f, -20.0f, -19.0f, -18.0f, -17.0f, -16.0f,
    -15.0f, -14.0f, -13.0f, -12.0f, -11.0f, -10.0f, -9.0f, -8.0f, -7.0f,
    -6.0f, -5.0f, -4.0f, -3.0f, -2.0f, -1.0f, 0.0f};
static const float kEffectMixData[] = {
    0.00f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f, 0.45f,
    0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.00f};

void bitcrusherReset(BitcrusherState& bitcrusher, int32_t int24Max) {
    bitcrusher.active = false;
    bitcrusher.aResolution = 7;
    bitcrusher.bSampleRate = 0;
    bitcrusher.cThreshold = 24;
    bitcrusher.dDry = 5;
    bitcrusher.eWet = 15;
    bitcrusher.mode = BIT_FOLD;
    bitcrusher.sampleCounter = 0;
    bitcrusher.sampleData = 0;
    bitcrusherUpdate(bitcrusher, int24Max);
}

void bitcrusherUpdate(BitcrusherState& bitcrusher, int32_t int24Max) {
    bitcrusher.resolution = kResolutionData[bitcrusher.aResolution];
    bitcrusher.sampleRate = kSampleRateData[bitcrusher.bSampleRate];
    bitcrusher.threshold = static_cast<int8_t>(kThresholdData[bitcrusher.cThreshold]);
    bitcrusher.dryFloat = kEffectMixData[bitcrusher.dDry];
    bitcrusher.wetFloat = kEffectMixData[bitcrusher.eWet];
    bitcrusher.sampleCounterMax = bitcrusher.sampleRate;

    bitcrusher.resModifier = 0;
    for (uint8_t i = 0; i < bitcrusher.resolution; i++) {
        bitcrusher.resModifier <<= 1;
        bitcrusher.resModifier += 1;
    }
    for (uint8_t j = 0; j < (24 - bitcrusher.resolution); j++) {
        bitcrusher.resModifier <<= 1;
    }
    bitcrusher.resModifier += 0xFF000000U;

    bitcrusher.limitMultiplier = std::pow(10.0f, bitcrusher.threshold / 20.0f);
    bitcrusher.limitPos = static_cast<int32_t>(bitcrusher.limitMultiplier * int24Max);
    bitcrusher.limitNeg = -bitcrusher.limitPos;
}

int32_t bitcrusherProcessSample(BitcrusherState& bitcrusher, int32_t input) {
    if (!bitcrusher.active) {
        return input;
    }

    bitcrusher.sampleCounter += 1;
    if (bitcrusher.sampleCounter >= bitcrusher.sampleCounterMax) {
        bitcrusher.sampleCounter = 0;
        bitcrusher.sampleData = input;

        while ((bitcrusher.sampleData < bitcrusher.limitNeg) || (bitcrusher.sampleData > bitcrusher.limitPos)) {
            if (bitcrusher.sampleData > bitcrusher.limitPos) {
                switch (bitcrusher.mode) {
                case BIT_CLIP:
                    bitcrusher.sampleData = bitcrusher.limitPos;
                    break;
                case BIT_FOLD:
                    bitcrusher.sampleData = bitcrusher.limitPos - (bitcrusher.sampleData - bitcrusher.limitPos);
                    if (bitcrusher.sampleData < 0) {
                        bitcrusher.sampleData *= -1;
                    }
                    break;
                }
            } else if (bitcrusher.sampleData < bitcrusher.limitNeg) {
                switch (bitcrusher.mode) {
                case BIT_CLIP:
                    bitcrusher.sampleData = bitcrusher.limitNeg;
                    break;
                case BIT_FOLD:
                    bitcrusher.sampleData = bitcrusher.limitNeg - (bitcrusher.sampleData - bitcrusher.limitNeg);
                    if (bitcrusher.sampleData > 0) {
                        bitcrusher.sampleData *= -1;
                    }
                    break;
                }
            }
        }
    }

    int32_t dataOut = static_cast<int32_t>(static_cast<uint32_t>(bitcrusher.sampleData) & bitcrusher.resModifier);
    return static_cast<int32_t>((dataOut * bitcrusher.wetFloat) + (input * bitcrusher.dryFloat));
}

} // namespace drumboy_fx

