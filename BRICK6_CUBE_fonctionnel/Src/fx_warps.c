#include "fx_warps.h"

#include "warps/dsp/warps_fx_c_api.h"

static warps_params_t g_params;
static int g_fx_enabled = 1;
static float g_dry_wet = 0.0f;

static float clamp01(float x)
{
    if(x < 0.0f) return 0.0f;
    if(x > 1.0f) return 1.0f;
    return x;
}

void fx_warps_init(float sample_rate)
{
    warps_fx_engine_init(sample_rate);

    g_params.algorithm = 0.0f;
    g_params.parameter = 0.0f;
    g_params.drive1 = 0.5f;
    g_params.drive2 = 0.5f;

    warps_fx_engine_set_algorithm(g_params.algorithm);
    warps_fx_engine_set_parameter(g_params.parameter);
    warps_fx_engine_set_drive(g_params.drive1, g_params.drive2);

    g_fx_enabled = 1;
    g_dry_wet = 0.0f;
}

void fx_warps_process(float* inL, float* inR, float* outL, float* outR, int size)
{
    if(size <= 0)
        return;

    if(!g_fx_enabled || g_dry_wet <= 0.0f)
    {
        for(int i = 0; i < size; ++i)
        {
            outL[i] = inL[i];
            outR[i] = inR[i];
        }
        return;
    }

    if(g_dry_wet >= 1.0f)
    {
        warps_fx_engine_process(inL, inR, outL, outR, size);
        return;
    }

    for(int offset = 0; offset < size; offset += 32)
    {
        int chunk = size - offset;
        if(chunk > 32) chunk = 32;

        float wetL[32];
        float wetR[32];

        warps_fx_engine_process(inL + offset, inR + offset, wetL, wetR, chunk);

        for(int i = 0; i < chunk; ++i)
        {
            float dryL = inL[offset + i];
            float dryR = inR[offset + i];
            outL[offset + i] = dryL + (wetL[i] - dryL) * g_dry_wet;
            outR[offset + i] = dryR + (wetR[i] - dryR) * g_dry_wet;
        }
    }
}

void fx_warps_set_algorithm(float v)
{
    g_params.algorithm = clamp01(v);
    warps_fx_engine_set_algorithm(g_params.algorithm);
}

void fx_warps_set_parameter(float v)
{
    g_params.parameter = clamp01(v);
    warps_fx_engine_set_parameter(g_params.parameter);
}

void fx_warps_set_drive(float d1, float d2)
{
    g_params.drive1 = clamp01(d1);
    g_params.drive2 = clamp01(d2);
    warps_fx_engine_set_drive(g_params.drive1, g_params.drive2);
}

void fx_warps_enable(int enabled)
{
    g_fx_enabled = enabled ? 1 : 0;
}

void fx_warps_set_drywet(float v)
{
    g_dry_wet = clamp01(v);
}
