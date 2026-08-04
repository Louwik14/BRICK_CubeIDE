/**
 * @file brick6_audio_runtime.h
 * @brief Audio DSP runtime callback API.
 *
 * Rôle du module:
 * - Exposer le callback DSP runtime et son init de contexte.
 *
 * Frontière:
 * - Ne gère pas l'init hardware/audio globale.
 * - Ne porte pas la superloop applicative.
 */

#pragma once

#include <stdint.h>

#include "audio_float.h"

#ifdef __cplusplus
extern "C" {
#endif

void brick6_audio_runtime_init(void);

/* Serialize main-loop diagnostic mutations against the audio IRQ. */
void brick6_audio_runtime_set_diagnostic_hold(uint8_t hold);

void brick6_audio_runtime_dsp(StereoTrack *tracks,
                              uint32_t track_count,
                              uint32_t frames);

#ifdef __cplusplus
}
#endif
