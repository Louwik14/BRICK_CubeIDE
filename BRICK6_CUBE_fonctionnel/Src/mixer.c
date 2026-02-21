#include "mixer.h"

static float master_gain = 1.0f;

void mixer_init(void)
{
    master_gain = 1.0f;
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

void mixer_process(StereoTrack *tracks, uint32_t track_count, uint32_t frames)
{
    for(uint32_t t = 0; t < track_count; t++)
    {
        if(tracks[t].enabled)
        {
            for(uint32_t n = 0; n < frames; n++)
            {
                tracks[t].L[n] *= master_gain;
                tracks[t].R[n] *= master_gain;
            }
        }
    }
}
