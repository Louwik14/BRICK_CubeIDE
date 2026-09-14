#pragma once

#include "IPC/audio_recorder_capture.h"

#define AUDIO_RECORDER_WAVEFORM_BINS 4096U

typedef struct
{
    uint32_t frame_count;
    uint32_t frames_per_bin;
    uint16_t bin_count;
    uint8_t ready;
    uint8_t reserved;
    int16_t min[AUDIO_RECORDER_WAVEFORM_BINS];
    int16_t max[AUDIO_RECORDER_WAVEFORM_BINS];
} audio_recorder_waveform_summary_t;

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
void audio_recorder_capture_audio_fault(audio_recorder_client_t client,
                                        audio_recorder_error_t fault);
uint8_t audio_recorder_capture_audio_waveform_export(
    audio_recorder_waveform_summary_t *out_summary);
