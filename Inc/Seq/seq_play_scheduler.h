#ifndef SEQ_PLAY_SCHEDULER_H
#define SEQ_PLAY_SCHEDULER_H

#if !defined(SEQ_RUNTIME_INTERNAL_USE) && !defined(SEQ_PLAY_SCHEDULER_IMPLEMENTATION)
#error "seq_play_scheduler is internal to the sequencer time-domain runtime."
#endif

#include <stdint.h>

#include "Seq/seq_types.h"
#include "Seq/seq_model.h"
#include "NoteFx/note_fx_event.h"


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
} seq_play_scheduler_event_t;

void seq_play_scheduler_init(void);
void seq_play_scheduler_clear(void);
void seq_play_scheduler_clear_tracks(const seq_track_id_t *tracks, uint8_t track_count);
void seq_play_scheduler_suspend_tracks(const seq_track_id_t *tracks, uint8_t track_count);
void seq_play_scheduler_resume_tracks(const seq_track_id_t *tracks, uint8_t track_count);
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
                                      uint32_t samples_per_step_q16,
                                      uint8_t swing_phase);
void seq_play_scheduler_schedule_step_lookahead_negative(seq_track_id_t track,
                                                         seq_step_id_t step,
                                                         uint64_t step_sample_time,
                                                         uint32_t samples_per_step_q16,
                                                         uint8_t swing_phase);
/*
 * Contract surface:
 * - audio-block projection of the scheduler queue.
 * - collects due events within the current block without advancing runtime timeline.
 */
uint16_t seq_play_scheduler_collect_due_events(seq_play_scheduler_event_t *out_events,
                                                       uint16_t max_events,
                                                       uint16_t block_frames,
                                                       uint64_t block_start_sample);
/*
 * Contract surface:
 * - apply surface only: dispatches a queued scheduler event to MIDI/engines/mixers.
 * - does not change transport or timeline ownership.
 */
uint8_t seq_play_scheduler_control_apply_event(
    const seq_play_scheduler_event_t *event);
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
 * - closes the removed sequence source and invalidates its future events.
 */
void seq_play_scheduler_notify_track_pattern_change(seq_track_id_t track);
void seq_play_scheduler_notify_play_changed(seq_track_id_t track,
                                            seq_step_id_t step,
                                            uint8_t voice,
                                            seq_step_play_field_t field);
void seq_play_scheduler_remove_play(seq_track_id_t track,
                                    seq_step_id_t step,
                                    int16_t voice);
void seq_play_scheduler_notify_roll_changed(seq_track_id_t track,
                                            seq_step_id_t step);

#endif /* SEQ_PLAY_SCHEDULER_H */
