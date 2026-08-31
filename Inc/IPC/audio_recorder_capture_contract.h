#pragma once

#include "IPC/audio_recorder_capture.h"

typedef struct
{
    volatile uint32_t head_cursor;
    volatile uint32_t tail_cursor;
    volatile uint32_t closed_session;
    volatile uint32_t capture_fault;
} audio_recorder_capture_transport_t;

_Static_assert(sizeof(audio_recorder_capture_transport_t) == 16U,
               "Recorder capture transport ABI changed");

extern int32_t g_audio_recorder_capture_ring
    [AUDIO_RECORDER_CAPTURE_RING_FRAMES * AUDIO_RECORDER_CHANNELS];
extern audio_recorder_capture_transport_t g_audio_recorder_capture;
