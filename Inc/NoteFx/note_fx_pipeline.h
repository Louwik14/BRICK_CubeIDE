#ifndef NOTE_FX_PIPELINE_H
#define NOTE_FX_PIPELINE_H

#include <stdint.h>
#include "Core/live_event.h"
#include "NoteFx/note_fx_event.h"
#include "NoteFx/note_fx_state.h"

#define NOTE_FX_HALF_BUFFER_FRAMES 64U
#define NOTE_FX_HALF_ON_QUOTA_PER_TRACK 8U
#define NOTE_FX_HALF_OFF_RESERVE 32U
#define NOTE_FX_HALF_EMISSION_BUDGET \
    ((NOTE_FX_TRACK_COUNT * NOTE_FX_HALF_ON_QUOTA_PER_TRACK) \
     + NOTE_FX_HALF_OFF_RESERVE)
#define NOTE_FX_HALF_COMMAND_QUOTA 32U

/* A live source is queued by a non-audio producer.  The audio owner resolves
 * this marker at command consumption so the event sample is the application
 * sample, not the producer's earlier timeline projection. */
#define NOTE_FX_SAMPLE_TIME_AUDIO_OWNER UINT64_MAX

void note_fx_pipeline_init(void);
typedef struct
{
    uint32_t accepted;
    uint32_t rejected;
    uint32_t stale;
    uint16_t command_high_water;
    uint32_t command_drop_count;
    uint32_t stage_emissions[5];
    uint32_t continuation_drop_count;
    uint32_t budget_on_drop_count;
    uint32_t budget_off_drop_count;
    uint16_t half_emissions_last;
    uint16_t half_emissions_high_water;
    uint8_t max_stage_reached;
} note_fx_pipeline_diag_t;

typedef enum
{
    NOTE_FX_TRANSITION_MUTE_TRIGS = 0,
    NOTE_FX_TRANSITION_STOP_CLOSE,
    NOTE_FX_TRANSITION_PANIC_CLOSE_ALL,
    NOTE_FX_TRANSITION_MODEL_RECONFIGURE,
    NOTE_FX_TRANSITION_PATTERN_REPLACE,
    NOTE_FX_TRANSITION_DESTINATION_REBIND,
    NOTE_FX_TRANSITION_SOURCE_CLOCK_CHANGE
} note_fx_transition_policy_t;

note_fx_pipeline_diag_t note_fx_pipeline_diag(uint8_t track);
note_fx_result_t note_fx_pipeline_submit(const note_fx_event_t *event);
/* Audio-owner seam: only the audio execution domain may call this directly. */
note_fx_result_t note_fx_pipeline_submit_audio(const note_fx_event_t *event);
note_fx_result_t note_fx_pipeline_submit_source_occurrence(
    uint8_t track, uint8_t note, uint8_t velocity, uint8_t is_note_on,
    uint64_t sample_time, note_event_provenance_t provenance,
    uint32_t source_occurrence_id);
note_fx_result_t note_fx_pipeline_submit_source_capture_tick(
    uint8_t track, uint8_t note, uint8_t velocity, uint8_t is_note_on,
    uint32_t capture_tick, uint32_t ingress_serial,
    note_event_provenance_t provenance,
    uint32_t source_occurrence_id);

typedef struct
{
    uint32_t late_count;
    uint32_t stale_count;
    uint32_t queue_drop_count;
    uint64_t max_lateness_samples;
    uint16_t depth;
    uint16_t high_water;
} note_fx_live_queue_diag_t;

void note_fx_pipeline_get_live_queue_diag(note_fx_live_queue_diag_t *out_diag);
/* Returns non-zero only while an FX-owned occurrence is still current in the
 * audio owner.  Terminal admission uses this to reject stale generated On. */
uint8_t note_fx_pipeline_is_generated_occurrence_current(
    uint8_t track, uint32_t occurrence_id, uint32_t generation);
void note_fx_pipeline_process(uint64_t block_start, uint16_t frames,
                              uint32_t samples_per_step_q16);
void note_fx_pipeline_begin_audio_half(uint16_t frames);
void note_fx_pipeline_end_audio_half(void);
uint16_t note_fx_pipeline_frames_until_deadline(uint64_t block_start,
                                                uint16_t max_frames);
uint8_t note_fx_pipeline_sync_track(uint8_t track);
void note_fx_pipeline_reset_runtime_overrides(uint8_t track);
void note_fx_pipeline_reset_all_runtime_overrides(void);
uint8_t note_fx_pipeline_transition_track(uint8_t track,
                                          note_fx_transition_policy_t policy);
uint8_t note_fx_pipeline_transition_all(note_fx_transition_policy_t policy);
uint8_t note_fx_pipeline_apply_runtime_param(uint8_t track, uint8_t slot,
                                             uint8_t param, uint8_t value);
uint8_t note_fx_pipeline_release_runtime_param(uint8_t track, uint8_t slot,
                                               uint8_t param);

#endif
