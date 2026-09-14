#include "Audio/fx_audio_xfade.h"

#include <math.h>
#include <stddef.h>

#define FX_AUDIO_XFADE_HALF_PI 1.5707963267948966f
#define FX_AUDIO_XFADE_LUT_LAST 127U

static float g_xfade_gain_lut[FX_AUDIO_XFADE_CURVE_COUNT][FX_AUDIO_XFADE_LUT_LAST+1U][2U];
static uint8_t g_xfade_gain_lut_ready;

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

static void prepare_lut(void)
{
    if(g_xfade_gain_lut_ready!=0U)return;
    for(uint8_t curve=0U;curve<FX_AUDIO_XFADE_CURVE_COUNT;++curve)
        for(uint16_t i=0U;i<=FX_AUDIO_XFADE_LUT_LAST;++i)
            gains(curve,(float)i/(float)FX_AUDIO_XFADE_LUT_LAST,
                  &g_xfade_gain_lut[curve][i][0],&g_xfade_gain_lut[curve][i][1]);
    g_xfade_gain_lut_ready=1U;
}

static void lookup_gains(uint8_t curve,float position,float*a,float*b)
{
    const float pos=clamp01(position)*(float)FX_AUDIO_XFADE_LUT_LAST;
    uint16_t index=(uint16_t)pos;
    if(index>=FX_AUDIO_XFADE_LUT_LAST){*a=g_xfade_gain_lut[curve][FX_AUDIO_XFADE_LUT_LAST][0];*b=g_xfade_gain_lut[curve][FX_AUDIO_XFADE_LUT_LAST][1];return;}
    const float frac=pos-(float)index;
    *a=g_xfade_gain_lut[curve][index][0]+(g_xfade_gain_lut[curve][index+1U][0]-g_xfade_gain_lut[curve][index][0])*frac;
    *b=g_xfade_gain_lut[curve][index][1]+(g_xfade_gain_lut[curve][index+1U][1]-g_xfade_gain_lut[curve][index][1])*frac;
}

void fx_audio_xfade_init(fx_audio_xfade_t *state)
{
    if (state == NULL) return;
    prepare_lut();
    state->position = 0.0f;
    state->position_current = 0.0f;
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
    const float start = state->position_current;
    const float end = state->position;
    const float step = (end - start) / (float)frames;
    float position = start;
    if ((target_l == NULL) || (target_r == NULL))
    {
        for (uint32_t i = 0U; i < frames; ++i)
        {
            float gain_a, gain_b;
            position += step;
            lookup_gains(state->curve, position, &gain_a, &gain_b);
            (void)gain_b;
            carrier_l[i] *= gain_a;
            carrier_r[i] *= gain_a;
        }
    }
    else
    {
        for (uint32_t i = 0U; i < frames; ++i)
        {
            float gain_a, gain_b;
            position += step;
            lookup_gains(state->curve, position, &gain_a, &gain_b);
            carrier_l[i] = carrier_l[i] * gain_a + target_l[i] * gain_b;
            carrier_r[i] = carrier_r[i] * gain_a + target_r[i] * gain_b;
        }
    }
    state->position_current = end;
}
