/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, section "Effect-Flanger Constants".
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), branche EF_FLANGER.
 * Effet couvert: Flanger.
 * Dependances conservees: buffer court, interpolation lineaire, modulation de decalage,
 *   feedback, dry/wet.
 * Dependances non resolues: transitions Drumboy EffectGenTransition.
 * Statut: extraction legerement isolee, compilable seule.
 */

#pragma once

#include <cstdint>

namespace drumboy_fx {

struct FlangerState {
    static const uint16_t kFlangerBufferSize = 250U;

    bool active = false;
    uint8_t aTime = 3;
    uint8_t bFeedback = 5;
    uint8_t cRate = 4;
    uint8_t dDry = 15;
    uint8_t eWet = 15;
    float time = 0.003f;
    float feedback = 0.25f;
    float rate = 0.50f;
    float dryFloat = 0.75f;
    float wetFloat = 0.75f;
    float depth = 0.90f;
    uint16_t lag = 0;
    uint16_t recordInterval = 0;
    uint16_t playInterval = 0;
    float flangerInterval = 0.0f;
    float shiftInterval = 0.0f;
    float shiftFreq = 0.0f;
    float shiftMin = 0.0f;
    float shiftMax = 0.0f;
    float shiftInc = 0.0f;
    int32_t flangerBuffer[kFlangerBufferSize] = {0};
};

void flangerReset(FlangerState& flanger, int32_t sampleRate = 44100);
void flangerUpdate(FlangerState& flanger, int32_t sampleRate = 44100);
void flangerCleanMemory(FlangerState& flanger, int32_t sampleRate = 44100);
int32_t flangerProcessSample(FlangerState& flanger, int32_t input);

} // namespace drumboy_fx

