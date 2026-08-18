#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_snapshot.h"

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

#ifdef __cplusplus
}
#endif
