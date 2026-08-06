#pragma once

#include <stdint.h>

#include "Sampler/sample_stream_request_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_SCHEDULER_DEFAULT_MAX_WAIT_FRAMES (24000ULL)

typedef struct
{
    sample_stream_audio_frame_t max_wait_frames;
} sample_stream_scheduler_config_t;

typedef struct
{
    uint32_t entry_index;
    sample_stream_audio_frame_t consume_deadline_audio_frame;
    sample_stream_audio_frame_t dispatch_deadline_audio_frame;
    sample_stream_audio_frame_t waited_frames;
    uint8_t starvation_guard_applied;
    uint8_t reserved[3];
} sample_stream_scheduler_decision_t;

void sample_stream_scheduler_init(const sample_stream_scheduler_config_t *config);
uint8_t sample_stream_scheduler_pick(
    const sample_stream_request_entry_t *entries,
    uint32_t entry_count,
    sample_stream_audio_frame_t now_audio_frame,
    sample_stream_scheduler_decision_t *out_decision);

#ifdef __cplusplus
}
#endif
