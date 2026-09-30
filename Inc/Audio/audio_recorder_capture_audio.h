#pragma once

#include "Recorder/audio_recorder_ring.h"

void audio_recorder_capture_audio_init(void);
uint8_t audio_recorder_capture_audio_start(uint32_t session_id,
                                           uint32_t frame_limit);
uint8_t audio_recorder_capture_audio_stop(uint32_t session_id);
uint8_t audio_recorder_capture_audio_push(const float *lr_interleaved,
                                          uint32_t frames);
void audio_recorder_capture_audio_fault(audio_recorder_error_t fault);
