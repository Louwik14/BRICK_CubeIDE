#pragma once

#include "Sampler/sample_page_cache.h"
#include "SD/sd_block_device.h"
#include "SD/sd_scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

void sample_stream_backend_physical_init(void);

typedef struct
{
    const sample_stream_physical_map_t *map;
    sample_stream_physical_cursor_t *cursor;
    uint8_t *buffer;
    uint64_t file_byte_offset;
    uint32_t buffer_capacity;
    uint32_t source_bytes;
    uint32_t logical_queued;
    uint32_t buffer_sectors;
    uint16_t first_sector_skip;
    uint8_t physical_reads;
    uint8_t count_multi_diag;
    uint8_t active;
    uint8_t failed;
    uint8_t completed;
    uint8_t cancel_requested;
    uint32_t deadline_margin_us;
    uint32_t deadline_started_ms;
    uint32_t owner_generation;
    sample_page_load_token_t page_token;
    uint32_t perf_accept_cycles;
    uint32_t perf_map_start_cycles;
    uint32_t perf_map_end_cycles;
    uint32_t perf_complete_cycles;
    sample_stream_physical_span_t current_span;
    sd_block_device_async_request_t request;
    uint8_t current_span_valid;
    uint8_t destination_cpu_clean;
    uint8_t progressive_enabled;
} sample_stream_backend_physical_async_t;

uint8_t sample_stream_backend_physical_begin(
    sample_stream_backend_physical_async_t *async,
    const sample_stream_safe_metadata_t *metadata,
    const sample_page_load_target_t *target,
    sample_stream_physical_cursor_t *cursor,
    uint8_t *buffer,
    uint32_t buffer_capacity,
    uint8_t destination_cpu_clean,
    uint32_t deadline_margin_us);
uint8_t sample_stream_backend_physical_poll(
    sample_stream_backend_physical_async_t *async,
    sample_page_load_result_t *out_result,
    const uint8_t **out_source,
    uint32_t *out_source_bytes,
    uint8_t *out_physical_reads);
void sample_stream_backend_physical_cancel(
    sample_stream_backend_physical_async_t *async);
sd_scheduler_provider_t sample_stream_backend_physical_read_provider(void);
uint8_t sample_stream_backend_physical_busy(void);

#ifdef __cplusplus
}
#endif
