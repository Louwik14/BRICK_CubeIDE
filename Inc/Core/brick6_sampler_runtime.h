/**
 * @file brick6_sampler_runtime.h
 * @brief Sampler runtime facade.
 *
 * Rôle du module:
 * - Porter le point d'insertion unique du futur moteur Sampler.
 * - Rester sans autorité parallèle ni allocation dynamique.
 */

#pragma once

#include <stdint.h>

#include "Core/track_runtime.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_voice_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLER_MULTI_MAX_VOICES_PER_TRACK (4U)
#define SAMPLER_MULTI_MAX_GLOBAL_VOICES    (16U)

typedef enum
{
    BRICK6_SAMPLER_MULTI_DIAG_REASON_NONE = 0,
    BRICK6_SAMPLER_MULTI_DIAG_REASON_NO_INSTRUMENT,
    BRICK6_SAMPLER_MULTI_DIAG_REASON_INSTRUMENT_NOT_READY,
    BRICK6_SAMPLER_MULTI_DIAG_REASON_NO_ZONE,
    BRICK6_SAMPLER_MULTI_DIAG_REASON_INVALID_SAMPLE,
    BRICK6_SAMPLER_MULTI_DIAG_REASON_PAGE0_MISSING,
    BRICK6_SAMPLER_MULTI_DIAG_REASON_TRACK_VOICE_LIMIT,
    BRICK6_SAMPLER_MULTI_DIAG_REASON_GLOBAL_VOICE_LIMIT,
    BRICK6_SAMPLER_MULTI_DIAG_REASON_STEAL_SAME_TRACK,
    BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_DONE,
    BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN,
    BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_STEAL,
    BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_REL_DONE
} brick6_sampler_multi_diag_reason_t;

typedef struct
{
    uint32_t fast_path_blocks;
    uint32_t slow_path_blocks;
    uint32_t mixed_segments;
    uint32_t segment_cursor_blocks;
    uint32_t render_track_calls;
    uint32_t active_voices;
    uint32_t max_active_voices;
    uint64_t trigger_audio_timeline_sample;
    uint64_t first_output_audio_timeline_sample;
    uint32_t first_output_frame_offset;
    uint32_t start_frame;
    uint32_t region_begin;
    uint32_t region_end;
    uint32_t sample_length_frames;
    uint32_t fade_in_frames;
    uint32_t fade_out_frames;
    uint16_t sample_id;
    uint16_t multi_instrument_id;
    uint16_t multi_sample_id;
    uint32_t multi_note_on_rejected_not_ready;
    uint32_t multi_resolve_fail;
    uint32_t multi_page0_missing;
    uint32_t multi_page_window_missing;
    uint32_t multi_page_underrun;
    uint32_t multi_voice_started;
    uint32_t multi_voice_stolen_same_track;
    uint32_t multi_voice_stolen_global;
    uint32_t multi_voice_rejected_no_voice;
    uint32_t multi_no_instrument_assigned;
    uint32_t multi_invalid_instrument_id;
    uint32_t multi_instrument_not_ready;
    uint32_t multi_prefetch_request_count;
    uint32_t multi_stop_done;
    uint32_t multi_stop_underrun;
    uint32_t multi_stop_steal;
    uint32_t multi_stop_rel_done;
    uint32_t multi_rejected_track_voice_limit;
    uint32_t multi_rejected_global_voice_limit;
    uint32_t multi_last_current_frame;
    uint32_t multi_last_end_frame;
    uint32_t multi_last_missing_page;
    uint32_t multi_last_ready_mask;
    uint32_t common_plan_classic_build_fail;
    uint32_t common_plan_multi_build_fail;
    uint32_t common_plan_last_reason;
    uint32_t start_gate_reject_count;
    uint32_t start_gate_reject_classic_count;
    uint32_t start_gate_reject_multi_count;
    uint32_t start_gate_last_status;
    uint32_t start_gate_last_missing_page;
    uint32_t start_gate_last_pending_page;
    uint32_t start_gate_invalid_plan_count;
    uint32_t start_gate_missing_count;
    uint32_t start_gate_pending_count;
    uint32_t start_gate_partial_count;
    uint8_t multi_last_stolen_kind;
    uint8_t multi_last_stolen_track;
    uint8_t multi_last_reject_reason;
    uint8_t multi_last_stop_reason;
    uint8_t multi_last_steal_reason;
    uint8_t multi_last_active_global;
    uint8_t multi_last_active_track;
    uint8_t multi_gain_applied;
    uint8_t track_id;
    uint8_t note;
    uint8_t velocity;
    uint8_t mode;
    uint8_t use_segment_cursor;
    uint8_t first_output_valid;
} brick6_sampler_runtime_diag_snapshot_t;

