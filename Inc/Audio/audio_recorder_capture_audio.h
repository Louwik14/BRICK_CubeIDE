#pragma once

#include "IPC/audio_recorder_capture.h"

void audio_recorder_capture_audio_init(void);
uint8_t audio_recorder_capture_audio_start(uint8_t client,
                                           uint32_t session_id,
                                           uint32_t frame_limit);
uint8_t audio_recorder_capture_audio_stop(uint8_t client,
                                          uint32_t session_id);
uint8_t audio_recorder_capture_audio_push(audio_recorder_client_t client,
                                          const int32_t *lr_interleaved,
                                          uint32_t frames);
uint8_t audio_recorder_capture_audio_frames(audio_recorder_client_t client,
                                            uint32_t *out_frames);
uint8_t audio_recorder_capture_audio_pending(void);
