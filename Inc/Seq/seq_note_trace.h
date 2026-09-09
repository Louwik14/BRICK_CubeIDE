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

#endif
