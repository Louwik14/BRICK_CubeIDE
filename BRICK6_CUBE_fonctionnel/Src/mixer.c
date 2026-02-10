#include "mixer.h"

/* Mixer state */
static float master_gain = 1.0f;
static float out_gain[MIXER_OUTPUTS];

void mixer_init(void)
{
    master_gain = 1.0f;

    for(int i = 0; i < MIXER_OUTPUTS; i++)
        out_gain[i] = 1.0f;
}

void mixer_set_master(float gain)
{
    if(gain < 0.0f) gain = 0.0f;
    if(gain > 2.0f) gain = 2.0f; /* allow boost if needed */

    master_gain = gain;
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
    for(uint32_t n = 0; n < frames; n++)
    {
        /* TEST: route ADC2/ADC3 -> DAC1/DAC2 */

        out[4][n] = in[2][n] * master_gain;  // DAC1 = ADC2
        out[5][n] = in[3][n] * master_gain;  // DAC2 = ADC3

        /* Silence everything else */
        for(int ch = 2; ch < 8; ch++)
            out[ch][n] = 0.0f;
    }
}
