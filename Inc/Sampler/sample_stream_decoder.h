#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

sample_page_load_result_t sample_stream_decoder_decode_page(
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target,
    const uint8_t *source,
    uint32_t source_bytes);

#ifdef __cplusplus
}
#endif
