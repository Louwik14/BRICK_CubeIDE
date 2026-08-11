#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_page_cache_config.h"
#include "Sampler/sample_pool.h"
#include "Sampler/sample_stream_fatfs_map.h"
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
    SAMPLE_PAGE_FREE = 0,
    SAMPLE_PAGE_RESERVED,
    SAMPLE_PAGE_LOADING,
    SAMPLE_PAGE_READY,
    SAMPLE_PAGE_FAILED
} sample_page_state_t;

typedef struct
{
    sample_audio_key_t key;
    uint16_t sample_id;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t page_index;
    uint32_t start_frame;
    uint32_t frame_count;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    float *data;
    volatile sample_page_state_t state;
    uint16_t pin_count;
    uint16_t use_count;
    uint16_t reserved;
    uint32_t generation;
    uint8_t load_cancel_requested;
    uint8_t lifecycle_reserved[3];
    uint32_t last_touch;
} sample_page_desc_t;

typedef struct
{
    const float *frames_interleaved;
    uint32_t frame_count;
    uint32_t start_frame;
    uint32_t page_index;
    uint32_t page_generation;
    sample_audio_key_t key;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint32_t slot_index;
} sample_page_span_t;

typedef struct
{
    sample_audio_key_t key;
    uint32_t page_index;
    uint32_t page_generation;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint32_t slot_index;
} sample_page_ref_t;

typedef struct
{
    uint32_t page_index;
    uint32_t generation;
    uint16_t slot_index;
    uint16_t frame_count;
    uint16_t use_count;
    sample_page_state_t state;
} sample_page_window_debug_t;

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
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    sample_page_block_status_t status;
} sample_page_block_t;

typedef struct
{
    sample_audio_key_t key;
    char path[SAMPLE_PAGE_CACHE_PATH_MAX];
    wav_info_t info;
    uint32_t total_frames;
    uint32_t data_offset;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    sample_stream_safe_metadata_t stream_safe;
    uint8_t physical_only;
} sample_page_stream_info_t;

typedef struct
{
    sample_audio_key_t key;
    uint16_t sample_id;
    uint16_t slot_index;
    uint32_t page_index;
    uint32_t start_frame;
    uint32_t frame_count;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint32_t page_generation;
    float *frames_interleaved;
} sample_page_load_target_t;

typedef struct
{
    sample_audio_key_t key;
    uint32_t page_index;
    uint32_t page_generation;
    uint32_t registration_epoch;
    uint16_t slot_index;
    uint16_t reserved;
} sample_page_load_token_t;

typedef enum
{
    SAMPLE_PAGE_FINISH_READY = 0,
    SAMPLE_PAGE_FINISH_ERROR
} sample_page_finish_result_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(sample_page_load_token_t) == 20U,
               "page load token ABI must remain 20 bytes");
#endif

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

typedef enum
{
    SAMPLE_PAGE_ALLOC_LEGACY_DEFAULT = 0,
    SAMPLE_PAGE_ALLOC_SLOT_PERMANENT,
    SAMPLE_PAGE_ALLOC_VOICE_WINDOW,
    SAMPLE_PAGE_ALLOC_MARGIN
} sample_page_alloc_type_t;

typedef struct
{
    uint16_t first_slot;
    uint16_t page_count;
    uint32_t capacity_bytes;
    void *data;
} sample_page_raw_allocation_t;

/*
 * Query API: RAM-only, no SD side effect, no implicit page request.
 */
void sample_page_cache_init(void);
void sample_page_cache_reset(void);
sample_audio_key_t sample_audio_key_classic(uint16_t sample_id);
sample_audio_key_t sample_audio_key_looper(uint16_t looper_id);
sample_audio_key_t sample_audio_key_multi(uint16_t multi_sample_id);
uint8_t sample_audio_key_equal(const sample_audio_key_t *a, const sample_audio_key_t *b);
void sample_page_cache_clear_key(sample_audio_key_t key);
void sample_page_cache_clear_sample(uint16_t sample_id);
uint8_t sample_page_cache_cancel_reserved_page_key(sample_audio_key_t key,
                                                 uint32_t page_index,
                                                 uint8_t reason);
uint32_t sample_page_cache_cancel_reserved_key(sample_audio_key_t key,
                                             uint8_t reason);
uint32_t sample_page_cache_cancel_reserved_domain(sample_audio_domain_t domain,
                                                uint8_t reason);
sample_page_state_t sample_page_cache_get_page_state_key(sample_audio_key_t key,
                                                         uint32_t page_index);
