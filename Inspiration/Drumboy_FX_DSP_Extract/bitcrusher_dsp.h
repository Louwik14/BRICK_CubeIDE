/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, section "Effect-Bitcrusher Constants".
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), branche EF_BITCRUSHER.
 * Effet couvert: Bitcrusher.
 * Dependances conservees: sample hold, resolution mask, threshold clip/fold, dry/wet.
 * Dependances non resolues: transitions Drumboy EffectGenTransition.
 * Statut: extraction legerement isolee, compilable seule.
 */

#pragma once

#include <cstdint>

namespace drumboy_fx {

enum BitcrusherMode {
    BIT_CLIP = 0x00,
    BIT_FOLD = 0x01,
};

struct BitcrusherState {
    bool active = false;
    uint8_t aResolution = 7;
    uint8_t bSampleRate = 0;
    uint8_t cThreshold = 24;
    uint8_t dDry = 5;
    uint8_t eWet = 15;
    uint8_t resolution = 8;
    uint8_t sampleRate = 1;
    int8_t threshold = 0;
    BitcrusherMode mode = BIT_FOLD;
    uint16_t sampleCounter = 0;
    uint16_t sampleCounterMax = 1;
    uint32_t resModifier = 0;
    float limitMultiplier = 1.0f;
    int32_t limitPos = 8388607;
    int32_t limitNeg = -8388607;
    int32_t sampleData = 0;
    float dryFloat = 0.25f;
    float wetFloat = 0.75f;
};

void bitcrusherReset(BitcrusherState& bitcrusher, int32_t int24Max = 8388607);
void bitcrusherUpdate(BitcrusherState& bitcrusher, int32_t int24Max = 8388607);
int32_t bitcrusherProcessSample(BitcrusherState& bitcrusher, int32_t input);

} // namespace drumboy_fx

