#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    sample_page_load_token_t token;
    sample_page_load_target_t target;
    sample_page_stream_info_t stream_info;
} sample_stream_io_command_t;

typedef struct
{
    sample_page_load_token_t token;
    sample_page_load_result_t load_result;
    uint32_t source_bytes;
    uint16_t fatfs_ops;
    uint8_t physical_reads;
    uint8_t backend;
} sample_stream_io_result_t;

void sample_stream_io_init(void);
void sample_stream_io_reset(void);
void sample_stream_io_release_key(sample_audio_key_t key);
uint32_t sample_stream_io_active_reader_count(void);
void sample_stream_io_execute(const sample_stream_io_command_t *command,
                              sample_stream_io_result_t *out_result);

#ifdef __cplusplus
}
#endif
