#pragma once

#include <stdint.h>

#include "Sampler/sample_pool.h"
#include "Storage/wav_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SAMPLE_CACHE_EMPTY = 0,
    SAMPLE_CACHE_PREPARING,
    SAMPLE_CACHE_PREFILLING,
    SAMPLE_CACHE_READY_FULL,
    SAMPLE_CACHE_READY_PARTIAL,
    SAMPLE_CACHE_PLAYING,
    SAMPLE_CACHE_UNDERRUN,
    SAMPLE_CACHE_DONE,
    SAMPLE_CACHE_NEEDS_REPREPARE,
    SAMPLE_CACHE_ERROR
} sample_cache_state_t;

typedef enum
{
    SAMPLE_CACHE_MODE_FULL = 0,
    SAMPLE_CACHE_MODE_STREAM
} sample_cache_mode_t;

typedef struct
{
    uint16_t sample_id;
    char path[SAMPLE_POOL_PATH_MAX];
    wav_info_t info;
    uint32_t total_frames;
    uint32_t data_offset;
    float *cache;
    uint32_t cache_capacity_frames;
    uint32_t cache_window_start_frame;
    uint32_t cache_valid_frames;
    uint32_t source_read_frame;
    uint32_t reprepare_start_frame;
    uint8_t fully_cached;
    uint8_t stream_active;
    sample_cache_mode_t mode;
    sample_cache_state_t state;
    uint8_t last_error;
} sample_cache_desc_t;

typedef struct
{
    uint8_t voice_id;
    uint16_t sample_id;
    uint32_t frame_pos;
    uint8_t active;
    uint8_t stop_on_underrun;
} sample_cache_voice_t;

typedef enum
{
    SAMPLE_CACHE_BLOCK_OK = 0,
    SAMPLE_CACHE_BLOCK_DONE,
    SAMPLE_CACHE_BLOCK_UNDERRUN,
    SAMPLE_CACHE_BLOCK_NOT_READY
} sample_cache_block_status_t;

typedef struct
{
    const float *l;
    const float *r;
    uint32_t frames;
    uint32_t frame_stride;
    uint8_t is_mono;
    sample_cache_block_status_t status;
} sample_cache_block_t;

void sample_cache_init(void);
void sample_cache_clear(uint16_t sample_id);
uint8_t sample_cache_prepare(uint16_t sample_id, const char *path);
void sample_cache_service(uint32_t byte_budget);
uint8_t sample_cache_is_ready(uint16_t sample_id);
sample_cache_state_t sample_cache_get_state(uint16_t sample_id);
uint8_t sample_cache_get_last_error(uint16_t sample_id);
uint8_t sample_cache_get_last_fresult(uint16_t sample_id);
uint8_t sample_cache_start_voice(uint16_t sample_id, uint8_t voice_id);
uint8_t sample_cache_start_voice_at(uint16_t sample_id, uint8_t voice_id, uint32_t frame_index);
uint8_t sample_cache_begin_read_block(uint8_t voice_id,
                                      uint32_t max_frames,
                                      sample_cache_block_t *out_block);
void sample_cache_commit_read_block(uint8_t voice_id, uint32_t consumed_frames);
uint32_t sample_cache_read_voice(uint8_t voice_id, float *out_l, float *out_r, uint32_t frames);
uint8_t sample_cache_read_voice_frame(uint8_t voice_id, uint32_t frame_index, float *out_l, float *out_r);
const float *sample_cache_get_legacy_data(uint16_t sample_id, uint32_t *out_frames);
void sample_cache_stop_voice(uint8_t voice_id);

#ifdef __cplusplus
}
#endif
