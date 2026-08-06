#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BRICK6_STREAM_TRACE
#define BRICK6_STREAM_TRACE 0
#endif

#define SAMPLE_STREAM_TRACE_CAPACITY (32U)
#define SAMPLE_STREAM_TRACE_MAGIC    (0x53545243UL)

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
    uint8_t reserved;
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
    sample_stream_trace_op_t operations[SAMPLE_STREAM_TRACE_CAPACITY];
} sample_stream_trace_snapshot_t;

#if BRICK6_STREAM_TRACE
extern volatile sample_stream_trace_snapshot_t g_sample_stream_trace;
#endif

#ifdef __cplusplus
}
#endif
