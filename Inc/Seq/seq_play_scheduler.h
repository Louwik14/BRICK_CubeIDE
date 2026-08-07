#ifndef SEQ_PLAY_SCHEDULER_H
#define SEQ_PLAY_SCHEDULER_H

#if !defined(SEQ_RUNTIME_INTERNAL_USE) && !defined(SEQ_PLAY_SCHEDULER_IMPLEMENTATION)
#error "seq_play_scheduler is internal to the sequencer time-domain runtime."
#endif

#include <stdint.h>

#include "Seq/seq_types.h"
#include "NoteFx/note_fx_event.h"

#define SEQ_PLAY_SCHEDULER_HALF_EVENT_QUOTA 128U

typedef struct
{
    uint8_t type;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint8_t track_generation;
    uint8_t reserved;
    uint16_t sample_offset_in_block;
    uint64_t sample_abs;
    uint32_t generation;
    uint32_t event_token;
} seq_play_scheduler_audio_event_t;

typedef struct
{
    uint16_t queue_high_water;
    uint16_t max_events_collected_per_call;
    uint32_t queue_overflow_drop_count;
    uint32_t note_pair_overflow_drop_count;
    uint32_t overdue_event_count;
    uint32_t offset_clamp_count;
    uint32_t stale_generation_drop_count;
    uint16_t active_occurrence_count;
    uint16_t max_active_occurrences;
    uint32_t orphan_note_off_count;
    uint32_t duplicate_note_on_count;
    uint16_t half_events_last;
    uint16_t half_events_high_water;
    uint32_t half_quota_exhaustion_count;
    uint32_t terminal_on_internal_admitted;
    uint32_t terminal_on_internal_refused;
    uint32_t terminal_on_stale_refused;
    uint32_t terminal_on_midi_admitted;
    uint32_t terminal_on_midi_refused;
    uint32_t terminal_off_refused;
    uint32_t terminal_capacity_refusal_count;
    uint32_t terminal_off_retry_count;
    uint16_t terminal_active_count;
    uint16_t terminal_high_water;
} seq_play_scheduler_diag_t;

typedef enum
{
    SEQ_PLAY_TRANSITION_MUTE_TRIGS = 0,
    SEQ_PLAY_TRANSITION_RESUME_TRIGS,
    SEQ_PLAY_TRANSITION_STOP_CLOSE,
    SEQ_PLAY_TRANSITION_PANIC_CLOSE_ALL,
    SEQ_PLAY_TRANSITION_PATTERN_REPLACE,
    SEQ_PLAY_TRANSITION_MODEL_RECONFIGURE,
    SEQ_PLAY_TRANSITION_DESTINATION_REBIND,
    SEQ_PLAY_TRANSITION_SOURCE_SWITCH
} seq_play_transition_policy_t;

void seq_play_scheduler_init(void);
void seq_play_scheduler_clear(void);
void seq_play_scheduler_clear_tracks(const seq_track_id_t *tracks, uint8_t track_count);
void seq_play_scheduler_suspend_tracks(const seq_track_id_t *tracks, uint8_t track_count);
void seq_play_scheduler_resume_tracks(const seq_track_id_t *tracks, uint8_t track_count);
uint8_t seq_play_scheduler_transition_tracks(const seq_track_id_t *tracks,
                                             uint8_t track_count,
                                             seq_play_transition_policy_t policy);
uint8_t seq_play_scheduler_transition_all(seq_play_transition_policy_t policy);
void seq_play_scheduler_audio_begin_half(uint16_t event_quota);
void seq_play_scheduler_audio_end_half(void);
void seq_play_scheduler_terminal_reset(void);
/*
 * Contract surface:
 * - scheduling surface only: consumes step boundaries and queues sample-domain events.
 * - does not own transport progression or audio-block timeline advancement.
 */
void seq_play_scheduler_schedule_step(seq_track_id_t track,
                                      seq_step_id_t step,
                                      uint16_t ticks_per_step,
                                      uint32_t step_tick,
                                      uint64_t step_sample_time,
                                      uint32_t samples_per_step_q16);
void seq_play_scheduler_schedule_step_lookahead_negative(seq_track_id_t track,
                                                         seq_step_id_t step,
                                                         uint64_t step_sample_time,
                                                         uint32_t samples_per_step_q16);
/*
 * Contract surface:
 * - audio-block projection of the scheduler queue.
 * - collects due events within the current block without advancing runtime timeline.
 */
uint16_t seq_play_scheduler_audio_collect_block_events(seq_play_scheduler_audio_event_t *out_events,
                                                       uint16_t max_events,
                                                       uint16_t block_frames,
                                                       uint64_t block_start_sample);
/*
 * Contract surface:
 * - apply surface only: dispatches a queued scheduler event to MIDI/engines/mixers.
 * - does not change transport or timeline ownership.
 */
void seq_play_scheduler_audio_apply_event(const seq_play_scheduler_audio_event_t *event);
note_fx_result_t seq_play_scheduler_dispatch_terminal_event(const note_fx_event_t *event);
/* Audio-owner panic closure.  It closes terminal admissions without using the
 * ordinary scheduler/NoteFx command queue.  Returns zero while a terminal
 * close must be retried on a later audio call. */
uint8_t seq_play_scheduler_panic_audio(uint64_t first_renderable_sample);
/*
 * Contract surface:
 * - post-commit notifications from runtime/transport.
 * - refresh scheduler-facing mirrors or emit transport-start/pattern-change re-seeding.
 */
void seq_play_scheduler_live_midi_program_changed(seq_track_id_t track, float program_value);
/*
 * Contract surface:
 * - post-commit notification on transport start.
 * - re-seeds program changes from scheduler mirrors without owning transport state.
 */
void seq_play_scheduler_emit_midi_program_on_transport_start(void);
/*
 * Contract surface:
 * - post-commit notification on pattern change.
 * - re-seeds scheduler-visible program state without changing timeline ownership.
 */
void seq_play_scheduler_notify_track_pattern_change(seq_track_id_t track);
void seq_play_scheduler_diag_reset(void);
/*
 * Contract surface:
 * - queue diagnostics mirror only.
 * - resets accumulated scheduler diagnostics without touching transport or timeline ownership.
 */
void seq_play_scheduler_diag_snapshot(seq_play_scheduler_diag_t *out_diag);

#endif /* SEQ_PLAY_SCHEDULER_H */
