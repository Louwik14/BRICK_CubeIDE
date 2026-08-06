#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_trace.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_MAX_ACTIVE (16U)
#define SAMPLE_STREAM_STEP_Q16_ONE (65536U)

typedef enum
{
    SAMPLE_STREAM_OWNER_NONE = 0,
    SAMPLE_STREAM_OWNER_CLASSIC_CACHE_VOICE,
    SAMPLE_STREAM_OWNER_MULTI_VOICE,
    SAMPLE_STREAM_OWNER_MULTI_LOOP
} sample_stream_owner_kind_t;

#define SAMPLE_STREAM_ACTIVE_PAGE_NONE UINT32_MAX

typedef struct
{
    uint32_t last_urgent_page;
    uint32_t last_normal_page;
} sample_stream_active_state_t;

typedef struct
{
    sample_audio_key_t key;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint32_t current_frame;
    uint32_t end_frame;
    uint32_t step_q16;
    uint32_t horizon_frames;
    int8_t direction;
    uint8_t lookahead_pages;
    uint8_t request_current_page;
    uint8_t role;
    uint8_t owner_kind;
    uint8_t owner_id;
    uint32_t owner_generation;
    sample_stream_active_state_t *state;
} sample_stream_active_desc_t;

/*
 * STREAM facade.
 *
 * Owns Sampler STREAM SD policy and persistent readers. service() must stay
 * outside audio IRQ and be called only while the sample-cache SD gate is held.
 */
void sample_stream_manager_init(void);
void sample_stream_manager_reset(void);
void sample_stream_manager_release_key(sample_audio_key_t key);
void sample_stream_manager_release_sample(uint16_t sample_id);
void sample_stream_manager_release_owner(uint8_t owner_kind,
                                         uint8_t owner_id,
                                         uint32_t owner_generation);
uint8_t sample_stream_manager_request_page_key(sample_audio_key_t key, uint32_t page_index);
uint8_t sample_stream_manager_request_page_key_alloc(sample_audio_key_t key,
                                                     uint32_t page_index,
                                                     sample_page_alloc_type_t alloc_type);
uint8_t sample_stream_manager_request_page(uint16_t sample_id, uint32_t page_index);
uint8_t sample_stream_manager_request_range_key(sample_audio_key_t key,
                                                uint32_t start_frame,
                                                uint32_t page_count);
uint8_t sample_stream_manager_request_range_key_alloc(sample_audio_key_t key,
                                                      uint32_t start_frame,
                                                      uint32_t page_count,
                                                      sample_page_alloc_type_t alloc_type);
uint8_t sample_stream_manager_request_range(uint16_t sample_id,
                                            uint32_t start_frame,
                                            uint32_t page_count);
void sample_stream_manager_active_state_reset(sample_stream_active_state_t *state);
uint8_t sample_stream_manager_queue_active_pages(const sample_stream_active_desc_t *desc);
uint8_t sample_stream_manager_reserve_active_pages(const sample_stream_active_desc_t *desc);
void sample_stream_manager_service(uint32_t byte_budget);
uint8_t sample_stream_manager_has_pending_sd_work(void);
void sample_stream_manager_trace_consume_miss(sample_audio_key_t key,
                                              uint32_t page_index,
                                              uint32_t reader_position,
                                              uint32_t frames_remaining);
#if defined(BRICK6_MULTI_STREAM_DIAG)
void sample_stream_manager_get_debug_stats(uint8_t current_owner_kind,
                                           uint8_t current_owner_id,
                                           uint32_t current_generation,
                                           uint8_t loop_owner_kind,
                                           uint8_t loop_owner_id,
                                           uint32_t loop_generation,
                                           uint32_t *out_pending_global,
                                           uint32_t *out_pending_current_owner,
                                           uint32_t *out_pending_loop_owner,
                                           uint32_t *out_readers_active);
#endif

#ifdef __cplusplus
}
#endif
