#pragma once

#include <stdint.h>

#include "Sampler/multi_sample_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MULTI_SAMPLE_LOAD_OK = 0,
    MULTI_SAMPLE_LOAD_INVALID_ARG,
    MULTI_SAMPLE_LOAD_ALREADY_READY,
    MULTI_SAMPLE_LOAD_SD_BUSY,
    MULTI_SAMPLE_LOAD_INDEX_FAIL,
    MULTI_SAMPLE_LOAD_INDEX_LIMIT,
    MULTI_SAMPLE_LOAD_FORMAT_MISMATCH,
    MULTI_SAMPLE_LOAD_POOL_FAIL,
    MULTI_SAMPLE_LOAD_PATH_TOO_LONG,
    MULTI_SAMPLE_LOAD_REGISTER_FAIL,
    MULTI_SAMPLE_LOAD_NOT_ENOUGH_CACHE,
    MULTI_SAMPLE_LOAD_PAGE_ERROR,
    MULTI_SAMPLE_LOAD_PREP_BUDGET_EXCEEDED
} multi_sample_load_result_t;

typedef struct
{
    uint16_t instrument_id;
    uint16_t total_samples;
    uint16_t samples_ready;
    uint16_t pages_requested;
    uint16_t pages_ready;
    uint16_t prep_pages_required;
    uint16_t prep_pages_budget;
    uint16_t prep_samples_preparable;
    uint16_t last_failed_sample;
    multi_sample_load_result_t last_error;
    multi_sample_instrument_state_t state;
} multi_sample_load_diag_t;

multi_sample_load_result_t multi_sample_load_instrument(const char *index_path,
                                                        uint16_t instrument_id);
void multi_sample_service_load(uint32_t byte_budget);
uint8_t multi_sample_is_ready(uint16_t instrument_id);
uint8_t multi_sample_load_has_pending(void);
void multi_sample_get_load_diag(multi_sample_load_diag_t *out_diag);

#ifdef __cplusplus
}
#endif
