#include "Audio/fx_audio_xfade.h"

#include <math.h>
#include <stddef.h>

#define FX_AUDIO_XFADE_HALF_PI 1.5707963267948966f
static float clamp01(float value)
{
    return (value <= 0.0f) ? 0.0f : ((value >= 1.0f) ? 1.0f : value);
}

static void gains(uint8_t curve, float position, float *a, float *b)
{
    const float x = clamp01(position);
    if (x <= 0.0f) { *a = 1.0f; *b = 0.0f; return; }
    if (x >= 1.0f) { *a = 0.0f; *b = 1.0f; return; }
    switch (curve)
    {
        case FX_AUDIO_XFADE_CURVE_LINEAR:
            *a = 1.0f - x; *b = x; break;
        case FX_AUDIO_XFADE_CURVE_DIP:
            *a = (1.0f - x) * (1.0f - x); *b = x * x; break;
        case FX_AUDIO_XFADE_CURVE_CUT:
            *a = (x < 0.5f) ? 1.0f : 0.0f;
            *b = (x >= 0.5f) ? 1.0f : 0.0f;
            break;
        case FX_AUDIO_XFADE_CURVE_TRANS:
        {
            const float shaped = x * x * (3.0f - (2.0f * x));
            *a = 1.0f - shaped; *b = shaped; break;
        }
        case FX_AUDIO_XFADE_CURVE_POWER:
        default:
            *a = cosf(x * FX_AUDIO_XFADE_HALF_PI);
            *b = sinf(x * FX_AUDIO_XFADE_HALF_PI);
            break;
    }
}

void fx_audio_xfade_init(fx_audio_xfade_t *state)
{
    if (state == NULL) return;
    state->position = 0.0f;
    state->gain_a = 1.0f;
    state->gain_b = 0.0f;
    state->gain_a_current = 1.0f;
    state->gain_b_current = 0.0f;
    state->target = FX_AUDIO_XFADE_TARGET_MASTER;
    state->curve = FX_AUDIO_XFADE_CURVE_POWER;
}

void fx_audio_xfade_prepare(fx_audio_xfade_t *state,
                            float position,
                            uint8_t target,
                            uint8_t curve)
{
    if (state == NULL) return;
    state->position = clamp01(position);
    state->target = (target < FX_AUDIO_XFADE_TARGET_COUNT)
        ? target : FX_AUDIO_XFADE_TARGET_MASTER;
    state->curve = (curve < FX_AUDIO_XFADE_CURVE_COUNT)
        ? curve : FX_AUDIO_XFADE_CURVE_POWER;
    gains(state->curve, state->position, &state->gain_a, &state->gain_b);
}

void fx_audio_xfade_process_block(fx_audio_xfade_t *state,
                                  float *carrier_l,
                                  float *carrier_r,
                                  const float *target_l,
                                  const float *target_r,
                                  uint32_t frames)
{
    if ((state == NULL) || (carrier_l == NULL) || (carrier_r == NULL)
            || (frames == 0U)) return;
    const float gain_a_step = (state->gain_a - state->gain_a_current)
        / (float)frames;
    if ((target_l == NULL) || (target_r == NULL))
    {
        float gain_a = state->gain_a_current;
        for (uint32_t i = 0U; i < frames; ++i)
        {
            gain_a += gain_a_step;
            carrier_l[i] *= gain_a;
            carrier_r[i] *= gain_a;
        }
    }
    else
    {
        float gain_a = state->gain_a_current;
        float gain_b = state->gain_b_current;
        const float gain_b_step = (state->gain_b - gain_b) / (float)frames;
        for (uint32_t i = 0U; i < frames; ++i)
        {
            gain_a += gain_a_step;
            gain_b += gain_b_step;
            carrier_l[i] = carrier_l[i] * gain_a + target_l[i] * gain_b;
            carrier_r[i] = carrier_r[i] * gain_a + target_r[i] * gain_b;
        }
    }
    state->gain_a_current = state->gain_a;
    state->gain_b_current = state->gain_b;
}
