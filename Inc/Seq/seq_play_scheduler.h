#ifndef SEQ_PLAY_SCHEDULER_H
#define SEQ_PLAY_SCHEDULER_H

#if !defined(SEQ_RUNTIME_INTERNAL_USE) && !defined(SEQ_PLAY_SCHEDULER_IMPLEMENTATION)
#error "seq_play_scheduler is internal to the sequencer time-domain runtime."
#endif

#include <stdint.h>

#include "Seq/seq_types.h"

typedef struct
{
    uint8_t type;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint16_t sample_offset_in_block;
    uint32_t event_token;
} seq_play_scheduler_audio_event_t;

typedef struct
{
    uint16_t queue_high_water;
    uint16_t max_events_collected_per_call;
    uint32_t queue_overflow_drop_count;
    uint32_t overdue_event_count;
    uint32_t offset_clamp_count;
    uint32_t stale_generation_drop_count;
} seq_play_scheduler_diag_t;

void seq_play_scheduler_init(void);
void seq_play_scheduler_clear(void);
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
