#include "mixer.h"
#include "fx_warps.h"

/* Mixer state */
static float master_gain = 1.0f;
static float out_gain[MIXER_OUTPUTS];

void mixer_init(void)
{
    master_gain = 1.0f;

    for(int i = 0; i < MIXER_OUTPUTS; i++)
        out_gain[i] = 1.0f;

    fx_warps_init(48000.0f);
    fx_warps_enable(1);
}

void mixer_set_master(float gain)
{
    if(gain < 0.0f) gain = 0.0f;
    if(gain > 2.0f) gain = 2.0f; /* allow boost if needed */

    master_gain = gain;
}

/* >>> AJOUT <<< */
float mixer_get_master(void)
{
    return master_gain;
}

void mixer_set_output_gain(uint32_t ch, float gain)
{
    if(ch >= MIXER_OUTPUTS)
        return;

    if(gain < 0.0f) gain = 0.0f;
    if(gain > 2.0f) gain = 2.0f;

    out_gain[ch] = gain;
}

void mixer_process(float **in,
                   float **out,
                   uint32_t frames)
{
    float tmpL[32];
    float tmpR[32];

    for(uint32_t n = 0; n < frames; n++)
    {
        /* Clear ALL outputs first */
        for(int ch = 0; ch < 8; ch++)
            out[ch][n] = 0.0f;

        /* Copy input to local temp buffers before FX */
        tmpL[n] = in[2][n];
        tmpR[n] = in[3][n];
    }

    fx_warps_process(tmpL, tmpR, out[2], out[3], (int)frames);

    for(uint32_t n = 0; n < frames; n++)
    {
        /* Apply master gain after FX */
        out[2][n] *= master_gain;
        out[3][n] *= master_gain;
    }
}
