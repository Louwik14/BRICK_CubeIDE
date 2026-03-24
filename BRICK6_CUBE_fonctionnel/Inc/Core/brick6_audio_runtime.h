#pragma once

#include <stdint.h>

#include "Audio/live_recorder.h"
#include "audio_float.h"

#ifdef __cplusplus
extern "C" {
#endif

void brick6_audio_runtime_init(live_recorder_t *live_recorder);

void brick6_audio_runtime_dsp(StereoTrack *tracks,
                              uint32_t track_count,
                              uint32_t frames);

#ifdef __cplusplus
}
#endif
