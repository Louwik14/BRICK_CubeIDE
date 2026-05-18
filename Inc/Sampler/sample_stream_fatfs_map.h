#pragma once

#include "Sampler/sample_page_cache.h"
#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    sample_stream_source_meta_t meta;
    FRESULT fr;
} sample_stream_fatfs_map_result_t;

sample_stream_fatfs_map_result_t sample_stream_fatfs_map_scan_wav(const char *path);

#ifdef __cplusplus
}
#endif