uint8_t sample_page_cache_page_exists_key(sample_audio_key_t key,
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
uint8_t sample_page_cache_alloc_slot_pool_bytes(uint32_t bytes,
                                                sample_page_raw_allocation_t *out_allocation);
void sample_page_cache_release_slot_pool_allocation(uint16_t first_slot,
                                                    uint16_t page_count);
uint32_t sample_page_cache_slot_pool_total_bytes(void);
uint32_t sample_page_cache_slot_pool_free_bytes(void);
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
uint8_t sample_page_cache_has_reserved_range(uint16_t first_sample_id,
                                           uint16_t sample_count);
uint8_t sample_page_cache_has_reserved_domain_range(sample_audio_domain_t domain,
                                                  uint16_t first_object_id,
                                                  uint16_t object_count);
uint8_t sample_page_cache_get_stream_info(uint16_t sample_id,
                                          sample_page_stream_info_t *out_info);
uint8_t sample_page_cache_get_stream_info_key(sample_audio_key_t key,
                                              sample_page_stream_info_t *out_info);
uint8_t sample_page_cache_get_load_target(uint16_t sample_id,
                                          uint32_t page_index,
                                          sample_page_load_target_t *out_target);
uint8_t sample_page_cache_get_load_target_key(sample_audio_key_t key,
                                              uint32_t page_index,
                                              sample_page_load_target_t *out_target);
uint8_t sample_page_cache_begin_loading(const sample_page_load_target_t *target,
                                          sample_page_load_token_t *out_token);
uint8_t sample_page_cache_resolve_loading_target(const sample_page_load_token_t *token,
                                                 sample_page_load_target_t *out_target);
uint8_t sample_page_cache_finish_loading(const sample_page_load_token_t *token,
                                           sample_page_finish_result_t result);
uint8_t sample_page_cache_cancel_loading_key(sample_audio_key_t key,
                                               uint32_t page_index);
uint8_t sample_page_cache_prepare_bulk_page_key_alloc(
    sample_audio_key_t key,
    uint32_t page_index,
    sample_page_alloc_type_t alloc_type);
uint8_t sample_page_cache_get_bulk_load_target_key(
    sample_audio_key_t key,
    uint32_t page_index,
    sample_page_load_target_t *out_target);
uint8_t sample_page_cache_set_page_state(uint16_t sample_id,
                                         uint32_t page_index,
                                         sample_page_state_t state);
uint8_t sample_page_cache_set_page_state_key(sample_audio_key_t key,
                                             uint32_t page_index,
                                             sample_page_state_t state);
#if defined(BRICK6_MULTI_STREAM_DIAG)
uint8_t sample_page_cache_get_window_page_debug(sample_audio_key_t key,
                                                uint32_t page_index,
                                                sample_page_window_debug_t *out_debug);
uint32_t sample_page_cache_debug_count_free_pages(void);
#endif

/*
 * Physical reservation API: records explicit cache storage intent only.
 * These calls never create or mutate a logical voice need.
 */
uint8_t sample_page_cache_reserve_page(uint16_t sample_id, uint32_t page_index);
uint8_t sample_page_cache_reserve_page_key(sample_audio_key_t key, uint32_t page_index);
uint8_t sample_page_cache_reserve_page_key_alloc(sample_audio_key_t key,
                                                 uint32_t page_index,
                                                 sample_page_alloc_type_t alloc_type);
uint8_t sample_page_cache_reserve_page_ref(uint16_t sample_id,
                                           uint32_t page_index,
                                           sample_page_ref_t *out_ref);
uint8_t sample_page_cache_reserve_page_ref_key(sample_audio_key_t key,
                                               uint32_t page_index,
                                               sample_page_ref_t *out_ref);
uint8_t sample_page_cache_reserve_start_pages(uint16_t sample_id,
                                              uint32_t start_frame,
                                              uint32_t page_count);
uint8_t sample_page_cache_reserve_start_pages_key(sample_audio_key_t key,
                                                  uint32_t start_frame,
                                                  uint32_t page_count);
uint8_t sample_page_cache_reserve_start_pages_key_alloc(sample_audio_key_t key,
                                                        uint32_t start_frame,
                                                        uint32_t page_count,
                                                        sample_page_alloc_type_t alloc_type);
uint8_t sample_page_cache_pin_page(uint16_t sample_id, uint32_t page_index);
uint8_t sample_page_cache_pin_page_key(sample_audio_key_t key, uint32_t page_index);
uint8_t sample_page_cache_pin_page_key_alloc(sample_audio_key_t key,
                                             uint32_t page_index,
                                             sample_page_alloc_type_t alloc_type);
void sample_page_cache_unpin_page(uint16_t sample_id, uint32_t page_index);
void sample_page_cache_unpin_page_key(sample_audio_key_t key, uint32_t page_index);
void sample_page_cache_unpin_page_ref_key(sample_audio_key_t key,
                                          const sample_page_ref_t *ref);
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
sample_page_load_result_t sample_page_cache_load_full_sample_key_alloc(
    sample_audio_key_t key,
    FIL *fp,
    const wav_info_t *info,
    uint32_t total_frames,
    uint32_t data_offset,
    uint8_t *io_buffer,
    uint32_t io_buffer_size,
    sample_page_alloc_type_t alloc_type);
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
uint8_t sample_page_cache_register_stream_sample_key_from_file(sample_audio_key_t key,
                                                               const char *path,
                                                               const wav_info_t *info,
                                                               uint32_t total_frames,
                                                               uint32_t data_offset,
                                                               FIL *map_file);
uint8_t sample_page_cache_register_stream_sample_key_no_map(
    sample_audio_key_t key,
    const char *path,
    const wav_info_t *info,
    uint32_t total_frames,
    uint32_t data_offset);
uint8_t sample_page_cache_register_live_pcm24_stereo_sample_key(
    sample_audio_key_t key,
    const char *path,
    uint32_t total_frames,
    uint32_t readable_frames,
    uint32_t data_offset,
    uint32_t file_size,
    const sample_stream_physical_extent_t *extents,
    uint16_t extent_count,
    uint32_t media_epoch);
uint8_t sample_page_cache_update_readable_frames_key(sample_audio_key_t key,
                                                      uint32_t readable_frames);
uint8_t sample_page_cache_update_stream_path_key(sample_audio_key_t key,
                                                  const char *path);

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
