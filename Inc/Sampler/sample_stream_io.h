#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"

#ifndef BRICK6_STREAM_BENCH
#define BRICK6_STREAM_BENCH (0)
#endif

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
    uint32_t read_bytes;
    uint16_t fatfs_ops;
    uint8_t physical_reads;
    uint8_t backend;
#if BRICK6_STREAM_BENCH
    uint8_t file_opens;
    uint8_t seeks;
    uint16_t read_cache_hits;
#endif
} sample_stream_io_result_t;

typedef enum
{
    SAMPLE_STREAM_READ_CHUNK_4_KIB = 4,
    SAMPLE_STREAM_READ_CHUNK_8_KIB = 8,
    SAMPLE_STREAM_READ_CHUNK_16_KIB = 16,
    SAMPLE_STREAM_READ_CHUNK_32_KIB = 32
} sample_stream_read_chunk_kib_t;

void sample_stream_io_init(void);
void sample_stream_io_reset(void);
void sample_stream_io_release_key(sample_audio_key_t key);
uint32_t sample_stream_io_active_reader_count(void);
uint8_t sample_stream_io_set_read_chunk_kib(sample_stream_read_chunk_kib_t chunk_kib);
sample_stream_read_chunk_kib_t sample_stream_io_get_read_chunk_kib(void);
void sample_stream_io_execute(const sample_stream_io_command_t *command,
                              sample_stream_io_result_t *out_result);

#ifdef __cplusplus
}
#endif
