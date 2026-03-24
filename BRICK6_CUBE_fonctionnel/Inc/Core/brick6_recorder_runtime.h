#pragma once

#include <stdint.h>

#include "Audio/live_recorder.h"

#ifdef __cplusplus
extern "C" {
#endif

void brick6_recorder_runtime_boot_init(live_recorder_t *rec,
                                       float *buffer,
                                       uint32_t max_frames);

void brick6_recorder_runtime_process_transport(live_recorder_t *rec);

void brick6_recorder_runtime_service_writer(void);

#ifdef __cplusplus
}
#endif
