#pragma once

#include "Sampler/sample_page_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

sample_page_load_result_t sample_stream_backend_contiguous_load_page(
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target);

#ifdef __cplusplus
}
#endif
