/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, section "Effect-Delay Constants"
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), branche EF_DELAY.
 * Effet couvert: Delay.
 * Dependances conservees: int32 audio 24-bit, dry/wet, level, feedback, tempo-sync.
 * Dependances non resolues: transitions Drumboy EffectGenTransition, adresses SDRAM fixes
 *   RAM_DELAY_0/RAM_DELAY_1 remplacees ici par un buffer fourni par l'appelant.
 * Statut: extraction legerement isolee, compilable seule si appelee avec un buffer valide.
 */

#pragma once

#include <cstdint>

namespace drumboy_fx {

struct DelayState {
    static const uint32_t kDelayBufferSize = 96000U;

    bool active = false;
    uint8_t aTime = 2;      // Drumboy TIME index: kDelayTimeDataLibrary
    uint8_t bLevel = 10;    // Drumboy LEV index: kDelayLevelDataLibrary
    uint8_t cFeedback = 5;  // Drumboy FEED index: kDelayFeedbackDataLibrary
    uint8_t dDry = 20;      // Drumboy DRY index: kEffectMixDataLibrary
    uint8_t eWet = 10;      // Drumboy WET index: kEffectMixDataLibrary

    float time = 0.25f;
    float level = 0.50f;
    float feedback = 0.25f;
    float dryFloat = 1.0f;
    float wetFloat = 0.50f;

    uint32_t lag = 0;
    uint32_t playInterval = 0;
    uint32_t recordInterval = 0;
};

void delayReset(DelayState& delay, uint16_t tempo = 120, int32_t sampleRate = 44100);
void delayUpdate(DelayState& delay, uint16_t tempo, int32_t sampleRate = 44100);
int32_t delayProcessSample(DelayState& delay, int32_t input, int32_t* delayBuffer);

} // namespace drumboy_fx

