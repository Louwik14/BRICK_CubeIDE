#ifndef BRICK6_LOOPER_RUNTIME_H
#define BRICK6_LOOPER_RUNTIME_H

#include <stdint.h>

#include "Audio/audio_note_engine_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    BRICK6_LOOPER_RUNTIME_SOURCE_NONE = 0,
    BRICK6_LOOPER_RUNTIME_SOURCE_WAV = 1
} brick6_looper_runtime_source_t;

typedef enum
{
    BRICK6_LOOPER_RUNTIME_STATE_EMPTY = 0,
    BRICK6_LOOPER_RUNTIME_STATE_LOAD_PENDING,
    BRICK6_LOOPER_RUNTIME_STATE_LOADING,
    BRICK6_LOOPER_RUNTIME_STATE_READY,
    BRICK6_LOOPER_RUNTIME_STATE_PLAYING,
    BRICK6_LOOPER_RUNTIME_STATE_FAILED
} brick6_looper_runtime_state_t;

typedef struct
{
    uint64_t scheduled_start_sample;
    uint64_t actual_start_sample;
    uint64_t first_output_audio_timeline_sample;
    uint32_t first_output_frame_offset;
    uint32_t start_playhead;
    uint32_t playhead;
    uint32_t recorded_frames;
    uint32_t recorded_steps_q16;
    uint32_t source_samples_per_step_q16;
    uint32_t source_bpm_milli;
    uint32_t current_page_start_frame;
    uint32_t current_page_frame_count;
    uint64_t record_start_sample;
    uint64_t record_stop_sample;
    uint16_t cache_id;
    uint8_t track_id;
    uint8_t play_auto;
    uint8_t scheduled_start_valid;
    uint8_t first_output_valid;
    uint8_t page_miss_seen;
    uint8_t state;
    uint8_t source;
    uint32_t preroll_bridge_active;
    uint32_t playback_normal_used;
    uint32_t fallback_miss;
    uint32_t wrap_count;
} brick6_looper_runtime_diag_snapshot_t;

/* Temporary hardware probe: reset at the effective Looper record start. */
typedef struct
{
    volatile uint32_t capture_blocks;
    volatile uint32_t capture_frames;
    volatile uint32_t recorder_blocks;
    volatile uint32_t recorder_frames;
    volatile uint32_t playback_source_reads;
    volatile uint32_t renderer_blocks;
    volatile uint32_t looper_bus_blocks;
    volatile float capture_peak;
    volatile float recorder_peak;
    volatile float playback_source_peak;
    volatile float renderer_peak;
    volatile float looper_bus_peak;
} brick6_looper_signal_probe_t;

extern volatile brick6_looper_signal_probe_t g_brick6_looper_signal_probe;

typedef struct
{
    volatile uint32_t ui_count;
    volatile uint32_t ui_command_id;
    volatile uint32_t ui_track;
    volatile uint32_t ui_value;
    volatile uint32_t control_count;
    volatile uint32_t control_command_id;
    volatile uint32_t control_track;
    volatile uint32_t control_value;
    volatile uint32_t publish_count;
    volatile uint32_t publish_command_id;
    volatile uint32_t publish_track;
    volatile uint32_t publish_value;
    volatile uint32_t publish_result;
    volatile uint32_t fifo_count;
    volatile uint32_t fifo_command_id;
    volatile uint32_t fifo_track;
    volatile uint32_t fifo_value;
    volatile uint32_t audio_apply_count;
    volatile uint32_t audio_apply_command_id;
    volatile uint32_t audio_apply_track;
    volatile uint32_t audio_apply_value;
    volatile uint32_t backend_count;
    volatile uint32_t backend_command_id;
    volatile uint32_t backend_track;
    volatile uint32_t backend_value;
    volatile uint32_t runtime_entry_count;
    volatile uint32_t runtime_command_id;
    volatile uint32_t runtime_track;
    volatile uint32_t runtime_value;
    volatile uint32_t rec_command_count;
    volatile uint32_t capture_start_command_count;
    volatile uint32_t runtime_arm_count;
    volatile uint32_t runtime_start_count;
    volatile uint32_t runtime_flags;
    volatile uint32_t source_mask;
    volatile uint32_t capture_gate_count;
    volatile uint32_t capture_active;
    volatile uint32_t capture_active_count;
    volatile uint32_t contributor_count;
    volatile uint32_t contributor_mask;
    volatile uint32_t capture_call_count;
    volatile uint32_t recorder_start_count;
    volatile uint32_t recorder_start_result;
    volatile uint32_t recorder_push_count;
    volatile uint32_t recorder_push_result;
    volatile uint32_t command_value;
    volatile uint32_t command_id;
    volatile uint32_t frames;
    volatile uint32_t track;
    volatile uint32_t flags;
} brick6_looper_record_probe_t;

