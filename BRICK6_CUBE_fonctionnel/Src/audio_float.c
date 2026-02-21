/* ============================================================
   audio_float.c
   STM32H743 + CS42448 TDM8
   Track-based Float Boundary Engine
   ============================================================ */

#include "audio_float.h"
#include "mixer.h"
#include <stdint.h>

#define AUDIO_TDM_SLOTS 8U
#define MAIN_L_SLOT     0U
#define MAIN_R_SLOT     1U
#define CUE_L_SLOT      2U
#define CUE_R_SLOT      3U

static const uint8_t k_track_slots[MAX_TRACKS][2] = {
    {0U, 1U},
    {2U, 3U},
    {4U, 5U},
};

/* Daisy-style gain staging */
static float postgain_recip = 1.0f;
static float output_adjust  = 1.0f;
static float postgain       = 1.0f;
static float output_comp    = 1.0f;

/* Track buffers/state */
static StereoTrack tracks[MAX_TRACKS];

/* Master bus */
static float masterL[AUDIO_BLOCK_SIZE];
static float masterR[AUDIO_BLOCK_SIZE];

/* User callback */
static audio_dsp_cb dsp_cb = 0;

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

void audio_set_dsp_callback(audio_dsp_cb cb)
{
    dsp_cb = cb;
}

void track_enable(uint32_t track_id, uint8_t enable)
{
    if(track_id >= MAX_TRACKS)
        return;

    tracks[track_id].enabled = (enable != 0U) ? 1U : 0U;
}

void track_set_gain(uint32_t track_id, float gain)
{
    if(track_id >= MAX_TRACKS)
        return;

    if(gain < 0.0f)
        gain = 0.0f;

    tracks[track_id].gain = gain;
}

static inline float s242f(int32_t x)
{
    if(x & 0x00800000)
        x |= 0xFF000000;

    return (float)x * (1.0f / 8388608.0f);
}

static inline int32_t f2s24(float x)
{
    if(x > 0.999999f)
        x = 0.999999f;
    if(x < -1.0f)
        x = -1.0f;

    return ((int32_t)(x * 8388607.0f)) & 0x00FFFFFF;
}

static void track_state_init_once(void)
{
    static uint8_t initialized = 0U;
    if(initialized)
        return;

    for(uint32_t t = 0; t < MAX_TRACKS; t++)
    {
        tracks[t].enabled = 1U;
        tracks[t].gain = 1.0f;
    }

    initialized = 1U;
}

void audio_process_block_int32(int32_t *rx,
                               int32_t *tx,
                               uint32_t frames)
{
    track_state_init_once();

    /* 1) Unpack only active tracks */
    for(uint32_t n = 0; n < frames; n++)
    {
        const uint32_t frame = n * AUDIO_TDM_SLOTS;
        for(uint32_t t = 0; t < MAX_TRACKS; t++)
        {
            if(tracks[t].enabled)
            {
                const uint8_t l_slot = k_track_slots[t][0];
                const uint8_t r_slot = k_track_slots[t][1];
                const float gain = tracks[t].gain;

                tracks[t].L[n] = s242f(rx[frame + l_slot]) * postgain_recip * gain;
                tracks[t].R[n] = s242f(rx[frame + r_slot]) * postgain_recip * gain;
            }
        }
    }

    /* 2) User DSP on track arrays */
    if(dsp_cb)
    {
        dsp_cb(tracks, MAX_TRACKS, frames);
    }

    /* 3) Minimal mixer: sum all active tracks */
    for(uint32_t n = 0; n < frames; n++)
    {
        float sumL = 0.0f;
        float sumR = 0.0f;

        for(uint32_t t = 0; t < MAX_TRACKS; t++)
        {
            if(tracks[t].enabled)
            {
                sumL += tracks[t].L[n];
                sumR += tracks[t].R[n];
            }
        }

        const float master = mixer_get_master();
        masterL[n] = sumL * master;
        masterR[n] = sumR * master;
    }

    /* 4) Pack only required outputs */
    for(uint32_t n = 0; n < frames; n++)
    {
        const uint32_t frame = n * AUDIO_TDM_SLOTS;
        tx[frame + MAIN_L_SLOT] = f2s24(masterL[n] * output_adjust);
        tx[frame + MAIN_R_SLOT] = f2s24(masterR[n] * output_adjust);
        tx[frame + CUE_L_SLOT] = f2s24(masterL[n] * output_adjust);
        tx[frame + CUE_R_SLOT] = f2s24(masterR[n] * output_adjust);

        tx[frame + 4U] = 0;
        tx[frame + 5U] = 0;
        tx[frame + 6U] = 0;
        tx[frame + 7U] = 0;
    }
}
