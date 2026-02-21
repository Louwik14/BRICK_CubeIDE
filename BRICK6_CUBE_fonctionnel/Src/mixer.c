#include "mixer.h"

void mixer_init(void)
{
}

void mixer_set_master(float gain)
{
    audio_float_set_master_gain(gain);
}

float mixer_get_master(void)
{
    return audio_float_get_master_gain();
}

void mixer_process(StereoTrack *tracks, uint32_t track_count, uint32_t frames)
{
    (void)tracks;
    (void)track_count;
    (void)frames;
    /* Routing-only stage for future matrix/inserts.
       Intentionally does not apply gain or modify samples. */
}
