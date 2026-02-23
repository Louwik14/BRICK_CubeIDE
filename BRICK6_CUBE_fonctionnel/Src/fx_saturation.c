#include "fx_saturation.h"

#define FX_SAT_MIN_DRIVE 0.5f
#define FX_SAT_MAX_DRIVE 10.0f

static inline float fx_sat_softclip(float x)
{
    const float ax = __builtin_fabsf(x);
    return x / (1.0f + ax);
}

void fx_saturation_init(fx_saturation_t *fx)
{
    if(fx == 0)
    {
        return;
    }

    fx->drive_gain = 1.0f;
    fx->mix = 1.0f;
    fx->bypass = 1U;
}

void fx_saturation_set_drive_ui(fx_saturation_t *fx, uint8_t drive_0_127)
{
    if(fx == 0)
    {
        return;
    }

    if(drive_0_127 == 0U)
    {
        fx->drive_gain = 1.0f;
        fx->bypass = 1U;
        return;
    }

    const float t = (float)drive_0_127 * (1.0f / 127.0f);
    const float shaped = t * t;

    fx->drive_gain = FX_SAT_MIN_DRIVE + (FX_SAT_MAX_DRIVE - FX_SAT_MIN_DRIVE) * shaped;
    fx->bypass = 0U;
}

void fx_saturation_set_mix_ui(fx_saturation_t *fx, uint8_t mix_0_127)
{
    if(fx == 0)
    {
        return;
    }

    fx->mix = (float)mix_0_127 * (1.0f / 127.0f);
}

void fx_saturation_process_block(fx_saturation_t *fx,
                                 float *inout_l,
                                 float *inout_r,
                                 uint32_t frames)
{
    if((fx == 0) || (inout_l == 0) || (inout_r == 0) || (frames == 0U) || (fx->bypass != 0U))
    {
        return;
    }

    const float drive = fx->drive_gain;
    const float wet = fx->mix;
    const float dry = 1.0f - wet;

    for(uint32_t n = 0; n < frames; n++)
    {
        const float in_l = inout_l[n];
        const float in_r = inout_r[n];

        const float sat_l = fx_sat_softclip(in_l * drive);
        const float sat_r = fx_sat_softclip(in_r * drive);

        float out_l = in_l * dry + sat_l * wet;
        float out_r = in_r * dry + sat_r * wet;

        out_l = __builtin_fmaxf(-1.0f, __builtin_fminf(out_l, 1.0f));
        out_r = __builtin_fmaxf(-1.0f, __builtin_fminf(out_r, 1.0f));

        inout_l[n] = out_l;
        inout_r[n] = out_r;
    }
}
