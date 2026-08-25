#pragma once

#include <stdint.h>

#include "SD/sd_scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES (4096U)

typedef enum
{
    SD_SCHEDULER_BACKGROUND_DATA = 0,
    SD_SCHEDULER_BACKGROUND_METADATA
} sd_scheduler_background_kind_t;

typedef struct
{
    uint32_t byte_count;
    uint32_t media_epoch;
    sd_scheduler_background_kind_t kind;
} sd_scheduler_background_request_t;

typedef enum
{
    SD_SCHEDULER_BACKGROUND_NOT_NOW = 0,
    SD_SCHEDULER_BACKGROUND_GO,
    SD_SCHEDULER_BACKGROUND_INVALID
} sd_scheduler_background_admission_t;

void sd_scheduler_runtime_init(void);
uint8_t sd_scheduler_runtime_bind_recorder(
    const sd_scheduler_provider_t *write_provider,
    const sd_scheduler_provider_t *filesystem_provider);
void sd_scheduler_runtime_service(void);
sd_scheduler_background_admission_t sd_scheduler_runtime_background_try_begin(
    const sd_scheduler_background_request_t *request);
void sd_scheduler_runtime_background_end(void);
uint8_t sd_scheduler_runtime_background_active(void);
void sd_scheduler_runtime_exclusive_request(void);
uint8_t sd_scheduler_runtime_exclusive_try_begin(void);
void sd_scheduler_runtime_exclusive_end(void);
sd_scheduler_owner_t sd_scheduler_runtime_owner(void);
void sd_scheduler_runtime_metrics_get(sd_scheduler_metrics_t *metrics);

#ifdef __cplusplus
}
#endif
