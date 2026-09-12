#ifndef NOTE_FX_PIPELINE_H
#define NOTE_FX_PIPELINE_H

#include <stdint.h>
#include "IPC/live_event.h"
#include "NoteFx/note_fx_event.h"
#include "NoteFx/note_fx_arp.h"
#include "NoteFx/note_fx_state.h"

#define NOTE_FX_PIPELINE_MAX_STAGE_FANOUT NOTE_FX_SLOT_COUNT
#define NOTE_FX_PIPELINE_MAX_TEMPORAL_HELD_PER_TRACK \
    (NOTE_FX_SLOT_COUNT * NOTE_FX_HELD_PITCH_CAPACITY)

_Static_assert(NOTE_FX_PIPELINE_MAX_STAGE_FANOUT == NOTE_FX_SLOT_COUNT,
               "Note FX pipeline must cover every canonical slot");

/* A live source is queued by a non-audio producer.  The audio owner resolves
 * this marker at command consumption so the event sample is the application
 * sample, not the producer's earlier timeline projection. */
#define NOTE_FX_SAMPLE_TIME_CONTROL_ANCHOR UINT64_MAX

void note_fx_pipeline_init(void);
uint16_t note_fx_pipeline_diagnostic_queue_depth(void);
void note_fx_pipeline_panic(void);

/* CONTROL-owned direct submission seam. */
note_event_result_t note_fx_pipeline_submit_control(const note_event_t *event);
note_event_result_t note_fx_pipeline_submit_control_batch(
    const note_event_t *events, uint8_t event_count);
uint8_t note_fx_pipeline_source_note_limit(uint8_t track);
uint8_t note_fx_pipeline_reset_track(uint8_t track);
note_event_result_t note_fx_pipeline_submit_source_occurrence(
    uint8_t track, uint8_t note, uint8_t velocity, uint8_t is_note_on,
    uint64_t sample_time, note_event_provenance_t provenance,
    uint32_t source_occurrence_id);
note_event_result_t note_fx_pipeline_submit_source_capture_tick(
    uint8_t track, uint8_t note, uint8_t velocity, uint8_t is_note_on,
    uint32_t capture_tick, uint32_t ingress_serial,
    note_event_provenance_t provenance,
    uint32_t source_occurrence_id);

uint8_t note_fx_pipeline_forget_causal_sources(
    uint8_t track, const uint32_t *causal_source_ids, uint16_t source_count);
uint8_t note_fx_pipeline_process(uint64_t block_start, uint16_t frames,
                                 uint32_t samples_per_step_q16);
uint8_t note_fx_pipeline_apply_pending(void);
uint8_t note_fx_pipeline_reserve_state(
    uint8_t track, const note_fx_track_state_t *state);
uint8_t note_fx_pipeline_prepare_external_window(uint64_t block_start,
                                                 uint16_t frames);
uint8_t note_fx_pipeline_configure_track(uint8_t track);
uint8_t note_fx_pipeline_apply_control_override(uint8_t track, uint8_t slot,
                                                uint8_t param, uint8_t value);
uint8_t note_fx_pipeline_release_control_override(uint8_t track, uint8_t slot,
                                                  uint8_t param);

#endif
