#pragma once

#include <stdint.h>

#include "Sampler/sample_play_plan.h"
#include "Sampler/sample_classic_config.h"
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
    SAMPLE_CACHE_ERROR
} sample_cache_state_t;

typedef enum
{
    SAMPLE_CACHE_SLOT_EMPTY = 0,
    SAMPLE_CACHE_SLOT_PREPARING,
    SAMPLE_CACHE_SLOT_START_PENDING,
    SAMPLE_CACHE_SLOT_PLAYABLE,
    SAMPLE_CACHE_SLOT_ERROR
} sample_cache_slot_readiness_t;

typedef enum
{
    SAMPLE_CACHE_MODE_FULL = 0,
    SAMPLE_CACHE_MODE_STREAM
} sample_cache_mode_t;

typedef struct sample_cache_desc
{
    wav_info_t info;
    uint32_t total_frames;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    float *cache;
    uint32_t cache_capacity_frames;
    uint32_t cache_window_start_frame;
    uint32_t cache_valid_frames;
    uint32_t loaded_frames;
    uint8_t fully_cached;
    sample_cache_mode_t mode;
    sample_cache_state_t state;
    uint8_t last_error;
} sample_cache_desc_t;

void sample_cache_init(void);
void sample_cache_clear(uint16_t sample_id);
uint8_t sample_cache_wav_format_supported(const wav_info_t *info);
uint8_t sample_cache_prepare(uint16_t sample_id, const char *path);
void sample_cache_service(uint32_t byte_budget);
uint8_t sample_cache_has_pending_sd_work(void);
uint8_t sample_cache_is_ready(uint16_t sample_id);
sample_cache_slot_readiness_t sample_cache_get_slot_readiness(uint16_t sample_id);
sample_cache_state_t sample_cache_get_state(uint16_t sample_id);
uint8_t sample_cache_get_last_error(uint16_t sample_id);
uint8_t sample_cache_get_last_fresult(uint16_t sample_id);
uint8_t sample_cache_resolve_classic_source(uint16_t sample_id,
                                            sample_resolved_source_t *out_source);
uint8_t sample_cache_peek_frame(uint16_t sample_id, uint32_t frame_index, float *out_l, float *out_r);

#ifdef __cplusplus
}
#endif
