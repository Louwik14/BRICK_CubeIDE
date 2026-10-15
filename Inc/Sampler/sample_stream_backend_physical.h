#pragma once

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_limits.h"
#include "SD/sd_block_device.h"
#include "SD/sd_scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const sample_stream_physical_map_t *map;
    sample_stream_physical_cursor_t *cursor;
    uint8_t *buffer;
    uint8_t *page_buffer[SAMPLE_STREAM_FASTPATH_MAX_PAGES];
    uint64_t file_byte_offset;
    uint32_t buffer_capacity;
    uint32_t source_bytes;
    uint32_t logical_page_bytes;
    uint16_t bytes_per_frame;
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
    sample_page_load_token_t page_token[SAMPLE_STREAM_FASTPATH_MAX_PAGES];
    sample_stream_physical_span_t current_span;
    sd_block_device_async_request_t request;
    uint8_t current_span_valid;
    uint8_t destination_cpu_clean;
    uint8_t progressive_enabled;
    uint8_t page_count;
    uint8_t scatter_enabled;
} sample_stream_backend_physical_async_t;

uint8_t sample_stream_backend_physical_begin(
    sample_stream_backend_physical_async_t *async,
    const sample_stream_safe_metadata_t *metadata,
    const sample_page_load_target_t *target,
    uint8_t page_count,
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
uint8_t sample_stream_backend_physical_contiguous_page_count(
    const sample_stream_safe_metadata_t *metadata,
    uint32_t start_frame,
    uint8_t requested_pages);
uint8_t sample_stream_backend_physical_submitted(
    const sample_stream_backend_physical_async_t *async);

#ifdef __cplusplus
}
#endif
