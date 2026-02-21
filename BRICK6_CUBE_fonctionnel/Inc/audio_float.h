#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_BLOCK_SIZE 32U
#define MAX_TRACKS       3U

typedef struct
{
    float L[AUDIO_BLOCK_SIZE];
    float R[AUDIO_BLOCK_SIZE];
    uint8_t enabled;
    float gain;
} StereoTrack;

/* Track-based DSP callback */
typedef void (*audio_dsp_cb)(StereoTrack *tracks,
                             uint32_t track_count,
                             uint32_t frames);

/* Install user DSP callback */
void audio_set_dsp_callback(audio_dsp_cb cb);

/* Optional helpers */
void track_enable(uint32_t track_id, uint8_t enable);
void track_set_gain(uint32_t track_id, float gain);

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
