#pragma once

#include <stdint.h>

#define AUDIO_RECORDER_SAMPLE_RATE_HZ (48000U)
#define AUDIO_RECORDER_CHANNELS (2U)
#define AUDIO_RECORDER_BITS_PER_SAMPLE (32U)
#define AUDIO_RECORDER_BYTES_PER_FRAME (8U)
#define AUDIO_RECORDER_CAPTURE_RING_FRAMES (12032U)

typedef enum
{
    AUDIO_RECORDER_ERROR_NONE = 0,
    AUDIO_RECORDER_ERROR_INVALID_ARGUMENT,
    AUDIO_RECORDER_ERROR_INVALID_STATE,
    AUDIO_RECORDER_ERROR_RING_OVERFLOW,
    AUDIO_RECORDER_ERROR_NO_SPACE,
    AUDIO_RECORDER_ERROR_SD_IO,
    AUDIO_RECORDER_ERROR_MEDIA_CHANGED,
    AUDIO_RECORDER_ERROR_OVERDUB_UNDERRUN
} audio_recorder_error_t;

/*
 * Monocore SPSC data plane.
 *
 * AUDIO owns produced_frames, started_session, closed_session and
 * capture_fault. STORAGE owns released_frames. Payload publication and frame
 * reclamation use DMB around the corresponding cursor access. The payload is
 * cacheable; CPU-to-SD-DMA maintenance remains the block-device owner's job.
 */
typedef struct
{
    volatile uint32_t produced_frames;
    volatile uint32_t released_frames;
    volatile uint32_t started_session;
    volatile uint32_t closed_session;
    volatile uint32_t capture_fault;
} audio_recorder_ring_state_t;

extern float g_audio_recorder_ring
    [AUDIO_RECORDER_CAPTURE_RING_FRAMES * AUDIO_RECORDER_CHANNELS];
extern audio_recorder_ring_state_t g_audio_recorder_ring_state;
