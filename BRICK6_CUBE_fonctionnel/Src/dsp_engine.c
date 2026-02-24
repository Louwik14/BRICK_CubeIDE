#include "dsp_engine.h"

static audio_dsp_cb s_cb = 0;

void dsp_engine_set_callback(audio_dsp_cb cb)
{
    s_cb = cb;
}

void dsp_engine_process_block(StereoTrack *tracks,
                              uint32_t track_count,
                              uint32_t frames)
{
    if(s_cb)
        s_cb(tracks, track_count, frames);
}
