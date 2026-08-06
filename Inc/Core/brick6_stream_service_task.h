#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t audio_wake_sequence;
    uint32_t serviced_wake_sequence;
    uint32_t poll_count;
    uint32_t busy_poll_count;
    uint32_t max_dispatch_delay_frames;
    uint32_t cadence_miss_count;
} brick6_stream_service_task_stats_t;

void brick6_stream_service_task_init(void);
void brick6_stream_service_task_notify_audio_irq(void);
void brick6_stream_service_task_poll(void);
void brick6_stream_service_task_get_stats(brick6_stream_service_task_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
