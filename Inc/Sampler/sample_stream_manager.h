#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_snapshot.h"
#include "Sampler/sample_stream_trace.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_STEP_Q16_ONE (65536U)

/*
 * STREAM facade.
 *
 * Owns Sampler STREAM request policy and scheduling. The dedicated I/O stage
 * owns persistent readers. service() stays outside audio IRQ and is called only
 * while the sample-cache SD gate is held.
 */
void sample_stream_manager_init(void);
void sample_stream_manager_reset(void);
void sample_stream_manager_release_key(sample_audio_key_t key);
void sample_stream_manager_release_sample(uint16_t sample_id);
void sample_stream_manager_service(uint32_t byte_budget);
uint8_t sample_stream_manager_has_pending_sd_work(void);
uint8_t sample_stream_manager_io_in_flight(void);
void sample_stream_manager_note_blocked_poll(uint8_t multi_blocked,
                                             uint8_t bulk_blocked,
                                             uint32_t elapsed_frames);
void sample_stream_manager_trace_consume_miss(sample_audio_key_t key,
                                              uint32_t page_index,
                                              uint32_t reader_position,
                                              uint32_t frames_remaining);
#if defined(BRICK6_STREAM_CALIBRATION) && BRICK6_STREAM_CALIBRATION
void sample_stream_manager_calibration_set_voice_context(uint8_t voice_id,
                                                         uint32_t generation);
void sample_stream_manager_calibration_clear_voice_context(void);
#endif
#if defined(BRICK6_MULTI_STREAM_DIAG)
void sample_stream_manager_get_debug_stats(uint32_t *out_active_needs,
                                           uint32_t *out_readers_active);
#endif

#ifdef __cplusplus
}
#endif
