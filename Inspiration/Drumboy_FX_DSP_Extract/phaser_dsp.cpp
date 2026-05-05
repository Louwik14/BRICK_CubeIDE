/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, Phaser::reset() / update().
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), branche EF_PHASER.
 * Effet couvert: Phaser.
 * Dependances conservees: formules exactes de la branche EF_PHASER.
 * Dependances non resolues: genTransition.active.
 * Statut: extraction legerement isolee, compilable seule.
 */

#include "phaser_dsp.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace drumboy_fx {

static const uint16_t kPhaserFreqData[] = {
    10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 150, 200, 250, 300, 350, 400,
    450, 500, 550, 600, 650, 700, 750, 800, 850, 900, 950, 1000, 2000, 3000,
    4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 13000, 14000,
    15000, 16000, 17000, 18000, 19000, 20000};
static const float kPhaserRateData[] = {
    0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f, 0.70f, 0.80f, 0.90f, 1.00f,
    1.50f, 2.00f, 2.50f, 3.00f, 3.50f, 4.00f, 4.50f, 5.00f, 5.50f, 6.00f,
    6.50f, 7.00f, 7.50f, 8.00f, 8.50f, 9.00f};
static const float kEffectMixData[] = {
    0.00f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f, 0.45f,
    0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.00f};

void phaserReset(PhaserState& phaser, int32_t sampleRate) {
    phaser.active = false;
    phaser.aStartFreq = 27;
    phaser.bEndFreq = 31;
    phaser.cRate = 4;
    phaser.dDry = 15;
    phaser.eWet = 15;
    phaser.startFreq = kPhaserFreqData[phaser.aStartFreq];
    phaser.endFreq = kPhaserFreqData[phaser.bEndFreq];
    phaser.rate = kPhaserRateData[phaser.cRate];
    phaser.dryFloat = kEffectMixData[phaser.dDry];
    phaser.wetFloat = kEffectMixData[phaser.eWet];
    phaser.lfo = 0.0f;
    phaser.dataX = 0.0f;
    phaser.dataY = 0.0f;
    phaser.Ts = 1.0f / sampleRate;
    phaser.ff[0] = phaser.ff[1] = 0.0f;
    phaser.fb[0] = phaser.fb[1] = 0.0f;
    phaserUpdate(phaser);
}

void phaserUpdate(PhaserState& phaser) {
    phaser.startFreq = kPhaserFreqData[phaser.aStartFreq];
    phaser.endFreq = kPhaserFreqData[phaser.bEndFreq];
    phaser.rate = kPhaserRateData[phaser.cRate];
    phaser.dryFloat = kEffectMixData[phaser.dDry];
    phaser.wetFloat = kEffectMixData[phaser.eWet];
    phaser.centerFreq = phaser.startFreq + ((phaser.endFreq - phaser.startFreq) / 2.0f);
    phaser.depthFreq = (phaser.endFreq - phaser.centerFreq) * 0.9f;
}

int32_t phaserProcessSample(PhaserState& phaser, int32_t input, int32_t sampleRate) {
    if (!phaser.active) {
        return input;
    }

    phaser.lfo = phaser.depthFreq * std::sin(2.0 * M_PI * phaser.dataY) + phaser.centerFreq;
    phaser.dataX += phaser.Ts;
    phaser.dataY = phaser.rate * phaser.dataX;
    if (phaser.dataY >= 1.0f) {
        phaser.dataX = 0.0f;
    }

    float w0 = static_cast<float>(2.0 * M_PI * phaser.lfo / sampleRate);
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * phaser.Q);

    float b0 = 1.0f - alpha;
    float b1 = -2.0f * cosw0;
    float b2 = 1.0f + alpha;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha;

    int32_t dataPhaser = static_cast<int32_t>(
        ((b0 / a0) * input) + ((b1 / a0) * phaser.ff[0]) + ((b2 / a0) * phaser.ff[1]) -
        ((a1 / a0) * phaser.fb[0]) - ((a2 / a0) * phaser.fb[1]));
    int32_t data = static_cast<int32_t>((dataPhaser * phaser.wetFloat) + (input * phaser.dryFloat));

    phaser.ff[1] = phaser.ff[0];
    phaser.ff[0] = static_cast<float>(input);
    phaser.fb[1] = phaser.fb[0];
    phaser.fb[0] = static_cast<float>(dataPhaser);
    return data;
}

} // namespace drumboy_fx

