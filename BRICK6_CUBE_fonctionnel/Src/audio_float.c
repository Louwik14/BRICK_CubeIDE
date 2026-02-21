/* ============================================================
   audio_float.c
   STM32H743 + CS42448 TDM8
   Track-based float boundary engine
   ============================================================ */

#include "audio_float.h"
#include <stdint.h>

/* ============================================================
   CONFIG
   ============================================================ */

#define AUDIO_TDM_SLOTS 8U

/* ============================================================
   Daisy-style gain staging
   ============================================================ */
static float postgain_recip = 1.0f; /* = 1/postgain */
static float output_adjust = 1.0f;  /* postgain * output_comp */

static float postgain = 1.0f;
static float output_comp = 1.0f;

void audio_float_set_postgain(float gain)
{
    if(gain <= 0.0f)
        gain = 1.0f;

    postgain = gain;
    postgain_recip = 1.0f / postgain;

    output_adjust = postgain * output_comp;
}

void audio_float_set_output_compensation(float comp)
{
    output_comp = comp;
    output_adjust = postgain * output_comp;
}

/* ============================================================
   TRACK STATE
   ============================================================ */

static StereoTrack tracks[MAX_TRACKS];
static float track_gain[MAX_TRACKS] = {1.0f, 1.0f, 1.0f};

/* ============================================================
   USER CALLBACK
   ============================================================ */

static audio_dsp_cb float_cb = 0;

void audio_set_float_callback(audio_dsp_cb cb)
{
    float_cb = cb;
}

void track_enable(uint32_t track_id, uint8_t enabled)
{
    if(track_id >= MAX_TRACKS)
        return;

    tracks[track_id].enabled = enabled ? 1U : 0U;
}

void track_set_gain(uint32_t track_id, float gain)
{
    if(track_id >= MAX_TRACKS)
        return;

    if(gain < 0.0f)
        gain = 0.0f;

    track_gain[track_id] = gain;
}

/* ============================================================
   CONVERSION HELPERS : CS42448 + STM32H7 SAI (TDM8, 24-bit)
   ============================================================ */

static inline float s242f(int32_t x)
{
    if(x & 0x00800000)
        x |= 0xFF000000;

    return (float)x * (1.0f / 8388608.0f); /* 2^23 */
}

static inline int32_t f2s24(float x)
{
    if(x > 0.999999f)
        x = 0.999999f;
    if(x < -1.0f)
        x = -1.0f;

    return ((int32_t)(x * 8388607.0f)) & 0x00FFFFFF;
}

/* ============================================================
   MAIN DSP BLOCK PROCESSOR
   Called by audio.c IRQ layer
   ============================================================ */

void audio_process_block_int32(int32_t *rx, int32_t *tx, uint32_t frames)
{
    static float master_l[AUDIO_BLOCK_SIZE];
    static float master_r[AUDIO_BLOCK_SIZE];

    for(uint32_t t = 0; t < MAX_TRACKS; t++)
    {
        if(tracks[t].enabled)
        {
            const uint32_t slot_l = t * 2U;
            const uint32_t slot_r = slot_l + 1U;

            for(uint32_t n = 0; n < frames; n++)
            {
                const uint32_t base = n * AUDIO_TDM_SLOTS;
                tracks[t].L[n] = s242f(rx[base + slot_l]) * postgain_recip;
                tracks[t].R[n] = s242f(rx[base + slot_r]) * postgain_recip;
            }
        }
    }

    if(float_cb)
        float_cb(tracks, MAX_TRACKS, frames);

    for(uint32_t n = 0; n < frames; n++)
    {
        float sum_l = 0.0f;
        float sum_r = 0.0f;

        for(uint32_t t = 0; t < MAX_TRACKS; t++)
        {
            if(tracks[t].enabled)
            {
                sum_l += tracks[t].L[n] * track_gain[t];
                sum_r += tracks[t].R[n] * track_gain[t];
            }
        }

        master_l[n] = sum_l * output_adjust;
        master_r[n] = sum_r * output_adjust;
    }

    for(uint32_t n = 0; n < frames; n++)
    {
        const uint32_t base = n * AUDIO_TDM_SLOTS;

        tx[base + 0U] = f2s24(master_l[n]); /* MAIN L */
        tx[base + 1U] = f2s24(master_r[n]); /* MAIN R */
        tx[base + 2U] = f2s24(master_l[n]); /* CUE L */
        tx[base + 3U] = f2s24(master_r[n]); /* CUE R */
        tx[base + 4U] = 0;
        tx[base + 5U] = 0;
        tx[base + 6U] = 0;
        tx[base + 7U] = 0;
    }
}
