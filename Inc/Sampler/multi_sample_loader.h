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

typedef enum
{
    MULTI_SAMPLE_LOAD_REQUESTER_UI = 0U,
    MULTI_SAMPLE_LOAD_REQUESTER_PROJECT,
    MULTI_SAMPLE_LOAD_REQUESTER_PATCH
} multi_sample_load_requester_t;

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
    uint16_t pages_remaining;
    uint16_t samples_remaining;
    uint32_t elapsed_ms;
    uint32_t file_opens;
    uint32_t seeks;
    uint32_t read_calls;
    uint32_t physical_reads;
    uint32_t bytes_read;
    uint32_t physical_bytes;
    uint32_t max_read_bytes;
    uint32_t decode_cycles;
    uint32_t service_passes;
    uint32_t saved_page_checks;
} multi_sample_load_diag_t;

typedef struct
{
    uint32_t request_id;
    multi_sample_load_requester_t requester;
    uint16_t logical_id;
    uint16_t instrument_id;
    uint8_t success;
    multi_sample_load_result_t result;
    char path[MULTI_SAMPLE_POOL_PATH_MAX];
} multi_sample_load_completion_t;

typedef struct
{
    uint8_t used;
    uint32_t request_id;
    multi_sample_load_requester_t requester;
    uint16_t old_logical_id;
    uint16_t instrument_id;
    uint8_t cancelled;
    uint8_t import_required;
    uint8_t canonical_retired;
    uint8_t physical_retire_started;
    uint8_t import_started;
    uint8_t load_started;
    char source_path[MULTI_SAMPLE_POOL_PATH_MAX];
    char path[MULTI_SAMPLE_POOL_PATH_MAX];
} multi_sample_external_request_t;

void multi_sample_loader_init(void);
multi_sample_load_result_t multi_sample_load_request_instrument(uint16_t logical_id,
                                                                const char *index_path,
                                                                uint16_t instrument_id);
multi_sample_load_result_t multi_sample_load_request_replacement(
    uint16_t old_logical_id, uint16_t instrument_id, const char *source_path,
    const char *index_path, uint8_t import_required);
multi_sample_load_result_t multi_sample_load_instrument(uint16_t logical_id,
                                                        const char *index_path,
                                                        uint16_t instrument_id);
uint8_t multi_sample_load_required_prep_pages(const multi_sample_index_t *index,
                                              uint32_t *out_pages);
void multi_sample_service_load(uint32_t byte_budget);
void multi_sample_load_storage_request_service(void);
uint8_t multi_sample_is_ready(uint16_t instrument_id);
uint8_t multi_sample_load_has_pending(void);
uint8_t multi_sample_load_is_active(void);
uint8_t multi_sample_cancel_load(void);
void multi_sample_cancel_all_loads(void);
uint8_t multi_sample_load_take_completion(
    multi_sample_load_completion_t *out_completion);
uint8_t multi_sample_load_peek_completion(
    multi_sample_load_completion_t *out_completion);
uint8_t multi_sample_load_finish_completion(uint32_t request_id);
uint8_t multi_sample_load_peek_external(uint32_t request_id,
                                        multi_sample_external_request_t *out_request);
uint8_t multi_sample_load_mark_canonical_retired(uint32_t request_id);
uint8_t multi_sample_load_publish_external_result(
    uint32_t request_id, multi_sample_load_result_t result);
uint8_t multi_sample_load_continue_after_import(uint32_t request_id,
                                                const char *index_path);
uint8_t multi_sample_load_replacement_waiting_for_retire(void);
void multi_sample_load_publish_import_result(uint32_t request_id,
                                             uint16_t instrument_id,
                                             const char *index_path,
                                             multi_sample_load_result_t result);
void multi_sample_get_load_diag(multi_sample_load_diag_t *out_diag);

#ifdef __cplusplus
}
#endif
