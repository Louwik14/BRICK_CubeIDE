#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_MAX_ACTIVE (16U)

#define SAMPLE_STREAM_ACTIVE_PAGE_NONE UINT32_MAX

typedef struct
{
    uint32_t last_page;
} sample_stream_active_state_t;

typedef struct
{
    sample_audio_key_t key;
    uint32_t current_frame;
    uint32_t end_frame;
    int8_t direction;
    uint8_t lookahead_pages;
    uint8_t request_current_page;
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
uint8_t sample_stream_manager_request_page_key(sample_audio_key_t key, uint32_t page_index);
uint8_t sample_stream_manager_request_presocle_page_key(sample_audio_key_t key, uint32_t page_index);
uint8_t sample_stream_manager_request_page(uint16_t sample_id, uint32_t page_index);
uint8_t sample_stream_manager_request_range_key(sample_audio_key_t key,
                                                uint32_t start_frame,
                                                uint32_t page_count);
uint8_t sample_stream_manager_request_range(uint16_t sample_id,
                                            uint32_t start_frame,
                                            uint32_t page_count);
void sample_stream_manager_active_state_reset(sample_stream_active_state_t *state);
uint8_t sample_stream_manager_queue_active_pages(const sample_stream_active_desc_t *desc);
void sample_stream_manager_service(uint32_t byte_budget);
uint8_t sample_stream_manager_has_pending_sd_work(void);

#ifdef __cplusplus
}
#endif
