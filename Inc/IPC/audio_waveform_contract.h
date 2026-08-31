#pragma once

#include <stdint.h>

#define AUDIO_WAVEFORM_CAPTURE_GRAPH_X 1U
#define AUDIO_WAVEFORM_CAPTURE_GRAPH_WIDTH 94U
#define AUDIO_WAVEFORM_CAPTURE_COLUMNS AUDIO_WAVEFORM_CAPTURE_GRAPH_WIDTH
#define AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES 1504U
#define AUDIO_WAVEFORM_CAPTURE_FRAME_AUDIO_SAMPLES 1504U
#define AUDIO_WAVEFORM_CAPTURE_FREE_RUN_SAMPLES (AUDIO_WAVEFORM_CAPTURE_FRAME_AUDIO_SAMPLES * 2U)

typedef struct
{
    uint32_t generation;
    uint8_t entity_id;
    uint8_t valid;
    uint8_t triggered;
    uint8_t trigger_fraction_q8;
    int8_t samples[AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES];
} audio_waveform_capture_snapshot_t;

typedef struct
{
    volatile uint32_t sequence;
    volatile uint32_t generation;
    volatile uint8_t entity_id;
    volatile uint8_t buffer;
    volatile uint8_t valid;
    volatile uint8_t triggered;
    volatile uint8_t trigger_fraction_q8;
} audio_waveform_layout_t;

extern int8_t g_audio_waveform_buffers[2][AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES];
extern audio_waveform_layout_t g_audio_waveform_layout;