typedef struct
{
    uint8_t active;
    uint8_t reverse;
    uint16_t sample_id;
    uint16_t ram_slot;
    uint32_t ram_generation;
    uint32_t frame;
    uint32_t frame_count;
    uint32_t trigger_order;
} brick6_sampler_ram_playhead_snapshot_t;

void brick6_sampler_runtime_init(void);
void brick6_sampler_runtime_reset_track(uint8_t track_id);
void brick6_sampler_runtime_set_sample(uint8_t track_id, uint16_t sample_id);
void brick6_sampler_runtime_set_gain(uint8_t track_id, float gain);
void brick6_sampler_runtime_set_multi_instrument(uint8_t track_id, uint16_t instrument_id);
void brick6_sampler_runtime_stop_multi_instrument(uint16_t instrument_id);
uint8_t brick6_sampler_runtime_get_multi_instrument(uint8_t track_id, uint16_t *out_instrument_id);
void brick6_sampler_runtime_set_multi_gain(uint8_t track_id, float gain);
float brick6_sampler_runtime_get_multi_gain(uint8_t track_id);
void brick6_sampler_runtime_set_multi_loop(uint8_t track_id, uint8_t enabled);
uint8_t brick6_sampler_runtime_multi_instrument_is_ready(uint8_t track_id);
void brick6_sampler_runtime_set_start(uint8_t track_id, float start);
void brick6_sampler_runtime_set_end(uint8_t track_id, float end);
void brick6_sampler_runtime_set_mode(uint8_t track_id, uint8_t mode);
void brick6_sampler_runtime_set_tune(uint8_t track_id, float tune);
void brick6_sampler_runtime_set_loop_start(uint8_t track_id, float loop_start);
void brick6_sampler_runtime_set_slice_count(uint8_t track_id, uint8_t slice_count);
void brick6_sampler_runtime_set_clip_source_bpm(uint8_t track_id, float source_bpm);
void brick6_sampler_runtime_set_clip_sync_length(uint8_t track_id, uint8_t sync_length);
void brick6_sampler_runtime_set_clip_pitch(uint8_t track_id, float semitones);
void brick6_sampler_runtime_set_clip_play_mode(uint8_t track_id, uint8_t play_mode);
void brick6_sampler_runtime_set_clip_loop(uint8_t track_id, uint8_t loop_enabled);
void brick6_sampler_runtime_set_clip_stretch_mode(uint8_t track_id, uint8_t stretch_mode);
void brick6_sampler_runtime_set_clip_grain_size(uint8_t track_id, uint16_t grain_size);
void brick6_sampler_runtime_trigger(uint8_t track_id);
void brick6_sampler_runtime_trigger_note(uint8_t track_id, uint8_t note);
void brick6_sampler_runtime_trigger_note_velocity(uint8_t track_id, uint8_t note, uint8_t velocity);
uint8_t brick6_sampler_runtime_trigger_multi_note_velocity(uint8_t track_id,
                                                           uint16_t instrument_id,
                                                           uint8_t note,
                                                           uint8_t velocity,
                                                           float gain);
uint8_t brick6_sampler_runtime_trigger_multi_track_note_velocity(uint8_t track_id,
                                                                 uint8_t note,
                                                                 uint8_t velocity);
void brick6_sampler_runtime_note_off_multi_track_note(uint8_t track_id, uint8_t note);
void brick6_sampler_runtime_note_off_note(uint8_t track_id, uint8_t note);
void brick6_sampler_runtime_note_off(uint8_t track_id);
void brick6_sampler_runtime_stop(uint8_t track_id);
void brick6_sampler_runtime_stop_transport_clips(void);
void brick6_sampler_runtime_queue_stream_pages(void);
void brick6_sampler_runtime_service(void);
void brick6_sampler_runtime_render_track(const track_runtime_ctx_t *ctx,
                                         float *out_l,
                                         float *out_r,
                                         uint32_t frames);
void brick6_sampler_runtime_diag_reset(void);
void brick6_sampler_runtime_diag_get_snapshot(brick6_sampler_runtime_diag_snapshot_t *out_snapshot);
uint8_t brick6_sampler_runtime_ram_slice_mode_active(uint8_t track_id);
uint8_t brick6_sampler_runtime_get_ram_playhead(uint8_t track_id,
                                                uint16_t sample_id,
                                                brick6_sampler_ram_playhead_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
