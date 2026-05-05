/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, Flanger::reset() / update() / cleanMemory().
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), branche EF_FLANGER.
 * Effet couvert: Flanger.
 * Dependances conservees: buffer `Effect::flangerBuffer`, feedback, modulation triangulaire.
 * Dependances non resolues: genTransition.activeRecordWet.
 * Statut: extraction legerement isolee, compilable seule.
 */

#include "flanger_dsp.h"
#include <cstring>

namespace drumboy_fx {

static const float kFlangerTimeData[] = {0.0010f, 0.0015f, 0.0020f, 0.0025f, 0.0030f, 0.0035f, 0.0040f, 0.0045f, 0.0050f};
static const float kFlangerFeedbackData[] = {
    0.00f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f, 0.45f,
    0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f};
static const float kFlangerRateData[] = {
    0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f, 0.70f, 0.80f, 0.90f, 1.00f,
    1.50f, 2.00f, 2.50f, 3.00f, 3.50f, 4.00f, 4.50f, 5.00f, 5.50f, 6.00f,
    6.50f, 7.00f, 7.50f, 8.00f, 8.50f, 9.00f};
static const float kEffectMixData[] = {
    0.00f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f, 0.45f,
    0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.00f};

void flangerReset(FlangerState& flanger, int32_t sampleRate) {
    flanger.active = false;
    flanger.aTime = 3;
    flanger.bFeedback = 5;
    flanger.cRate = 4;
    flanger.dDry = 15;
    flanger.eWet = 15;
    flanger.depth = 0.90f;
    std::memset(flanger.flangerBuffer, 0, sizeof(flanger.flangerBuffer));
    flangerUpdate(flanger, sampleRate);
    flangerCleanMemory(flanger, sampleRate);
}

void flangerUpdate(FlangerState& flanger, int32_t sampleRate) {
    flanger.time = kFlangerTimeData[flanger.aTime];
    flanger.feedback = kFlangerFeedbackData[flanger.bFeedback];
    flanger.rate = kFlangerRateData[flanger.cRate];
    flanger.dryFloat = kEffectMixData[flanger.dDry];
    flanger.wetFloat = kEffectMixData[flanger.eWet];
    flanger.lag = static_cast<uint16_t>(flanger.time * sampleRate);
    int32_t playInterval = static_cast<int32_t>(flanger.recordInterval) - flanger.lag;
    if (playInterval < 0) {
        playInterval += FlangerState::kFlangerBufferSize;
    }
    flanger.playInterval = static_cast<uint16_t>(playInterval);
    flanger.shiftFreq = flanger.rate * 2.0f;
    flanger.shiftMin = 0.0f;
    flanger.shiftMax = (flanger.depth * flanger.lag) - 2.0f;
    flanger.shiftInc = (flanger.shiftMax * flanger.shiftFreq) / sampleRate;
}

void flangerCleanMemory(FlangerState& flanger, int32_t sampleRate) {
    flanger.recordInterval = 0;
    flanger.playInterval = static_cast<uint16_t>((FlangerState::kFlangerBufferSize - 1U) - flanger.lag);
    flanger.shiftInterval = 0.0f;
    flanger.shiftInc = (flanger.shiftMax * flanger.shiftFreq) / sampleRate;
}

int32_t flangerProcessSample(FlangerState& flanger, int32_t input) {
    if (!flanger.active) {
        return input;
    }

    flanger.flangerInterval = flanger.playInterval + flanger.shiftInterval;
    if (flanger.flangerInterval < 0.0f) {
        flanger.flangerInterval += FlangerState::kFlangerBufferSize;
    } else if (flanger.flangerInterval > FlangerState::kFlangerBufferSize) {
        flanger.flangerInterval -= FlangerState::kFlangerBufferSize;
    }

    uint16_t intervalInt0 = static_cast<uint16_t>(flanger.flangerInterval);
    uint16_t intervalInt1 = intervalInt0 + 1U;
    float remainder = flanger.flangerInterval - intervalInt0;
    if (intervalInt1 == FlangerState::kFlangerBufferSize) {
        intervalInt1 = 0;
    }

    int32_t data0 = flanger.flangerBuffer[intervalInt0];
    int32_t data1 = flanger.flangerBuffer[intervalInt1];
    int32_t dataFlanger = static_cast<int32_t>(data0 + ((data1 - data0) * remainder));

    flanger.flangerBuffer[flanger.recordInterval] = static_cast<int32_t>(input + (dataFlanger * flanger.feedback));
    int32_t data = static_cast<int32_t>(
        (dataFlanger * flanger.wetFloat) +
        (flanger.flangerBuffer[flanger.recordInterval] * flanger.dryFloat));

    flanger.shiftInterval += flanger.shiftInc;
    if (flanger.shiftInterval <= flanger.shiftMin) {
        flanger.shiftInc *= -1.0f;
        flanger.shiftInterval = flanger.shiftMin;
    }
    if (flanger.shiftInterval >= flanger.shiftMax) {
        flanger.shiftInc *= -1.0f;
        flanger.shiftInterval = flanger.shiftMax;
    }

    if (++flanger.recordInterval > (FlangerState::kFlangerBufferSize - 1U)) {
        flanger.recordInterval = 0;
    }
    if (++flanger.playInterval > (FlangerState::kFlangerBufferSize - 1U)) {
        flanger.playInterval = 0;
    }
    return data;
}

} // namespace drumboy_fx