extern volatile brick6_looper_record_probe_t g_brick6_looper_record_probe;

void brick6_looper_runtime_init(void);
void brick6_looper_runtime_service(uint32_t byte_budget);
void brick6_looper_runtime_stop_playback(uint8_t track_id);
void brick6_looper_runtime_prepare_replace(uint8_t track_id);
void brick6_looper_runtime_arm_live_record_start(uint8_t track_id,
                                                 uint8_t len_mode,
                                                 uint32_t expected_frames,
                                                 uint8_t play_auto,
                                                 uint8_t overdub,
                                                 uint64_t request_sample);
void brick6_looper_runtime_arm_record_stop(uint64_t request_sample);
void brick6_looper_runtime_on_record_start(uint64_t sample_time);
void brick6_looper_runtime_on_record_stop(uint64_t sample_time);
/* AUDIO-local capture routing; no Storage state is consulted. */
uint8_t brick6_looper_runtime_get_record_capture_track(uint8_t *out_track);
uint8_t brick6_looper_runtime_is_overdub_recording(uint8_t track_id);
void brick6_looper_runtime_preroll_capture_from_irq(uint8_t track_id,
                                                    const int32_t *lr_interleaved,
                                                    uint32_t frames);
uint8_t brick6_looper_runtime_capture_from_irq(uint8_t track_id,
                                               const int32_t *lr_interleaved,
                                               uint32_t frames);
void brick6_looper_runtime_set_play_auto(uint8_t track_id, uint8_t play_auto);
void brick6_looper_runtime_set_main_xfade(uint8_t track_id, float xfade);
float brick6_looper_runtime_get_main_xfade(uint8_t track_id);
void brick6_looper_runtime_set_stretch(uint8_t track_id,
                                       uint8_t mode,
                                       float pitch_semitones,
                                       uint16_t grain_frames);
void brick6_looper_runtime_set_stretch_mode(uint8_t track_id, uint8_t mode);
void brick6_looper_runtime_set_stretch_pitch(uint8_t track_id,
                                              float pitch_semitones);
void brick6_looper_runtime_set_stretch_grain(uint8_t track_id,
                                              uint16_t grain_frames);
void brick6_looper_runtime_on_transport_start(uint64_t sample_time);
void brick6_looper_runtime_on_transport_stop(void);
uint8_t brick6_looper_runtime_next_start_offset(uint64_t block_start_sample,
                                                uint32_t block_frames,
                                                uint16_t *out_offset);
void brick6_looper_runtime_on_scheduled_start(uint64_t sample_time);
uint8_t brick6_looper_runtime_is_ready(uint8_t track_id);
uint8_t brick6_looper_runtime_is_playing(uint8_t track_id);
uint16_t brick6_looper_runtime_playing_mask(void);
uint16_t brick6_looper_runtime_scheduled_start_mask(void);
void brick6_looper_runtime_render_track(const track_audio_runtime_ctx_t *ctx,
                                        float *out_l,
                                        float *out_r,
                                        uint32_t frames);
void brick6_looper_runtime_probe_rendered(const float *out_l,
                                          const float *out_r,
                                          uint32_t frames);
void brick6_looper_runtime_probe_bus(const float *left,
                                     const float *right,
                                     uint32_t frames);
void brick6_looper_runtime_diag_get_snapshot(brick6_looper_runtime_diag_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* BRICK6_LOOPER_RUNTIME_H */
