#pragma once

#include "Sampler/sample_page_cache.h"
#include "SD/sd_scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const sample_stream_physical_map_t *map;
    sample_stream_physical_cursor_t *cursor;
    uint8_t *scratch;
    uint64_t file_byte_offset;
    uint32_t scratch_capacity;
    uint32_t source_bytes;
    uint32_t logical_queued;
    uint32_t scratch_sectors;
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
    uint32_t active_lba;
    uint32_t active_sector_count;
    uint8_t *active_buffer;
    sample_stream_physical_span_t cached_span;
    const sample_stream_physical_map_t *cached_map;
    sample_stream_physical_cursor_t *cached_cursor;
    uint8_t *cached_scratch;
    uint64_t cached_file_byte_offset;
    uint32_t cached_requested_bytes;
    uint32_t cached_source_bytes;
    uint32_t cached_scratch_sectors;
    uint32_t cached_map_generation;
    uint32_t cached_media_epoch;
    uint8_t cached_span_valid;
} sample_stream_backend_physical_async_t;

uint8_t sample_stream_backend_physical_begin(
    sample_stream_backend_physical_async_t *async,
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target,
    sample_stream_physical_cursor_t *cursor,
    uint8_t *scratch,
    uint32_t scratch_capacity,
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
