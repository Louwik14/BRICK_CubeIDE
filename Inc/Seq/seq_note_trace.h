#ifndef SEQ_NOTE_TRACE_H
#define SEQ_NOTE_TRACE_H

#include <stdint.h>

#define SEQ_NOTE_TRACE_CAPACITY 256U

typedef enum
{
    SEQ_NOTE_TRACE_PLAY_EXPECTED = 1,
    SEQ_NOTE_TRACE_SAMPLE_PLANNED,
    SEQ_NOTE_TRACE_ACTIVE_CREATED,
    SEQ_NOTE_TRACE_ACTIVE_REPLACED,
    SEQ_NOTE_TRACE_VICTIM_OFF_GENERATED,
    SEQ_NOTE_TRACE_VICTIM_OFF_MISSING,
    SEQ_NOTE_TRACE_FIFO_NOTE_ON,
    SEQ_NOTE_TRACE_FIFO_NOTE_OFF,
    SEQ_NOTE_TRACE_TERMINAL_ON,
    SEQ_NOTE_TRACE_TERMINAL_OFF,
    SEQ_NOTE_TRACE_HORIZON_COMMIT,
    SEQ_NOTE_TRACE_HORIZON_ABORT,
    SEQ_NOTE_TRACE_SKIP_COMMIT_FLOOR,
    SEQ_NOTE_TRACE_SKIP_INVALIDATED,
    SEQ_NOTE_TRACE_REJECT_SCHED_CAPACITY,
    SEQ_NOTE_TRACE_REJECT_NOTE_FX,
    SEQ_NOTE_TRACE_SUPPRESS_MUTED,
    SEQ_NOTE_TRACE_REJECT_TERMINAL,
    SEQ_NOTE_TRACE_REJECT_INVALID_SOURCE,
    SEQ_NOTE_TRACE_REJECT_STALE_SCHEDULER
} seq_note_trace_kind_t;

typedef enum
{
    SEQ_STEP_DEBUG_SKIP_NONE = 0U,
    SEQ_STEP_DEBUG_SKIP_SOURCE_REJECTED,
    SEQ_STEP_DEBUG_SKIP_IMMINENT_REJECTED,
    SEQ_STEP_DEBUG_SKIP_WINDOW_ABORTED,
    SEQ_STEP_DEBUG_SKIP_FIFO_REJECTED,
    SEQ_STEP_DEBUG_SKIP_COMMIT_FLOOR,
    SEQ_STEP_DEBUG_SKIP_INVALIDATED,
    SEQ_STEP_DEBUG_SKIP_STALE,
    SEQ_STEP_DEBUG_SKIP_MUTED,
    SEQ_STEP_DEBUG_SKIP_NOTE_FX
} seq_step_debug_skip_t;

typedef struct
{
    uint64_t sample_time;
    uint64_t horizon_first;
    uint64_t horizon_end;
    uint32_t expected_count;
    uint32_t scheduler_processed_count;
    uint32_t note_on_generated_count;
    uint32_t note_off_generated_count;
    uint32_t actions_staged_count;
    uint32_t actions_published_count;
    uint32_t audio_consumed_count;
    uint32_t audio_applied_count;
    uint32_t scheduler_source_refused_count;
    uint32_t imminent_occurrence_refused_count;
    uint32_t musical_window_aborted_count;
    uint32_t fifo_publication_refused_count;
    uint32_t step_advance_without_event_count;
    uint32_t last_output_id;
    uint16_t last_skip_reason;
    uint8_t track;
    uint8_t expected_step;
    uint8_t processed_step;
    uint8_t reserved[3];
} seq_step_debug_t;

typedef struct
{
    uint64_t sample;
    uint64_t aux_sample;
    uint32_t output_id;
    uint32_t related_id;
    uint32_t sequence;
    uint16_t kind;
    uint8_t track;
    uint8_t step;
} seq_note_trace_entry_t;

_Static_assert(sizeof(seq_note_trace_entry_t) == 32U,
               "temporary sequencer trace entry must remain fixed");
_Static_assert((SEQ_NOTE_TRACE_CAPACITY
                & (SEQ_NOTE_TRACE_CAPACITY - 1U)) == 0U,
               "temporary sequencer trace capacity must be a power of two");

extern volatile seq_note_trace_entry_t
    g_seq_note_trace_ring[SEQ_NOTE_TRACE_CAPACITY];
extern volatile uint32_t g_seq_note_trace_head;
extern volatile uint32_t g_seq_note_trace_overwrite_count;
extern volatile uint8_t g_seq_note_trace_enabled;
extern volatile uint8_t g_seq_note_trace_track;
extern volatile uint8_t g_seq_note_trace_step;
extern volatile seq_step_debug_t g_seq_step_debug;

uint8_t seq_note_trace_target(uint8_t track, uint8_t step);
void seq_note_trace_record(uint16_t kind, uint8_t track, uint8_t step,
                           uint64_t sample, uint64_t aux_sample,
                           uint32_t output_id, uint32_t related_id);
void seq_note_trace_watch_output(uint8_t track, uint8_t step,
                                 uint32_t output_id);
uint8_t seq_note_trace_output_is_watched(uint32_t output_id,
                                         uint8_t *out_track,
                                         uint8_t *out_step);
void seq_note_trace_horizon_begin(uint64_t first_sample);
void seq_note_trace_horizon_commit(uint64_t first_sample,
                                   uint64_t end_sample);
void seq_note_trace_horizon_abort(uint64_t first_sample,
                                  uint64_t end_sample);
void seq_step_debug_expect(uint8_t track, uint8_t step, uint64_t sample_time);
void seq_step_debug_scheduler_processed(uint8_t track, uint8_t step,
                                        uint64_t sample_time,
                                        uint8_t generated_any);
void seq_step_debug_source_refused(uint8_t track, uint8_t step,
                                   uint64_t sample_time);
void seq_step_debug_imminent_refused(uint8_t track, uint8_t step,
                                     uint64_t sample_time);
void seq_step_debug_note_off_generated(uint8_t track, uint8_t step,
                                       uint64_t sample_time,
                                       uint32_t output_id);
void seq_step_debug_audio(uint32_t output_id, uint64_t sample_time,
                          uint8_t applied);

#endif
