#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_STREAM_SERVICE_BYTE_BUDGET      (32768U)
#define BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES  (8192U)
#define BRICK6_STREAM_OTHER_SD_QUANTUM_FRAMES (1024U)

void brick6_stream_service_task_init(void);
void brick6_stream_service_task_poll(void);

#ifdef __cplusplus
}
#endif
