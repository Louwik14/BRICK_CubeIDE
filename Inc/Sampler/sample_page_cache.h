#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache_config.h"
#include "Sampler/sample_pool.h"
#include "Storage/wav_parser.h"

#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SAMPLE_PAGE_EMPTY = 0,
    SAMPLE_PAGE_QUEUED,
    SAMPLE_PAGE_LOADING,
    SAMPLE_PAGE_READY,
    SAMPLE_PAGE_ERROR
} sample_page_state_t;

typedef struct
{
    uint16_t sample_id;
    uint16_t reserved;
    uint32_t page_index;
    uint32_t start_frame;
    uint32_t frame_count;
    float *data;
    sample_page_state_t state;
    uint16_t pin_count;
    uint16_t use_count;
    uint32_t generation;
    uint32_t last_touch;
} sample_page_desc_t;

typedef struct
{
    const float *frames_interleaved;
    uint32_t frame_count;
    uint32_t start_frame;
    uint32_t page_index;
    uint32_t page_generation;
    uint32_t slot_index;
} sample_page_span_t;

typedef struct
{
    uint32_t page_index;
    uint32_t page_generation;
    uint32_t slot_index;
} sample_page_ref_t;

typedef enum
{
    SAMPLE_PAGE_BLOCK_OK = 0,
    SAMPLE_PAGE_BLOCK_DONE,
    SAMPLE_PAGE_BLOCK_NOT_READY
} sample_page_block_status_t;

typedef struct
{
    const float *frames_interleaved;
    uint32_t frame_count;
    uint32_t start_frame;
    uint32_t page_index;
    sample_page_block_status_t status;
} sample_page_block_t;

typedef enum
{
    SAMPLE_PAGE_LOAD_OK = 0,
    SAMPLE_PAGE_LOAD_INVALID_ARG,
    SAMPLE_PAGE_LOAD_UNSUPPORTED_SAMPLE,
    SAMPLE_PAGE_LOAD_NO_SPACE,
    SAMPLE_PAGE_LOAD_SEEK_FAILED,
    SAMPLE_PAGE_LOAD_READ_FAILED,
    SAMPLE_PAGE_LOAD_DECODE_FAILED
} sample_page_load_result_t;

/*
 * Query API: RAM-only, no SD side effect, no implicit page request.
 */
void sample_page_cache_init(void);
void sample_page_cache_reset(void);
void sample_page_cache_clear_sample(uint16_t sample_id);
sample_page_state_t sample_page_cache_get_page_state(uint16_t sample_id, uint32_t page_index);
const sample_page_desc_t *sample_page_cache_get_page_desc(uint32_t slot_index);
uint8_t sample_page_cache_try_acquire_page(uint16_t sample_id,
                                           uint32_t page_index,
                                           sample_page_span_t *out_span);
uint8_t sample_page_cache_try_acquire_page_ref(uint16_t sample_id,
                                               const sample_page_ref_t *ref,
                                               sample_page_span_t *out_span);
void sample_page_cache_release_page(uint16_t sample_id, uint32_t page_index);
void sample_page_cache_release_page_ref(uint16_t sample_id, const sample_page_ref_t *ref);
const float *sample_page_cache_get_full_sample_base(uint16_t sample_id, uint32_t *out_frames);
uint8_t sample_page_cache_begin_read_block(uint16_t sample_id,
                                           uint32_t frame_index,
                                           uint32_t max_frames,
                                           sample_page_block_t *out_block);
void sample_page_cache_commit_read_block(uint16_t sample_id,
                                         uint32_t page_index);

/*
 * Command API: queues or records explicit intent only.
 * Audio callers must never use these commands.
 */
uint8_t sample_page_cache_request_page(uint16_t sample_id, uint32_t page_index);
uint8_t sample_page_cache_request_page_ref(uint16_t sample_id,
                                           uint32_t page_index,
                                           sample_page_ref_t *out_ref);
uint8_t sample_page_cache_request_start_pages(uint16_t sample_id,
                                              uint32_t start_frame,
                                              uint32_t page_count);
uint8_t sample_page_cache_pin_page(uint16_t sample_id, uint32_t page_index);
void sample_page_cache_unpin_page(uint16_t sample_id, uint32_t page_index);
sample_page_load_result_t sample_page_cache_load_full_sample(uint16_t sample_id,
                                                             FIL *fp,
                                                             const wav_info_t *info,
                                                             uint32_t total_frames,
                                                             uint32_t data_offset,
                                                             uint8_t *io_buffer,
                                                             uint32_t io_buffer_size);
uint8_t sample_page_cache_register_stream_sample(uint16_t sample_id,
                                                 const char *path,
                                                 const wav_info_t *info,
                                                 uint32_t total_frames,
                                                 uint32_t data_offset);

/*
 * Service API: the only place where queued stream page loads may touch FatFs.
 * Must stay outside audio and be called only while the caller already owns
 * `sd_access_gate` for the sample-cache client.
 */
void sample_page_cache_service(uint32_t byte_budget);

#ifdef __cplusplus
}
#endif
