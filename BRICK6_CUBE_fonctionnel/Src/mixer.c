#include "mixer.h"

#define MIXER_OUTPUTS 8U

static float master_gain = 1.0f;
static float out_gain[MIXER_OUTPUTS];

void mixer_init(void)
{
    master_gain = 1.0f;
    for(uint32_t i = 0; i < MIXER_OUTPUTS; i++)
    {
        out_gain[i] = 1.0f;
    }
}

void mixer_set_master(float gain)
{
    if(gain < 0.0f)
        gain = 0.0f;
    if(gain > 2.0f)
        gain = 2.0f;

    master_gain = gain;
}

float mixer_get_master(void)
{
    return master_gain;
}

void mixer_set_output_gain(uint32_t ch, float gain)
{
    if(ch >= MIXER_OUTPUTS)
        return;

    if(gain < 0.0f)
        gain = 0.0f;
    if(gain > 2.0f)
        gain = 2.0f;

    out_gain[ch] = gain;
}

void mixer_process(StereoTrack *tracks,
                   uint32_t track_count,
                   uint32_t frames)
{
    (void)frames;

    for(uint32_t t = 0; t < track_count; t++)
    {
        if(!tracks[t].enabled)
            continue;

        /* Hook point for per-track insert FX / routing. */
    }
}
