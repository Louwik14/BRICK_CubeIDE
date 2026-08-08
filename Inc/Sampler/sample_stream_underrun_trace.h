#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_arch_contract.h"
#include "Sampler/sample_stream_io.h"
#include "Sampler/sample_stream_scheduler.h"
#include "Sampler/sample_stream_snapshot.h"

#ifndef BRICK6_STREAM_UNDERRUN_TRACE
#define BRICK6_STREAM_UNDERRUN_TRACE (0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_STREAM_UNDERRUN_TRACE_MAGIC       (0x53555254UL)
#define BRICK6_STREAM_UNDERRUN_TRACE_ABI_VERSION (1U)
#define BRICK6_STREAM_UNDERRUN_TRACE_CAPACITY    (1024U)

typedef enum
{
    BRICK6_STREAM_TRACE_VOICE_STATE = 0,
    BRICK6_STREAM_TRACE_NEED_CREATED,
    BRICK6_STREAM_TRACE_NEED_SELECTABLE,
    BRICK6_STREAM_TRACE_SCHEDULER_DECISION,
    BRICK6_STREAM_TRACE_LOAD_BEGIN,
    BRICK6_STREAM_TRACE_IO_BEGIN,
    BRICK6_STREAM_TRACE_IO_END,
    BRICK6_STREAM_TRACE_LOAD_END,
    BRICK6_STREAM_TRACE_READY,
    BRICK6_STREAM_TRACE_CONSUME_MISS,
    BRICK6_STREAM_TRACE_PAGE_STATE,
    BRICK6_STREAM_TRACE_SERVICE_BEGIN,
    BRICK6_STREAM_TRACE_SERVICE_BLOCKED,
    BRICK6_STREAM_TRACE_SERVICE_END,
    BRICK6_STREAM_TRACE_MANAGER_END
} brick6_stream_underrun_trace_type_t;

typedef enum
{
    BRICK6_STREAM_TRACE_REASON_NONE = 0,
    BRICK6_STREAM_TRACE_REASON_NO_ACTIVE_NEED,
    BRICK6_STREAM_TRACE_REASON_ALL_READY,
    BRICK6_STREAM_TRACE_REASON_ALL_LOADING,
    BRICK6_STREAM_TRACE_REASON_NO_CANDIDATE,
    BRICK6_STREAM_TRACE_REASON_RESERVE_FAILED,
    BRICK6_STREAM_TRACE_REASON_TARGET_FAILED,
    BRICK6_STREAM_TRACE_REASON_EPOCH_MISMATCH,
    BRICK6_STREAM_TRACE_REASON_ZERO_BUDGET,
    BRICK6_STREAM_TRACE_REASON_SERVICE_BYTE_BUDGET,
    BRICK6_STREAM_TRACE_REASON_SERVICE_PAGE_LIMIT,
    BRICK6_STREAM_TRACE_REASON_SERVICE_FATFS_LIMIT,
    BRICK6_STREAM_TRACE_REASON_SERVICE_TICK_LIMIT,
    BRICK6_STREAM_TRACE_REASON_MULTI_BULK_BLOCKED,
    BRICK6_STREAM_TRACE_REASON_GATE_BLOCKED,
    BRICK6_STREAM_TRACE_REASON_LOAD_ERROR,
    BRICK6_STREAM_TRACE_REASON_PUBLISH_ERROR
} brick6_stream_underrun_trace_reason_t;

typedef struct
{
    uint32_t sequence;
    uint32_t audio_frame_low;
    uint32_t audio_frame_high;
    uint32_t cycle;
    uint32_t duration_cycles;
    uint32_t key_domain;
    uint16_t key_object_id;
    uint16_t key_reserved;
    uint32_t page_index;
    uint32_t generation;
    uint32_t registration_epoch;
    uint32_t value0;
    uint32_t value1;
    uint32_t value2;
    uint32_t value3;
    uint8_t type;
    uint8_t source;
    uint8_t voice_id;
    uint8_t state;
    uint8_t reason;
    uint8_t backend;
    uint8_t result;
    uint8_t reserved;
} brick6_stream_underrun_trace_event_t;

typedef struct
{
    uint32_t magic;
    uint32_t abi_version;
    uint32_t event_size;
    uint32_t capacity;
    uint32_t write_index;
    uint32_t count;
    uint32_t dropped_count;
    uint32_t last_miss_sequence;
    brick6_stream_underrun_trace_event_t events[BRICK6_STREAM_UNDERRUN_TRACE_CAPACITY];
} brick6_stream_underrun_trace_snapshot_t;

