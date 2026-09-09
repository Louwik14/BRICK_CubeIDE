#pragma once

#include <stdint.h>

#include "Sampler/multi_sample_pool.h"
#include "Sampler/multi_sample_index.h"

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
    MULTI_SAMPLE_LOAD_PREP_BUDGET_EXCEEDED,
    MULTI_SAMPLE_LOAD_TRANSPORT_ACTIVE,
    MULTI_SAMPLE_LOAD_CANCELLED
} multi_sample_load_result_t;

typedef struct
{
    uint16_t instrument_id;
    uint16_t pages_requested;
    uint16_t pages_ready;
    uint16_t prep_pages_required;
    multi_sample_load_result_t last_error;
    multi_sample_instrument_state_t state;
} multi_sample_load_diag_t;

void multi_sample_loader_init(void);
multi_sample_load_result_t multi_sample_load_instrument(uint16_t logical_id,
                                                        const char *index_path,
                                                        uint16_t instrument_id);
uint8_t multi_sample_load_required_prep_pages(const multi_sample_index_t *index,
                                              uint32_t *out_pages);
void multi_sample_service_load(uint32_t byte_budget);
uint8_t multi_sample_is_ready(uint16_t instrument_id);
uint8_t multi_sample_load_has_pending(void);
uint8_t multi_sample_load_is_active(void);
uint8_t multi_sample_cancel_load(void);
void multi_sample_cancel_all_loads(void);
void multi_sample_get_load_diag(multi_sample_load_diag_t *out_diag);

#ifdef __cplusplus
}
#endif
