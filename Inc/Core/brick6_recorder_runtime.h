/**
 * @file brick6_recorder_runtime.h
 * @brief Recorder runtime orchestration API.
 *
 * Rôle du module:
 * - Exposer les hooks boot/runtime du live recorder + transport + SD writer.
 *
 * Frontière:
 * - Ne contient pas les implémentations DSP internes des recorders.
 * - Ne décide pas des policy UI de déclenchement.
 */

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
