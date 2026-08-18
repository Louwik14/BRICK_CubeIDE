#include "Audio/fx_audio_point.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static float clamp01(float value)
{
    if (value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static float point_process_channel(fx_audio_point_channel_t *channel,
                                   uint8_t flip,
                                   float sample,
                                   float gain_trim,
                                   float nib_attack,
                                   float nob_attack,
                                   float nib_decay,
                                   float nob_decay)
{
    sample *= gain_trim;
    const float absolute = fabsf(sample);
    float factor = 0.0f;
    float *const nib = (flip != 0U) ? &channel->nib_a : &channel->nib_b;
    float *const nob = (flip != 0U) ? &channel->nob_a : &channel->nob_b;

    *nib = (*nib * nib_decay) + (absolute * nib_attack);
    *nob = (*nob * nob_decay) + (absolute * nob_attack);
    if (*nob > 0.0f)
        factor = *nib / *nob;

    return sample * factor;
}

void fx_audio_point_reset(fx_audio_point_state_t *state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->gain_trim = 1.0f;
    state->flip = 1U;
    state->needs_warm_start = 1U;
}

static float point_warm_start_channel(fx_audio_point_channel_t *channel,
                                      float sample,
                                      float gain_trim)
{
    const float trimmed = sample * gain_trim;
    const float level = fabsf(trimmed);
    if (level > 0.0f)
    {
        channel->nib_a = level;
        channel->nib_b = level;
        channel->nob_a = level;
        channel->nob_b = level;
    }
    return trimmed;
}

void fx_audio_point_prepare(fx_audio_point_state_t *state,
                            float amount,
                            float point,
                            float speed,
                            float sample_rate)
{
    if (state == NULL) return;

    const float a = clamp01(amount);
    const float b = clamp01(point);
    const float c = clamp01(speed);
    const float overall_scale = ((sample_rate > 0.0f) ? sample_rate : 44100.0f)
        / 44100.0f;
    const float point_bipolar = (b * 2.0f) - 1.0f;
    float nib_div = 1.0f / powf(c + 0.2f, 7.0f);
    nib_div /= overall_scale;

    float nob_div;
    if (point_bipolar > 0.0f)
        nob_div = nib_div / (1.001f - point_bipolar);
    else
        nob_div = nib_div
            * (1.001f - ((point_bipolar * 0.75f)
                         * (point_bipolar * 0.75f)));

    state->gain_trim = powf(10.0f, (((a * 24.0f) - 12.0f) / 20.0f));
    state->nib_attack = 1.0f / (nib_div + 1.0f);
    state->nob_attack = 1.0f / (nob_div + 1.0f);
    state->nib_decay = 1.0f - state->nib_attack;
    state->nob_decay = 1.0f - state->nob_attack;
}

float fx_audio_point_process_mono_sample(fx_audio_point_state_t *state,
                                         float sample)
{
    if (state == NULL) return sample;
    if (state->needs_warm_start != 0U)
    {
        state->needs_warm_start = 0U;
        return point_warm_start_channel(&state->left, sample,
                                        state->gain_trim);
    }
    const float output = point_process_channel(&state->left,
                                               state->flip,
                                               sample,
                                               state->gain_trim,
                                               state->nib_attack,
                                               state->nob_attack,
                                               state->nib_decay,
                                               state->nob_decay);
    state->flip = (uint8_t)(state->flip == 0U);
    return output;
}

void fx_audio_point_process_stereo_sample(fx_audio_point_state_t *state,
                                          float *left,
                                          float *right)
{
    if ((state == NULL) || (left == NULL) || (right == NULL)) return;
    if (state->needs_warm_start != 0U)
    {
        state->needs_warm_start = 0U;
        *left = point_warm_start_channel(&state->left, *left,
                                         state->gain_trim);
        *right = point_warm_start_channel(&state->right, *right,
                                          state->gain_trim);
        return;
    }
    const uint8_t flip = state->flip;
    *left = point_process_channel(&state->left, flip, *left,
                                  state->gain_trim,
                                  state->nib_attack,
                                  state->nob_attack,
                                  state->nib_decay,
                                  state->nob_decay);
    *right = point_process_channel(&state->right, flip, *right,
                                   state->gain_trim,
                                   state->nib_attack,
                                   state->nob_attack,
                                   state->nib_decay,
                                   state->nob_decay);
    state->flip = (uint8_t)(flip == 0U);
}

void fx_audio_point_process_mono(fx_audio_point_state_t *state,
                                 float *buffer,
                                 uint32_t frames)
{
    if ((state == NULL) || (buffer == NULL)) return;
    for (uint32_t i = 0U; i < frames; ++i)
        buffer[i] = fx_audio_point_process_mono_sample(state, buffer[i]);
}

void fx_audio_point_process_stereo(fx_audio_point_state_t *state,
                                   float *left,
                                   float *right,
                                   uint32_t frames)
{
    if ((state == NULL) || (left == NULL) || (right == NULL)) return;
    for (uint32_t i = 0U; i < frames; ++i)
        fx_audio_point_process_stereo_sample(state, &left[i], &right[i]);
}
