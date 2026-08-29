/**
 * @file audio_engine_dispatch.h
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
uint8_t brick6_audio_runtime_set_input_owner(uint8_t input, uint8_t owner);

void brick6_audio_runtime_dsp(StereoTrack *tracks,
                              uint32_t track_count,
                              uint32_t frames);

#ifdef __cplusplus
}
#endif
