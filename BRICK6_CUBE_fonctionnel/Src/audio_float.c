/* ============================================================
   audio_float.c
   STM32H743 + CS42448 TDM8
   Track-based float boundary engine
   ============================================================ */

#include "audio_float.h"
#include <stdint.h>
#include <string.h>

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
   TRACK + MIX STATE
   ============================================================ */

static StereoTrack tracks[MAX_TRACKS];
static float track_gain[MAX_TRACKS] = {1.0f, 1.0f, 1.0f};
static float master_gain = 1.0f;

/* ============================================================
   USER CALLBACK
   ============================================================ */

static audio_dsp_cb float_cb = 0;

void audio_set_float_callback(audio_dsp_cb cb)
{
    float_cb = cb;
}

void audio_tracks_init(void)
{
    for(uint32_t t = 0; t < MAX_TRACKS; t++)
    {
        tracks[t].enabled = 0U;
        track_gain[t] = 1.0f;
        memset(tracks[t].L, 0, sizeof(tracks[t].L));
        memset(tracks[t].R, 0, sizeof(tracks[t].R));
    }

    master_gain = 1.0f;
}

void track_enable(uint32_t track_id, uint8_t enabled)
{
    if(track_id >= MAX_TRACKS)
        return;

    tracks[track_id].enabled = enabled ? 1U : 0U;
}

uint32_t track_is_enabled(uint32_t track_id)
{
    if(track_id >= MAX_TRACKS)
        return 0U;

    return (uint32_t)tracks[track_id].enabled;
}

void track_set_gain(uint32_t track_id, float gain)
{
    if(track_id >= MAX_TRACKS)
        return;

    if(gain < 0.0f)
        gain = 0.0f;

    track_gain[track_id] = gain;
}

void audio_float_set_master_gain(float gain)
{
    if(gain < 0.0f)
        gain = 0.0f;
    if(gain > 2.0f)
        gain = 2.0f;

    master_gain = gain;
}

float audio_float_get_master_gain(void)
{
    return master_gain;
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

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

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
        else
        {
            memset(tracks[t].L, 0, frames * sizeof(float));
            memset(tracks[t].R, 0, frames * sizeof(float));
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

        master_l[n] = sum_l * master_gain;
        master_r[n] = sum_r * master_gain;
    }

    for(uint32_t n = 0; n < frames; n++)
    {
        const float out_l = master_l[n] * output_adjust;
        const float out_r = master_r[n] * output_adjust;
        const uint32_t base = n * AUDIO_TDM_SLOTS;

        tx[base + 0U] = f2s24(out_l); /* MAIN L */
        tx[base + 1U] = f2s24(out_r); /* MAIN R */
        tx[base + 2U] = f2s24(out_l); /* CUE L */
        tx[base + 3U] = f2s24(out_r); /* CUE R */
        tx[base + 4U] = 0;
        tx[base + 5U] = 0;
        tx[base + 6U] = 0;
        tx[base + 7U] = 0;
    }
}
