/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, sections
 *   "Effect-Compressor Constants" et "Effect-Expander Constants".
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), branches EF_COMPRESSOR et EF_EXPANDER.
 * Effets couverts: Compressor, Expander.
 * Dependances conservees: detection dB, threshold, ratio/rate, attack/release smoothing,
 *   mix wet/dry.
 * Dependances non resolues: transitions Drumboy EffectGenTransition.
 * Statut: extraction legerement isolee, compilable seule.
 */

#pragma once

#include <cstdint>

namespace drumboy_fx {

struct CompressorState {
    bool active = false;
    uint8_t aThreshold = 18;
    uint8_t bRate = 3;
    uint8_t cAttackTime = 0;
    uint8_t dReleaseTime = 9;
    uint8_t eMix = 20;
    float threshold = -6.0f;
    float rate = 4.0f;
    float attackTime = 0.001f;
    float releaseTime = 0.100f;
    float attackAlpha = 0.0f;
    float releaseAlpha = 0.0f;
    float gainSmoothPrev = 0.0f;
    float dryFloat = 0.0f;
    float wetFloat = 1.0f;
};

struct ExpanderState {
    bool active = false;
    uint8_t aThreshold = 12;
    uint8_t bRate = 3;
    uint8_t cAttackTime = 0;
    uint8_t dReleaseTime = 13;
    uint8_t eMix = 20;
    float threshold = -12.0f;
    float rate = 4.0f;
    float attackTime = 0.001f;
    float releaseTime = 0.500f;
    float attackAlpha = 0.0f;
    float releaseAlpha = 0.0f;
    float gainSmoothPrev = -144.0f;
    float dryFloat = 0.0f;
    float wetFloat = 1.0f;
};

void compressorReset(CompressorState& compressor, int32_t sampleRate = 44100);
void compressorUpdate(CompressorState& compressor, int32_t sampleRate = 44100);
int32_t compressorProcessSample(CompressorState& compressor, int32_t input, int32_t int24Max = 8388607);

void expanderReset(ExpanderState& expander, int32_t sampleRate = 44100);
void expanderUpdate(ExpanderState& expander, int32_t sampleRate = 44100);
int32_t expanderProcessSample(ExpanderState& expander, int32_t input, int32_t int24Max = 8388607);

} // namespace drumboy_fx

