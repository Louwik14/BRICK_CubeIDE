#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_MAX_ACTIVE (16U)

typedef struct
{
    uint32_t request_page_calls;
    uint32_t request_range_calls;
    uint32_t service_calls;
    uint32_t has_pending_calls;
    uint32_t pages_requested;
    uint32_t pages_loaded;
    uint32_t pages_failed;
    uint32_t open_failures;
    uint32_t seek_failures;
    uint32_t read_failures;
    uint32_t close_failures;
    uint32_t reader_allocations;
    uint32_t reader_full;
    uint32_t reader_opens;
    uint32_t reader_closes;
    uint32_t sequential_reads;
    uint32_t seek_reads;
    uint32_t pages_served_urgent;
    uint32_t pages_served_normal;
    uint32_t pages_served_prefetch;
    uint32_t selected_sample_id;
    uint32_t skipped_due_fairness;
    uint32_t max_pending_age;
    uint32_t pick_scan_max;
    uint32_t pick_no_work;
    uint32_t pending_stale_dropped;
    uint32_t pending_invalid_sample;
    uint32_t pending_not_loadable;
    uint32_t pending_dropped_ready;
    uint32_t pending_dropped_loading;
    uint32_t pending_dropped_non_stream;
    uint32_t has_pending_stale_cleaned;
    uint32_t service_no_loadable_work;
    uint32_t service_time_max_ticks;
    uint32_t gate_hold_time_max_ticks;
    uint32_t service_budget_exhausted;
    uint32_t service_time_yield;
    uint32_t pages_per_call_max;
    uint64_t bytes_read;
} sample_stream_manager_diag_snapshot_t;

/*
 * STREAM facade.
 *
 * Owns Sampler STREAM SD policy and persistent readers. service() must stay
 * outside audio IRQ and be called only while the sample-cache SD gate is held.
 */
void sample_stream_manager_init(void);
void sample_stream_manager_reset(void);
void sample_stream_manager_release_key(sample_audio_key_t key);
void sample_stream_manager_release_sample(uint16_t sample_id);
uint8_t sample_stream_manager_request_page_key(sample_audio_key_t key, uint32_t page_index);
uint8_t sample_stream_manager_request_page_urgent_key(sample_audio_key_t key, uint32_t page_index);
uint8_t sample_stream_manager_request_page_normal_key(sample_audio_key_t key, uint32_t page_index);
uint8_t sample_stream_manager_request_page(uint16_t sample_id, uint32_t page_index);
uint8_t sample_stream_manager_request_range_key(sample_audio_key_t key,
                                                uint32_t start_frame,
                                                uint32_t page_count);
uint8_t sample_stream_manager_request_range(uint16_t sample_id,
                                            uint32_t start_frame,
                                            uint32_t page_count);
void sample_stream_manager_service(uint32_t byte_budget);
uint8_t sample_stream_manager_has_pending_sd_work(void);
void sample_stream_manager_diag_get_snapshot(sample_stream_manager_diag_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