#if BRICK6_STREAM_UNDERRUN_TRACE
extern volatile brick6_stream_underrun_trace_snapshot_t
    g_brick6_stream_underrun_trace;

void brick6_stream_underrun_trace_reset(void);
void brick6_stream_underrun_trace_voice_state(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    const sample_stream_snapshot_t *snapshot,
    const sample_stream_target_voice_registry_entry_t *entry);
void brick6_stream_underrun_trace_need_created(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    uint32_t generation,
    const sample_stream_target_voice_need_t *need,
    uint32_t frames_ahead);
void brick6_stream_underrun_trace_need_selectable(
    const sample_stream_scheduler_candidate_t *candidate,
    sample_page_state_t state,
    uint32_t candidate_count);
void brick6_stream_underrun_trace_scheduler(
    const sample_stream_scheduler_candidate_t *candidate,
    const sample_stream_scheduler_decision_t *decision,
    uint32_t candidate_count,
    uint32_t critical_voices,
    uint32_t loadable_needs,
    uint8_t reason);
void brick6_stream_underrun_trace_load_begin(
    const sample_stream_scheduler_candidate_t *candidate,
    const sample_page_load_target_t *target,
    const sample_page_load_token_t *token);
void brick6_stream_underrun_trace_io_begin(
    const sample_stream_scheduler_candidate_t *candidate,
    const sample_stream_io_command_t *command);
void brick6_stream_underrun_trace_io_end(
    const sample_stream_scheduler_candidate_t *candidate,
    const sample_stream_io_command_t *command,
    const sample_stream_io_result_t *result,
    uint32_t duration_cycles);
void brick6_stream_underrun_trace_load_end(
    const sample_stream_scheduler_candidate_t *candidate,
    const sample_stream_io_result_t *result,
    uint8_t reason);
void brick6_stream_underrun_trace_ready(
    const sample_stream_scheduler_candidate_t *candidate,
    const sample_page_load_target_t *target,
    const sample_stream_io_result_t *result);
void brick6_stream_underrun_trace_consume_miss(sample_audio_key_t key,
                                               uint32_t page_index,
                                               uint32_t reader_position,
                                               uint32_t frames_remaining);
void brick6_stream_underrun_trace_page_state(const sample_page_desc_t *page,
                                             sample_page_state_t old_state,
                                             sample_page_state_t new_state);
void brick6_stream_underrun_trace_service_begin(uint32_t byte_budget,
                                                uint32_t pending_needs,
                                                uint32_t wake_sequence,
                                                uint32_t interval_frames,
                                                uint8_t gate_owner);
void brick6_stream_underrun_trace_service_blocked(uint8_t reason,
                                                  uint8_t gate_owner,
                                                  uint32_t poll_delay_frames);
void brick6_stream_underrun_trace_manager_end(uint32_t pages,
                                              uint32_t fatfs_ops,
                                              uint8_t reason);
void brick6_stream_underrun_trace_service_end(uint8_t reason,
                                              uint32_t pending_needs,
                                              uint8_t critical_active);
#else
#define brick6_stream_underrun_trace_reset() ((void)0)
#define brick6_stream_underrun_trace_voice_state(...) ((void)0)
#define brick6_stream_underrun_trace_need_created(...) ((void)0)
#define brick6_stream_underrun_trace_need_selectable(...) ((void)0)
#define brick6_stream_underrun_trace_scheduler(...) ((void)0)
#define brick6_stream_underrun_trace_load_begin(...) ((void)0)
#define brick6_stream_underrun_trace_io_begin(...) ((void)0)
#define brick6_stream_underrun_trace_io_end(...) ((void)0)
#define brick6_stream_underrun_trace_load_end(...) ((void)0)
#define brick6_stream_underrun_trace_ready(...) ((void)0)
#define brick6_stream_underrun_trace_consume_miss(...) ((void)0)
#define brick6_stream_underrun_trace_page_state(...) ((void)0)
#define brick6_stream_underrun_trace_service_begin(...) ((void)0)
#define brick6_stream_underrun_trace_service_blocked(...) ((void)0)
#define brick6_stream_underrun_trace_manager_end(...) ((void)0)
#define brick6_stream_underrun_trace_service_end(...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif
