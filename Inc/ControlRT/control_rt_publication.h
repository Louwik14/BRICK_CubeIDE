#ifndef BRICK6_CONTROL_RT_PUBLICATION_H
#define BRICK6_CONTROL_RT_PUBLICATION_H

#include <stdint.h>

#include "IPC/control_audio_command.h"

typedef enum
{
    CONTROL_RT_DEBUG_REJECT_NONE = 0U,
    CONTROL_RT_DEBUG_REJECT_BATCH_NOW_ARGUMENT = 1U,
    CONTROL_RT_DEBUG_REJECT_BATCH_NOW_CLOCK = 2U,
    CONTROL_RT_DEBUG_REJECT_BATCH_SCHEDULED_ARGUMENT = 3U,
    CONTROL_RT_DEBUG_REJECT_BATCH_SCHEDULED_HORIZON_CAPACITY = 4U,
    CONTROL_RT_DEBUG_REJECT_STAGE_ARGUMENT = 5U,
    CONTROL_RT_DEBUG_REJECT_STAGE_CAPACITY = 6U,
    CONTROL_RT_DEBUG_REJECT_STAGE_TIMESTAMP = 7U
} control_rt_debug_reject_reason_t;

typedef enum
{
    CONTROL_RT_PUBLISH_DEBUG_NONE = 0U,
    CONTROL_RT_PUBLISH_DEBUG_SUPPRESSED_INVALID_ARGS = 1U,
    CONTROL_RT_PUBLISH_DEBUG_HORIZON_INSUFFICIENT_FREE = 2U,
    CONTROL_RT_PUBLISH_DEBUG_HORIZON_STAGE_REJECT = 3U,
    CONTROL_RT_PUBLISH_DEBUG_FIFO_DIRECT_REJECT = 4U
} control_rt_publish_debug_reason_t;

extern volatile uint32_t g_control_rt_publish_debug_reason;
extern volatile uint32_t g_control_rt_publish_debug_count;
extern volatile uint32_t g_control_rt_publish_debug_suppressed;
extern volatile uint32_t g_control_rt_publish_debug_horizon_active;
extern volatile uint32_t g_control_rt_publish_debug_horizon_free;
extern volatile uint32_t g_control_rt_publish_debug_stage_result;
extern volatile uint32_t g_control_rt_publish_debug_fifo_result;

extern volatile uint32_t g_control_rt_debug_reject_reason;
extern volatile uint8_t g_control_rt_debug_horizon_active;
extern volatile uint16_t g_control_rt_debug_horizon_used;
extern volatile uint16_t g_control_rt_debug_horizon_limit;
extern volatile uint8_t g_control_rt_debug_suppressed;
extern volatile uint64_t g_control_rt_debug_command_timestamp;
extern volatile uint64_t g_control_rt_debug_first_unpublished;

/*
 * CONTROL_RT is the sole CONTROL-side authority for publication of functional
 * CONTROL -> AUDIO commands. It owns publication mechanics and FIFO commit,
 * but never owns a product value.
 */
void control_rt_publication_init(void);
void control_rt_publication_suppress_begin(void);
void control_rt_publication_suppress_end(void);
uint8_t control_rt_publication_horizon_active(void);
uint8_t control_rt_now_sample(uint64_t *out_sample_time);
uint8_t control_rt_publication_resolve_asap_sample(
    uint64_t minimum_sample, uint64_t *out_sample_time);
uint8_t control_rt_capture_tick_to_sample(uint32_t capture_tick,
                                          uint64_t minimum_sample,
                                          uint64_t *out_sample_time);
uint8_t control_rt_publish_batch_scheduled(
    const control_audio_command_t *commands, uint16_t count);
uint8_t control_rt_publish_batch_captured(control_audio_command_t *commands,
                                          uint16_t count,
                                          uint32_t capture_tick,
                                          uint64_t minimum_sample);
uint8_t control_rt_publish_batch_now(control_audio_command_t *commands,
                                     uint16_t count);
uint8_t control_rt_publication_begin_horizon(uint64_t first_sample,
                                             uint16_t frames);
void control_rt_publication_abort_horizon(void);
uint8_t control_rt_publication_commit_horizon(void);
uint16_t control_rt_publication_free(void);
uint32_t control_rt_publication_capacity_failure_count(void);
uint8_t control_rt_publish_program(uint8_t entity, uint32_t descriptor,
                                   uint64_t sample_time);
uint8_t control_rt_publish_param(uint8_t entity, uint16_t param_id,
                                 uint32_t value, uint32_t target_detail,
                                 uint64_t sample_time);
uint8_t control_rt_publish_param_now(uint8_t entity, uint16_t param_id,
                                     uint32_t value, uint32_t target_detail);
uint8_t control_rt_publish_note(uint8_t entity, uint8_t kind,
                                uint32_t output_id, uint8_t note,
                                uint8_t velocity, uint64_t sample_time);
uint8_t control_rt_publish_record(uint8_t kind, uint32_t session_id,
                                  uint32_t config, uint8_t client,
                                  uint64_t sample_time);
uint8_t control_rt_publish_panic(uint8_t kind, uint8_t entity,
                                 uint64_t sample_time);
uint8_t control_rt_publish_audio_state_commit(uint32_t generation);

#endif /* BRICK6_CONTROL_RT_PUBLICATION_H */
