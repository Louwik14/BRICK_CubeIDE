#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MULTI_SAMPLE_IMPORT_OK = 0,
    MULTI_SAMPLE_IMPORT_INVALID_ARG,
    MULTI_SAMPLE_IMPORT_SD_BUSY,
    MULTI_SAMPLE_IMPORT_SD_MOUNT_FAIL,
    MULTI_SAMPLE_IMPORT_OPEN_DIR_FAIL,
    MULTI_SAMPLE_IMPORT_NO_WAV,
    MULTI_SAMPLE_IMPORT_TOO_MANY_SAMPLES,
    MULTI_SAMPLE_IMPORT_PATH_TOO_LONG,
    MULTI_SAMPLE_IMPORT_WAV_OPEN_FAIL,
    MULTI_SAMPLE_IMPORT_WAV_PARSE_FAIL,
    MULTI_SAMPLE_IMPORT_WAV_UNSUPPORTED,
    MULTI_SAMPLE_IMPORT_DUPLICATE_ZONE,
    MULTI_SAMPLE_IMPORT_ZONE_LIMIT,
    MULTI_SAMPLE_IMPORT_INDEX_WRITE_FAIL
} multi_sample_import_result_t;

typedef enum
{
    MULTI_SAMPLE_IMPORT_PHASE_IDLE = 0,
    MULTI_SAMPLE_IMPORT_PHASE_SCAN,
    MULTI_SAMPLE_IMPORT_PHASE_CONVERT,
    MULTI_SAMPLE_IMPORT_PHASE_INDEX,
    MULTI_SAMPLE_IMPORT_PHASE_READY,
    MULTI_SAMPLE_IMPORT_PHASE_FAILED,
    MULTI_SAMPLE_IMPORT_PHASE_CANCELLED
} multi_sample_import_phase_t;

typedef struct
{
    multi_sample_import_phase_t phase;
    multi_sample_import_result_t result;
    uint16_t current_item;
    uint16_t total_items;
    uint64_t work_done_bytes;
    uint64_t work_total_bytes;
} multi_sample_import_status_t;

uint8_t multi_sample_import_start(const char *instrument_dir);
void multi_sample_import_service(uint32_t byte_budget);
uint8_t multi_sample_import_is_active(void);
uint8_t multi_sample_import_cancel(void);
void multi_sample_import_get_status(multi_sample_import_status_t *out_status);
void multi_sample_import_clear_finished(void);
multi_sample_import_result_t multi_sample_import_get_last_result(void);
const char *multi_sample_import_get_last_diagnostic(void);
uint16_t multi_sample_import_get_last_sample_count(void);
uint16_t multi_sample_import_get_last_zone_count(void);

#ifdef __cplusplus
}
#endif
