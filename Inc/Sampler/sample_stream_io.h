#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    sample_audio_key_t key;
    uint32_t page_index;
    uint32_t start_frame;
    uint32_t frame_count;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint32_t page_generation;
    uint16_t slot_index;
    sample_audio_format_t format;
    uint16_t stride_floats;
} sample_stream_io_target_t;

typedef struct
{
    sample_page_load_token_t token;
    sample_stream_io_target_t target;
    sample_page_stream_info_t stream_info;
    uint32_t deadline_margin_us;
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
    uint8_t file_opens;
    uint8_t seeks;
    uint16_t read_cache_hits;
    uint32_t decode_cycles;
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
uint8_t sample_stream_io_begin(const sample_stream_io_command_t *command);
uint8_t sample_stream_io_poll(sample_stream_io_result_t *out_result);
void sample_stream_io_cancel(void);
uint8_t sample_stream_io_command_init(sample_stream_io_command_t *out_command,
                                      const sample_page_load_token_t *token,
                                      const sample_page_load_target_t *target,
                                      const sample_page_stream_info_t *stream_info);

#ifdef __cplusplus
}
#endif
