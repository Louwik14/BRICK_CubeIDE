#pragma once

#include <stdint.h>

#include "SD/sd_scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

void sd_scheduler_runtime_init(void);
uint8_t sd_scheduler_runtime_bind_recorder(
    const sd_scheduler_provider_t *write_provider,
    const sd_scheduler_provider_t *filesystem_provider);
void sd_scheduler_runtime_service(void);
sd_scheduler_owner_t sd_scheduler_runtime_owner(void);
void sd_scheduler_runtime_metrics_get(sd_scheduler_metrics_t *metrics);

#ifdef __cplusplus
}
#endif
