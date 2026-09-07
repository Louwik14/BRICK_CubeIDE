#ifndef SEQ_STALL_DEBUG_H
#define SEQ_STALL_DEBUG_H

#include <stdint.h>

#include "Seq/seq_types.h"

#define SEQ_STALL_DEBUG_VERSION 1U

typedef enum
{
    SEQ_STALL_STAGE_IDLE = 0,
    SEQ_STALL_STAGE_CORE_ENTRY,
    SEQ_STALL_STAGE_WINDOW_PREPARE,
    SEQ_STALL_STAGE_WINDOW_BEGIN,
    SEQ_STALL_STAGE_COLLECT,
    SEQ_STALL_STAGE_APPLY_PENDING,
    SEQ_STALL_STAGE_APPLY_EVENTS,
    SEQ_STALL_STAGE_NOTE_FX,
    SEQ_STALL_STAGE_MUSIC_COMMIT,
    SEQ_STALL_STAGE_RT_COMMIT,
    SEQ_STALL_STAGE_FINALIZE,
    SEQ_STALL_STAGE_CORE_EXIT
} seq_stall_stage_t;

typedef enum
{
    SEQ_STALL_EXIT_NONE = 0,
    SEQ_STALL_EXIT_COMPLETE,
    SEQ_STALL_EXIT_PATTERN_BOUNDARY,
    SEQ_STALL_EXIT_RT_BEGIN_FAIL,
    SEQ_STALL_EXIT_MUSIC_BEGIN_FAIL,
    SEQ_STALL_EXIT_APPLY_PENDING_FAIL,
    SEQ_STALL_EXIT_EVENT_PUBLISH_FAIL,
    SEQ_STALL_EXIT_EVENT_APPLY_FAIL,
    SEQ_STALL_EXIT_NOTE_FX_FAIL,
    SEQ_STALL_EXIT_MUSIC_COMMIT_FAIL,
    SEQ_STALL_EXIT_RT_COMMIT_FAIL,
    SEQ_STALL_EXIT_FINALIZE_FAIL,
    SEQ_STALL_EXIT_STOPPED,
    SEQ_STALL_EXIT_START_PENDING,
    SEQ_STALL_EXIT_EXTERNAL_CLOCK
} seq_stall_exit_reason_t;

typedef struct
{
    uint32_t version;
    uint32_t core_entry_count;
    uint32_t core_exit_count;
    uint32_t control_wake_count;
    uint32_t control_last_wake_flags;
    uint32_t control_deadline_tail_count;
    uint32_t control_power_shutdown_return_count;
    uint32_t stage;
    uint32_t exit_reason;
    uint32_t deadline_arm_count;
    uint32_t deadline_fire_count;
    uint32_t deadline_service_entry_count;
    uint32_t deadline_arm_attempt_count;
    uint32_t deadline_armed;
    uint32_t deadline_hal_status;
    uint32_t deadline_service_return_reason;
    uint32_t deadline_musical_active;
    uint32_t deadline_live_note_active;
    uint32_t deadline_clock_source;
    uint32_t deadline_runtime_running;
    uint32_t deadline_start_pending;
    uint32_t deadline_tim_cr1;
    uint32_t deadline_tim_dier;
    uint32_t deadline_tim_sr;
    uint32_t deadline_tim_cnt;
    uint32_t internal_tick;
    uint32_t metronome_step;
    uint32_t boundary_count;
    uint32_t boundary_track_step;
    uint32_t scheduler_pass_count;
    uint32_t scheduler_active_sources;
    uint32_t scheduler_imminent_count;
    uint32_t window_begin_count;
    uint32_t music_commit_count;
    uint32_t rt_commit_count;
    uint32_t window_finalize_count;
    uint64_t audio_sample;
    uint64_t control_cursor;
    uint64_t publish_limit;
    uint64_t first_unpublished;
    uint64_t timeline;
    uint64_t boundary_sample;
    uint64_t scheduler_block_start;
    uint64_t scheduler_block_end;
    uint64_t window_first;
    uint32_t window_frames;
    uint8_t running;
    uint8_t transport_state;
    uint8_t play_step[SEQ_LANE_CAPACITY];
    uint8_t reserved[2];
} seq_stall_debug_t;

extern volatile seq_stall_debug_t g_seq_stall_debug;

#endif /* SEQ_STALL_DEBUG_H */
