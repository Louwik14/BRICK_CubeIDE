#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_STREAM_SERVICE_BYTE_BUDGET      (32768U)
#define BRICK6_STREAM_SERVICE_CADENCE_FRAMES  (256U)
#define BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES  (8192U)
#define BRICK6_STREAM_OTHER_SD_QUANTUM_FRAMES (1024U)

typedef struct
{
    uint32_t audio_wake_sequence;
    uint32_t serviced_wake_sequence;
    uint32_t poll_count;
    uint32_t busy_poll_count;
    uint32_t max_dispatch_delay_frames;
    uint32_t cadence_miss_count;
    uint32_t streaming_active;
    uint32_t streaming_transition_count;
} brick6_stream_service_task_stats_t;

void brick6_stream_service_task_init(void);
void brick6_stream_service_task_notify_audio_irq(void);
void brick6_stream_service_task_poll(void);
void brick6_stream_service_task_get_stats(brick6_stream_service_task_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
