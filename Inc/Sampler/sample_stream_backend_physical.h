#pragma once

#include "Sampler/sample_page_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

sample_page_load_result_t sample_stream_backend_physical_read_page(
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target,
    sample_stream_physical_cursor_t *cursor,
    uint8_t *scratch,
    uint32_t scratch_capacity,
    const uint8_t **out_source,
    uint32_t *out_source_bytes,
    uint8_t *out_physical_reads);

#ifdef __cplusplus
}
#endif
