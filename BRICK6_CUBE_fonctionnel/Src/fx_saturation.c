#include "fx_saturation.h"

#define FX_SAT_ASYM              0.78f
#define FX_SAT_MIN_K             1.0f
#define FX_SAT_K_RANGE           25.0f
#define FX_SAT_MIN_OUTPUT_GAIN   0.33f

void fx_saturation_init(fx_saturation_t *fx)
{
    if(fx == 0)
    {
        return;
    }

    fx->k = FX_SAT_MIN_K;
    fx->asym = FX_SAT_ASYM;
    fx->output_gain = 1.0f;
    fx->mix = 1.0f;
    fx->dry = 0.0f;
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
        fx->k = FX_SAT_MIN_K;
        fx->output_gain = 1.0f;
        fx->bypass = 1U;
        return;
    }

    const float norm = (float)drive_0_127 * (1.0f / 127.0f);
    const float shaped = norm * norm;

    fx->k = FX_SAT_MIN_K + (FX_SAT_K_RANGE * shaped);

    /* Compensation cheap: diminue progressivement quand k augmente. */
    fx->output_gain = 1.0f / (1.0f + 0.06f * fx->k);
    if(fx->output_gain < FX_SAT_MIN_OUTPUT_GAIN)
    {
        fx->output_gain = FX_SAT_MIN_OUTPUT_GAIN;
    }

    fx->bypass = 0U;
}

void fx_saturation_set_mix_ui(fx_saturation_t *fx, uint8_t mix_0_127)
{
    if(fx == 0)
    {
        return;
    }

    fx->mix = (float)mix_0_127 * (1.0f / 127.0f);
    fx->dry = 1.0f - fx->mix;
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

    const float k = fx->k;
    const float asym = fx->asym;
    const float out_gain = fx->output_gain;
    const float wet = fx->mix;
    const float dry = fx->dry;

    float *l = inout_l;
    float *r = inout_r;

    for(uint32_t n = 0U; n < frames; n++)
    {
        float xl = l[n];
        float xr = r[n];

        if(xl < 0.0f)
        {
            xl *= asym;
        }
        if(xr < 0.0f)
        {
            xr *= asym;
        }

        const float xl2 = xl * xl;
        const float xr2 = xr * xr;
        const float axl = __builtin_fabsf(xl);
        const float axr = __builtin_fabsf(xr);

        const float sat_l = (xl * (1.0f + k * axl) / (1.0f + k * xl2)) * out_gain;
        const float sat_r = (xr * (1.0f + k * axr) / (1.0f + k * xr2)) * out_gain;

        float yl = l[n] * dry + sat_l * wet;
        float yr = r[n] * dry + sat_r * wet;

        yl = __builtin_fmaxf(-1.0f, __builtin_fminf(yl, 1.0f));
        yr = __builtin_fmaxf(-1.0f, __builtin_fminf(yr, 1.0f));

        l[n] = yl;
        r[n] = yr;
    }
}
