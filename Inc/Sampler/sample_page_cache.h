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

typedef enum
{
    SAMPLE_AUDIO_DOMAIN_CLASSIC = 0,
    SAMPLE_AUDIO_DOMAIN_LOOPER,
    SAMPLE_AUDIO_DOMAIN_MULTI
} sample_audio_domain_t;

typedef struct
{
    sample_audio_domain_t domain;
    uint16_t object_id;
} sample_audio_key_t;

typedef struct
{
    sample_audio_key_t key;
    uint16_t sample_id;
    uint16_t reserved;
    uint32_t page_index;
    uint32_t start_frame;
    uint32_t frame_count;
    float *data;
    sample_page_state_t state;
    uint16_t pin_count;
    uint16_t use_count;
    uint16_t window_pin_count;
    uint16_t reserved2;
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

typedef struct
{
    sample_audio_key_t key;
    char path[SAMPLE_PAGE_CACHE_PATH_MAX];
    wav_info_t info;
    uint32_t total_frames;
    uint32_t data_offset;
    uint8_t raw_pcm24;
} sample_page_stream_info_t;

typedef struct
{
    sample_audio_key_t key;
    uint16_t sample_id;
    uint16_t slot_index;
    uint32_t page_index;
    uint32_t start_frame;
    uint32_t frame_count;
    float *frames_interleaved;
} sample_page_load_target_t;

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

typedef struct
{
    uint32_t lookup_hits;
    uint32_t lookup_misses;
    uint32_t max_lookup_scan;
    uint32_t max_free_scan;
    uint32_t max_evict_scan;
    uint32_t evict_fail;
    uint32_t bounded_scan_yield;
} sample_page_cache_diag_snapshot_t;

/*
 * Query API: RAM-only, no SD side effect, no implicit page request.
 */
void sample_page_cache_init(void);
void sample_page_cache_reset(void);
void sample_page_cache_diag_get_snapshot(sample_page_cache_diag_snapshot_t *out_snapshot);
sample_audio_key_t sample_audio_key_classic(uint16_t sample_id);
sample_audio_key_t sample_audio_key_looper(uint16_t looper_id);
sample_audio_key_t sample_audio_key_multi(uint16_t multi_sample_id);
uint8_t sample_audio_key_equal(const sample_audio_key_t *a, const sample_audio_key_t *b);
void sample_page_cache_clear_key(sample_audio_key_t key);
void sample_page_cache_clear_sample(uint16_t sample_id);
sample_page_state_t sample_page_cache_get_page_state_key(sample_audio_key_t key,
                                                         uint32_t page_index);
sample_page_state_t sample_page_cache_get_page_state(uint16_t sample_id, uint32_t page_index);
const sample_page_desc_t *sample_page_cache_get_page_desc(uint32_t slot_index);
uint8_t sample_page_cache_try_acquire_page_key(sample_audio_key_t key,
                                               uint32_t page_index,
                                               sample_page_span_t *out_span);
uint8_t sample_page_cache_try_acquire_page(uint16_t sample_id,
                                           uint32_t page_index,
                                           sample_page_span_t *out_span);
uint8_t sample_page_cache_try_acquire_page_ref_key(sample_audio_key_t key,
                                                   const sample_page_ref_t *ref,
                                                   sample_page_span_t *out_span);
uint8_t sample_page_cache_try_acquire_page_ref(uint16_t sample_id,
                                               const sample_page_ref_t *ref,
                                               sample_page_span_t *out_span);
void sample_page_cache_release_page_key(sample_audio_key_t key, uint32_t page_index);
void sample_page_cache_release_page(uint16_t sample_id, uint32_t page_index);
void sample_page_cache_release_page_ref_key(sample_audio_key_t key, const sample_page_ref_t *ref);
void sample_page_cache_release_page_ref(uint16_t sample_id, const sample_page_ref_t *ref);
const float *sample_page_cache_get_full_sample_base_key(sample_audio_key_t key,
                                                        uint32_t *out_frames);
const float *sample_page_cache_get_full_sample_base(uint16_t sample_id, uint32_t *out_frames);
uint8_t sample_page_cache_begin_read_block_key(sample_audio_key_t key,
                                               uint32_t frame_index,
                                               uint32_t max_frames,
                                               sample_page_block_t *out_block);
uint8_t sample_page_cache_begin_read_block(uint16_t sample_id,
                                           uint32_t frame_index,
                                           uint32_t max_frames,
                                           sample_page_block_t *out_block);
void sample_page_cache_commit_read_block_key(sample_audio_key_t key,
                                             uint32_t page_index);
void sample_page_cache_commit_read_block(uint16_t sample_id,
                                         uint32_t page_index);
uint8_t sample_page_cache_has_queued_range(uint16_t first_sample_id,
                                           uint16_t sample_count);
uint8_t sample_page_cache_has_queued_domain_range(sample_audio_domain_t domain,
                                                  uint16_t first_object_id,
                                                  uint16_t object_count);
uint8_t sample_page_cache_get_stream_info(uint16_t sample_id,
                                          sample_page_stream_info_t *out_info);
uint8_t sample_page_cache_get_stream_info_key(sample_audio_key_t key,
                                              sample_page_stream_info_t *out_info);
uint8_t sample_page_cache_find_queued_load_target(uint16_t first_sample_id,
                                                  uint16_t sample_count,
                                                  sample_page_load_target_t *out_target);
uint8_t sample_page_cache_get_load_target(uint16_t sample_id,
                                          uint32_t page_index,
                                          sample_page_load_target_t *out_target);
uint8_t sample_page_cache_get_load_target_key(sample_audio_key_t key,
                                              uint32_t page_index,
                                              sample_page_load_target_t *out_target);
uint8_t sample_page_cache_set_page_state(uint16_t sample_id,
                                         uint32_t page_index,
                                         sample_page_state_t state);
uint8_t sample_page_cache_set_page_state_key(sample_audio_key_t key,
                                             uint32_t page_index,
                                             sample_page_state_t state);
uint8_t sample_page_cache_acquire_window_page_key(sample_audio_key_t key,
                                                  uint32_t page_index,
                                                  uint8_t owner_kind,
                                                  uint8_t owner_id,
                                                  uint32_t owner_generation);
void sample_page_cache_release_window_owner(uint8_t owner_kind,
                                            uint8_t owner_id,
                                            uint32_t owner_generation);
void sample_page_cache_release_window_owner_outside_key(uint8_t owner_kind,
                                                        uint8_t owner_id,
                                                        uint32_t owner_generation,
                                                        sample_audio_key_t key,
                                                        uint32_t first_page,
                                                        uint32_t last_page);
uint8_t sample_page_cache_has_window_locks(void);

/*
 * Command API: queues or records explicit intent only.
 * Audio callers must never use these commands.
 */
uint8_t sample_page_cache_request_page(uint16_t sample_id, uint32_t page_index);
uint8_t sample_page_cache_request_page_key(sample_audio_key_t key, uint32_t page_index);
uint8_t sample_page_cache_request_page_ref(uint16_t sample_id,
                                           uint32_t page_index,
                                           sample_page_ref_t *out_ref);
uint8_t sample_page_cache_request_page_ref_key(sample_audio_key_t key,
                                               uint32_t page_index,
                                               sample_page_ref_t *out_ref);
uint8_t sample_page_cache_request_start_pages(uint16_t sample_id,
                                              uint32_t start_frame,
                                              uint32_t page_count);
uint8_t sample_page_cache_request_start_pages_key(sample_audio_key_t key,
                                                  uint32_t start_frame,
                                                  uint32_t page_count);
uint8_t sample_page_cache_pin_page(uint16_t sample_id, uint32_t page_index);
uint8_t sample_page_cache_pin_page_key(sample_audio_key_t key, uint32_t page_index);
void sample_page_cache_unpin_page(uint16_t sample_id, uint32_t page_index);
void sample_page_cache_unpin_page_key(sample_audio_key_t key, uint32_t page_index);
sample_page_load_result_t sample_page_cache_load_full_sample(uint16_t sample_id,
                                                             FIL *fp,
                                                             const wav_info_t *info,
                                                             uint32_t total_frames,
                                                             uint32_t data_offset,
                                                             uint8_t *io_buffer,
                                                             uint32_t io_buffer_size);
sample_page_load_result_t sample_page_cache_load_full_sample_key(sample_audio_key_t key,
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
uint8_t sample_page_cache_register_stream_sample_key(sample_audio_key_t key,
                                                     const char *path,
                                                     const wav_info_t *info,
                                                     uint32_t total_frames,
                                                     uint32_t data_offset);
uint8_t sample_page_cache_register_raw_pcm24_stereo_sample(uint16_t sample_id,
                                                           const char *path,
                                                           uint32_t total_frames);
uint8_t sample_page_cache_register_raw_pcm24_stereo_sample_key(sample_audio_key_t key,
                                                               const char *path,
                                                               uint32_t total_frames);

/*
 * Legacy/transient range service kept for non-Sampler-pool users.
 * Sampler STREAM pool service is owned by sample_stream_manager.
 */
void sample_page_cache_service_range(uint16_t first_sample_id,
                                     uint16_t sample_count,
                                     uint32_t byte_budget);
void sample_page_cache_service_domain_range(sample_audio_domain_t domain,
                                            uint16_t first_object_id,
                                            uint16_t object_count,
                                            uint32_t byte_budget);

#ifdef __cplusplus
}
#endif
