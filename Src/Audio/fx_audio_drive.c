#include "Audio/fx_audio_drive.h"

#include <math.h>
#include <stddef.h>

static inline float daisy_soft_clip(float x)
{
    if (x < -3.0f) return -1.0f;
    if (x > 3.0f) return 1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + (9.0f * x2));
}

static inline float drive_channel(const fx_audio_drive_state_t *state, float x)
{
    const float driven = daisy_soft_clip(
        (x * state->input_gain) * state->pre_gain) * state->post_gain;
    return driven * state->level_gain;
}

static float db_gain(float db)
{
    return (db == 0.0f) ? 1.0f : powf(10.0f, db * 0.05f);
}

void fx_audio_drive_reset(fx_audio_drive_state_t *state)
{
    if (state == NULL) return;
    *state = (fx_audio_drive_state_t){
        .pre_gain = 0.0f,
        .post_gain = 1.0f / daisy_soft_clip(0.33f),
        .input_gain = 1.0f,
        .level_gain = 1.0f
    };
}

void fx_audio_drive_prepare(fx_audio_drive_state_t *state,
                            float drive,
                            float input,
                            float level)
{
    if (state == NULL) return;
    /* Daisy's inclusive drive=1 endpoint makes drive_native=2 and collapses
       drive_squashed to zero.  Keep the 7-bit maximum on the upper half-open
       grid so it remains above step 126 without entering that discontinuity. */
    const float drive_max = 127.0f / 128.0f;
    const float d = (drive <= 0.0f) ? 0.0f
        : (drive >= drive_max) ? drive_max : drive;
    const float in = (input <= 0.0f) ? 0.0f
        : (input >= 1.0f) ? 1.0f : input;
    const float lev = (level <= 0.0f) ? 0.0f
        : (level >= 127.0f) ? 127.0f : level;
    const float drive_native = 2.0f * d;
    const float drive2 = drive_native * drive_native;
    const float pre_gain_a = drive_native * 0.5f;
    const float pre_gain_b = drive2 * drive2 * drive_native * 24.0f;
    const float drive_squashed = drive_native * (2.0f - drive_native);
    const float input_db = (in * 24.0f) - 12.0f;
    const float level_db = (lev <= 64.0f)
        ? -12.0f + (lev * (12.0f / 64.0f))
        : (lev - 64.0f) * (6.0f / 63.0f);

    state->pre_gain = pre_gain_a
        + ((pre_gain_b - pre_gain_a) * drive2);
    state->post_gain = 1.0f / daisy_soft_clip(
        0.33f + drive_squashed * (state->pre_gain - 0.33f));
    state->input_gain = db_gain(input_db);
    state->level_gain = db_gain(level_db);
}

float fx_audio_drive_process_sample(const fx_audio_drive_state_t *state,
                                    float sample)
{
    return (state != NULL) ? drive_channel(state, sample) : sample;
}

void fx_audio_drive_process_stereo(fx_audio_drive_state_t *state,
                                   float *left,
                                   float *right,
                                   uint32_t frames)
{
    if ((state == NULL) || (left == NULL) || (right == NULL)) return;
    const fx_audio_drive_state_t hot = *state;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        left[i] = drive_channel(&hot, left[i]);
        right[i] = drive_channel(&hot, right[i]);
    }
}
