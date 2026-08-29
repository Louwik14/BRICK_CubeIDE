#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_STREAM_SERVICE_BYTE_BUDGET      (32768U)
#define BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES  (8192U)
#define BRICK6_STREAM_OTHER_SD_QUANTUM_FRAMES (1024U)

typedef struct
{
    uint32_t poll_count;
    uint32_t busy_poll_count;
    uint32_t streaming_active;
    uint32_t streaming_transition_count;
} brick6_stream_service_task_stats_t;

void brick6_stream_service_task_init(void);
void brick6_stream_service_task_poll(void);
void brick6_stream_service_task_get_stats(brick6_stream_service_task_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
