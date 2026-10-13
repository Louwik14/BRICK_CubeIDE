#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_IO_JOB_CAPACITY (2U)

typedef struct
{
    sample_page_load_token_t token[SAMPLE_STREAM_FASTPATH_MAX_PAGES];
    sample_page_load_result_t load_result;
    uint32_t source_bytes;
    uint32_t read_bytes;
    uint32_t request_cycles;
    uint8_t page_count;
    uint8_t request_class;
} sample_stream_io_result_t;

typedef enum
{
    SAMPLE_STREAM_REQUEST_NORMAL_16K = 0,
    SAMPLE_STREAM_REQUEST_COALESCED_SEQUENTIAL
} sample_stream_request_class_t;

typedef enum
{
    SAMPLE_STREAM_READ_CHUNK_4_KIB = 4,
    SAMPLE_STREAM_READ_CHUNK_8_KIB = 8,
    SAMPLE_STREAM_READ_CHUNK_16_KIB = 16,
    SAMPLE_STREAM_READ_CHUNK_32_KIB = 32
} sample_stream_read_chunk_kib_t;

void sample_stream_io_init(void);
void sample_stream_io_reset(void);
uint8_t sample_stream_io_set_read_chunk_kib(sample_stream_read_chunk_kib_t chunk_kib);
sample_stream_read_chunk_kib_t sample_stream_io_get_read_chunk_kib(void);
uint8_t sample_stream_io_begin(const sample_page_load_token_t *token,
                               uint32_t deadline_margin_us);
uint8_t sample_stream_io_begin_coalesced(
    const sample_page_load_token_t token[SAMPLE_STREAM_FASTPATH_MAX_PAGES],
    uint8_t page_count,
    uint8_t voice_id,
    uint32_t lease_seq,
    uint32_t deadline_margin_us);
uint8_t sample_stream_io_cancel_stale_coalesced_before_submit(
    uint8_t voice_id, uint32_t current_lease_seq);
uint8_t sample_stream_io_poll(sample_stream_io_result_t *out_result);
uint32_t sample_stream_io_active_job_count(void);
uint8_t sample_stream_io_key_busy(sample_audio_key_t key);
void sample_stream_io_cancel(void);

#ifdef __cplusplus
}
#endif
