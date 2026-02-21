#include "fx_warps.h"

#include "warps/dsp/warps_fx_c_api.h"

static warps_params_t g_params;
static int g_fx_enabled = 1;

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
}

void fx_warps_process(float* inL, float* inR, float* outL, float* outR, int size)
{
    if(size <= 0)
        return;

    if(!g_fx_enabled)
    {
        for(int i = 0; i < size; ++i)
        {
            outL[i] = inL[i];
            outR[i] = inR[i];
        }
        return;
    }

    warps_fx_engine_process(inL, inR, outL, outR, size);
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
