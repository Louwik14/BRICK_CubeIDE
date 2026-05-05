/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, Chorus::reset() / Chorus::update().
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), branche EF_CHORUS.
 * Effet couvert: Chorus.
 * Dependances conservees: delayCoef[2] = {1, 0.75}, modulation triangulaire,
 *   interpolation lineaire et feedback.
 * Dependances non resolues: genTransition.activeRecordWet et SDRAM mappee.
 * Statut: extraction legerement isolee, compilable seule.
 */

#include "chorus_dsp.h"

namespace drumboy_fx {

static const float kChorusTimeData[] = {0.010f, 0.015f, 0.020f, 0.025f, 0.030f, 0.035f, 0.040f, 0.045f, 0.050f};
static const float kChorusFeedbackData[] = {
    0.00f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f, 0.45f,
    0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f};
static const float kChorusRateData[] = {
    0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f, 0.70f, 0.80f, 0.90f, 1.00f,
    1.50f, 2.00f, 2.50f, 3.00f, 3.50f, 4.00f, 4.50f, 5.00f, 5.50f, 6.00f,
    6.50f, 7.00f, 7.50f, 8.00f, 8.50f, 9.00f};
static const float kEffectMixData[] = {
    0.00f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f, 0.45f,
    0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.00f};
static const float kDelayCoef[2] = {1.0f, 0.75f};

void chorusReset(ChorusState& chorus, int32_t sampleRate) {
    chorus.active = false;
    chorus.aTime = 0;
    chorus.bFeedback = 5;
    chorus.cRate = 4;
    chorus.dDry = 15;
    chorus.eWet = 15;
    chorus.depth = 0.80f;
    chorus.recordInterval = 0;
    chorusUpdate(chorus, sampleRate);
}

void chorusUpdate(ChorusState& chorus, int32_t sampleRate) {
    chorus.time = kChorusTimeData[chorus.aTime];
    chorus.feedback = kChorusFeedbackData[chorus.bFeedback];
    chorus.rate = kChorusRateData[chorus.cRate];
    chorus.dryFloat = kEffectMixData[chorus.dDry];
    chorus.wetFloat = kEffectMixData[chorus.eWet];

    for (uint8_t i = 0; i < 2; i++) {
        ChorusDelayState& cD = chorus.chorusDelay[i];
        cD.time = chorus.time * kDelayCoef[i];
        cD.depth = chorus.depth * kDelayCoef[i];
        cD.rate = chorus.rate * kDelayCoef[i];
        cD.mix = 0.50f;
        cD.lag = static_cast<uint16_t>(cD.time * sampleRate);
        int32_t playInterval = static_cast<int32_t>(chorus.recordInterval) - cD.lag;
        if (playInterval < 0) {
            playInterval += ChorusState::kChorusBufferSize;
        }
        cD.playInterval = static_cast<uint16_t>(playInterval);
        cD.shiftFreq = cD.rate * 2.0f;
        cD.shiftMax = (cD.depth * cD.lag) - 2.0f;
        cD.shiftMin = -cD.shiftMax;
        cD.shiftInterval = cD.shiftMin;
        cD.shiftInc = ((cD.shiftMax - cD.shiftMin) * cD.shiftFreq) / sampleRate;
    }
}

int32_t chorusProcessSample(ChorusState& chorus, int32_t input, int32_t* chorusBuffer) {
    if (!chorus.active || chorusBuffer == nullptr) {
        return input;
    }

    int32_t dataChorusDelay[2] = {0, 0};
    for (uint8_t i = 0; i < 2; i++) {
        ChorusDelayState& cD = chorus.chorusDelay[i];
        cD.chorusInterval = cD.playInterval + cD.shiftInterval;
        if (cD.chorusInterval < 0.0f) {
            cD.chorusInterval += ChorusState::kChorusBufferSize;
        } else if (cD.chorusInterval > ChorusState::kChorusBufferSize) {
            cD.chorusInterval -= ChorusState::kChorusBufferSize;
        }

        uint16_t intervalInt0 = static_cast<uint16_t>(cD.chorusInterval);
        uint16_t intervalInt1 = intervalInt0 + 1U;
        float remainder = cD.chorusInterval - intervalInt0;
        if (intervalInt1 == ChorusState::kChorusBufferSize) {
            intervalInt1 = 0;
        }

        int32_t data0 = chorusBuffer[intervalInt0];
        int32_t data1 = chorusBuffer[intervalInt1];
        dataChorusDelay[i] = static_cast<int32_t>(data0 + ((data1 - data0) * remainder));

        cD.shiftInterval += cD.shiftInc;
        if (cD.shiftInterval <= cD.shiftMin) {
            cD.shiftInc *= -1.0f;
            cD.shiftInterval = cD.shiftMin;
        }
        if (cD.shiftInterval >= cD.shiftMax) {
            cD.shiftInc *= -1.0f;
            cD.shiftInterval = cD.shiftMax;
        }
    }

    int32_t dataChorus = static_cast<int32_t>(
        (dataChorusDelay[0] * chorus.chorusDelay[0].mix) +
        (dataChorusDelay[1] * chorus.chorusDelay[1].mix));
    chorusBuffer[chorus.recordInterval] = static_cast<int32_t>(input + (dataChorus * chorus.feedback));
    int32_t data = static_cast<int32_t>((dataChorus * chorus.wetFloat) + (chorusBuffer[chorus.recordInterval] * chorus.dryFloat));

    if (++chorus.recordInterval > (ChorusState::kChorusBufferSize - 1U)) {
        chorus.recordInterval = 0;
    }
    for (uint8_t i = 0; i < 2; i++) {
        if (++chorus.chorusDelay[i].playInterval > (ChorusState::kChorusBufferSize - 1U)) {
            chorus.chorusDelay[i].playInterval = 0;
        }
    }
    return data;
}

} // namespace drumboy_fx

