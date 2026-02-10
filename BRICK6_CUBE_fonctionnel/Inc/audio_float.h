#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
   Float DSP Callback (Mutable/Daisy style)
   ============================================================ */

typedef void (*audio_float_cb)(float **in,
                              float **out,
                              uint32_t frames);

/* Install user DSP callback */
void audio_set_float_callback(audio_float_cb cb);

/* ============================================================
   Gain staging (Daisy-style)
   ============================================================ */

/* Postgain affects input scaling (ADC level into DSP) */
void audio_float_set_postgain(float gain);

/* Output compensation affects float->DAC scaling */
void audio_float_set_output_compensation(float comp);

/* ============================================================
   Engine entry point called by audio.c
   ============================================================ */

void audio_process_block_int32(int32_t *rx,
                               int32_t *tx,
                               uint32_t frames);

#ifdef __cplusplus
}
#endif
