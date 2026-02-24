#pragma once

#include <stdint.h>
#include "audio_float.h"

void audio_io_unpack(const int32_t *AUDIO_RESTRICT rx,
                     StereoTrack *AUDIO_RESTRICT track_buf,
                     uint32_t frames,
                     float in_scale);

void audio_io_pack(int32_t *AUDIO_RESTRICT tx,
                   const float *AUDIO_RESTRICT bus_main_l,
                   const float *AUDIO_RESTRICT bus_main_r,
                   const float *AUDIO_RESTRICT bus_cue_l,
                   const float *AUDIO_RESTRICT bus_cue_r,
                   uint32_t frames,
                   float out_gain);
