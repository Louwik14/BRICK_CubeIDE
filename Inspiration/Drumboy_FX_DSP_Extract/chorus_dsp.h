/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, section "Effect-Chorus Constants".
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), branche EF_CHORUS.
 * Effet couvert: Chorus.
 * Dependances conservees: deux lectures modulees, interpolation lineaire, feedback,
 *   dry/wet, buffer circulaire.
 * Dependances non resolues: transitions Drumboy et adresses SDRAM RAM_CHORUS_*.
 * Statut: extraction legerement isolee, compilable seule si appelee avec un buffer valide.
 */

#pragma once

#include <cstdint>

namespace drumboy_fx {

struct ChorusDelayState {
    float time = 0.010f;
    float depth = 0.80f;
    float rate = 0.50f;
    float mix = 0.50f;
    uint16_t lag = 0;
    uint16_t playInterval = 0;
    float chorusInterval = 0.0f;
    float shiftFreq = 0.0f;
    float shiftMax = 0.0f;
    float shiftMin = 0.0f;
    float shiftInterval = 0.0f;
    float shiftInc = 0.0f;
};

struct ChorusState {
    static const uint16_t kChorusBufferSize = 5000U;

    bool active = false;
    uint8_t aTime = 0;
    uint8_t bFeedback = 5;
    uint8_t cRate = 4;
    uint8_t dDry = 15;
    uint8_t eWet = 15;
    float time = 0.010f;
    float feedback = 0.25f;
    float rate = 0.50f;
    float dryFloat = 0.75f;
    float wetFloat = 0.75f;
    float depth = 0.80f;
    uint16_t recordInterval = 0;
    ChorusDelayState chorusDelay[2];
};

void chorusReset(ChorusState& chorus, int32_t sampleRate = 44100);
void chorusUpdate(ChorusState& chorus, int32_t sampleRate = 44100);
int32_t chorusProcessSample(ChorusState& chorus, int32_t input, int32_t* chorusBuffer);

} // namespace drumboy_fx

