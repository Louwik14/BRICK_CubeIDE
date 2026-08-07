#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BRICK6_STREAM_TRACE
#define BRICK6_STREAM_TRACE 0
#endif

#ifndef BRICK6_STREAM_AUDIT
#define BRICK6_STREAM_AUDIT 0
#endif

#define SAMPLE_STREAM_TRACE_CAPACITY (32U)
#define SAMPLE_STREAM_TRACE_MAGIC    (0x53545243UL)
#define SAMPLE_STREAM_AUDIT_HISTORY_CAPACITY (64U)
#define SAMPLE_STREAM_AUDIT_SERVICE_CAPACITY (64U)

typedef enum
{
    SAMPLE_STREAM_AUDIT_EXIT_NONE = 0,
    SAMPLE_STREAM_AUDIT_EXIT_EMPTY,
    SAMPLE_STREAM_AUDIT_EXIT_BYTE_BUDGET,
    SAMPLE_STREAM_AUDIT_EXIT_PAGE_LIMIT,
    SAMPLE_STREAM_AUDIT_EXIT_FATFS_LIMIT,
    SAMPLE_STREAM_AUDIT_EXIT_TIME_LIMIT,
    SAMPLE_STREAM_AUDIT_EXIT_STREAM_INFO,
    SAMPLE_STREAM_AUDIT_EXIT_STALE_TARGET,
    SAMPLE_STREAM_AUDIT_EXIT_IO_ERROR,
    SAMPLE_STREAM_AUDIT_EXIT_PUBLISH_ERROR
} sample_stream_audit_exit_t;

#if BRICK6_STREAM_AUDIT
typedef struct
{
    uint32_t service_sequence;
    uint32_t audio_frame_low;
    int32_t frames_to_deadline;
    uint16_t edf_rank;
    uint16_t requests_ahead;
    uint16_t backlog;
    uint16_t overdue;
} sample_stream_audit_rank_sample_t;

typedef struct
{
    uint64_t begin_audio_frame;
    uint64_t end_audio_frame;
    uint32_t begin_cycle;
    uint32_t end_cycle;
    uint32_t interval_cycles;
    uint32_t arrivals_since_previous;
    uint32_t other_sd_cycles_since_previous;
    uint32_t multi_bulk_cycles_since_previous;
    uint16_t backlog_begin;
    uint16_t overdue_begin;
    uint16_t pages_selected;
    uint8_t exit_reason;
    uint8_t reserved;
} sample_stream_audit_service_t;
#endif

typedef enum
{
    SAMPLE_STREAM_ROLE_SPECULATIVE = 0,
    SAMPLE_STREAM_ROLE_START,
    SAMPLE_STREAM_ROLE_LOOP,
    SAMPLE_STREAM_ROLE_CURRENT,
    SAMPLE_STREAM_ROLE_NEIGHBOR,
    SAMPLE_STREAM_ROLE_ANTICIPATION
} sample_stream_page_role_t;

typedef enum
{
    SAMPLE_STREAM_TRACE_TRIGGER_NONE = 0,
    SAMPLE_STREAM_TRACE_TRIGGER_LATE_SELECTION,
    SAMPLE_STREAM_TRACE_TRIGGER_CONSUME_MISS
} sample_stream_trace_trigger_t;

typedef struct
{
    sample_audio_key_t key;
    uint64_t created_audio_frame;
    uint64_t consume_deadline_audio_frame;
    uint64_t dispatch_deadline_audio_frame;
    uint64_t scheduler_waited_frames;
    uint64_t selected_audio_frame;
    uint64_t in_flight_audio_frame;
    uint64_t ready_audio_frame;
    uint32_t page_index;
    uint32_t reader_position;
    uint32_t frames_remaining;
    uint32_t deadline_frames;
    uint32_t deadline_cycle;
    uint32_t request_cycle;
    uint32_t select_cycle;
    uint32_t read_begin_cycle;
    uint32_t read_end_cycle;
    uint32_t ready_cycle;
    uint32_t owner_generation;
    uint32_t service_interval_cycles;
    uint32_t source_bytes;
    uint16_t pending_global;
    uint8_t role;
    uint8_t priority;
    uint8_t owner_kind;
    uint8_t owner_id;
    uint8_t backend;
    uint8_t physical_reads;
    uint8_t success;
    uint8_t starvation_guard_applied;
    uint8_t reserved;
#if BRICK6_STREAM_AUDIT
    uint32_t audit_history_write;
    uint32_t audit_history_count;
    sample_stream_audit_rank_sample_t audit_history[SAMPLE_STREAM_AUDIT_HISTORY_CAPACITY];
#endif
} sample_stream_trace_op_t;

typedef struct
{
    uint32_t magic;
    uint32_t enabled;
    uint32_t frozen;
    uint32_t trigger;
    sample_audio_key_t trigger_key;
    uint32_t trigger_page;
    uint32_t trigger_reader_position;
    uint32_t trigger_frames_remaining;
    uint64_t trigger_audio_frame;
    uint32_t trigger_cycle;
    uint32_t write_index;
    uint32_t count;
    uint32_t max_read_cycles;
    uint32_t max_wait_cycles;
    uint32_t max_service_interval_cycles;
    uint32_t max_deadline_late_cycles;
    uint64_t max_deadline_late_frames;
    uint32_t max_backlog;
    uint32_t file_changes;
    uint32_t max_pages_per_service;
#if BRICK6_STREAM_AUDIT
    uint32_t audit_service_sequence;
    uint32_t audit_service_write;
    uint32_t audit_service_count;
    uint32_t audit_arrivals_total;
    uint32_t audit_blocked_multi_polls;
    uint32_t audit_blocked_bulk_polls;
    uint64_t audit_blocked_multi_frames;
    uint64_t audit_blocked_bulk_frames;
    sample_stream_audit_service_t audit_services[SAMPLE_STREAM_AUDIT_SERVICE_CAPACITY];
#endif
    sample_stream_trace_op_t operations[SAMPLE_STREAM_TRACE_CAPACITY];
} sample_stream_trace_snapshot_t;

#if BRICK6_STREAM_TRACE
extern volatile sample_stream_trace_snapshot_t g_sample_stream_trace;
#endif

#ifdef __cplusplus
}
#endif
