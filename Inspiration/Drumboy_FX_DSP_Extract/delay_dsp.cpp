/*
 * Provenance exacte:
 * - Drumboy original: Core/Library/Global/Global.h, Delay::reset() / Delay::update().
 * - Drumboy original: Core/Library/Controller/Controller.cpp,
 *   Controller::processAudioEffect(...), lignes de la branche EF_DELAY.
 * Effet couvert: Delay.
 * Dependances conservees: calcul de lag depuis tempo, lecture/ecriture circulaire,
 *   feedback, dry/wet.
 * Dependances non resolues: genTransition.active, activeRecordWet, memoire SDRAM mappee.
 * Statut: extraction legerement isolee, compilable seule.
 */

#include "delay_dsp.h"

namespace drumboy_fx {

static const float kDelayTimeData[] = {0.1250f, 0.1666f, 0.2500f, 0.3333f, 0.5000f, 1.0000f};
static const float kDelayLevelData[] = {
    0.00f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f, 0.45f,
    0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.00f};
static const float kEffectMixData[] = {
    0.00f, 0.05f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.35f, 0.40f, 0.45f,
    0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f, 0.90f, 0.95f, 1.00f};

void delayReset(DelayState& delay, uint16_t tempo, int32_t sampleRate) {
    delay.active = false;
    delay.aTime = 2;
    delay.bLevel = 10;
    delay.cFeedback = 5;
    delay.dDry = 20;
    delay.eWet = 10;
    delay.time = kDelayTimeData[delay.aTime];
    delay.level = kDelayLevelData[delay.bLevel];
    delay.feedback = kDelayLevelData[delay.cFeedback];
    delay.dryFloat = kEffectMixData[delay.dDry];
    delay.wetFloat = kEffectMixData[delay.eWet];
    delay.lag = static_cast<uint32_t>(((60 * sampleRate) / tempo) * delay.time);
    delay.playInterval = 0;
    delay.recordInterval = delay.lag;
}

void delayUpdate(DelayState& delay, uint16_t tempo, int32_t sampleRate) {
    delay.time = kDelayTimeData[delay.aTime];
    delay.level = kDelayLevelData[delay.bLevel];
    delay.feedback = kDelayLevelData[delay.cFeedback];
    delay.dryFloat = kEffectMixData[delay.dDry];
    delay.wetFloat = kEffectMixData[delay.eWet];
    delay.lag = static_cast<uint32_t>(((60 * sampleRate) / tempo) * delay.time);
    int32_t playInterval = static_cast<int32_t>(delay.recordInterval) - static_cast<int32_t>(delay.lag);
    if (playInterval < 0) {
        playInterval += static_cast<int32_t>(DelayState::kDelayBufferSize);
    }
    delay.playInterval = static_cast<uint32_t>(playInterval);
}

int32_t delayProcessSample(DelayState& delay, int32_t input, int32_t* delayBuffer) {
    if (!delay.active || delayBuffer == nullptr) {
        return input;
    }

    int32_t playData = input + delayBuffer[delay.playInterval];
    delayBuffer[delay.recordInterval] =
        static_cast<int32_t>((input * delay.level) + (delayBuffer[delay.playInterval] * delay.feedback));
    int32_t data = static_cast<int32_t>((playData * delay.wetFloat) + (input * delay.dryFloat));

    if (++delay.playInterval > (DelayState::kDelayBufferSize - 1U)) {
        delay.playInterval = 0;
    }
    if (++delay.recordInterval > (DelayState::kDelayBufferSize - 1U)) {
        delay.recordInterval = 0;
    }
    return data;
}

} // namespace drumboy_fx

