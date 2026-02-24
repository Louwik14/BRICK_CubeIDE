#pragma once

#include <stdint.h>
#include "audio_float.h"

void dsp_engine_set_callback(audio_dsp_cb cb);
void dsp_engine_process_block(StereoTrack *tracks,
                              uint32_t track_count,
                              uint32_t frames);
