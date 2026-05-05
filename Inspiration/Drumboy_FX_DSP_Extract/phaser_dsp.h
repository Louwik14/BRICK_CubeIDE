/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, section "Effect-Phaser Constants".
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), branche EF_PHASER.
 * Effet couvert: Phaser.
 * Dependances conservees: LFO sinus, calcul coefficients par sample, etats ff/fb,
 *   dry/wet.
 * Dependances non resolues: transitions Drumboy.
 * Statut: extraction legerement isolee, compilable seule.
 */

#pragma once

#include <cstdint>

namespace drumboy_fx {

struct PhaserState {
    bool active = false;
    uint8_t aStartFreq = 27;
    uint8_t bEndFreq = 31;
    uint8_t cRate = 4;
    uint8_t dDry = 15;
    uint8_t eWet = 15;
    uint16_t startFreq = 1000;
    uint16_t endFreq = 4000;
    float rate = 0.50f;
    float dryFloat = 0.75f;
    float wetFloat = 0.75f;
    float centerFreq = 0.0f;
    float depthFreq = 0.0f;
    float lfo = 0.0f;
    float dataX = 0.0f;
    float dataY = 0.0f;
    float Ts = 1.0f / 44100.0f;
    float ff[2] = {0.0f, 0.0f};
    float fb[2] = {0.0f, 0.0f};
    float Q = 0.5f;
};

void phaserReset(PhaserState& phaser, int32_t sampleRate = 44100);
void phaserUpdate(PhaserState& phaser);
int32_t phaserProcessSample(PhaserState& phaser, int32_t input, int32_t sampleRate = 44100);

} // namespace drumboy_fx

