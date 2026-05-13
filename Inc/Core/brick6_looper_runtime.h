#ifndef BRICK6_LOOPER_RUNTIME_H
#define BRICK6_LOOPER_RUNTIME_H

#include <stdint.h>

#include "Core/track_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_LOOPER_RUNTIME_DIAG_PATH_MAX 96U

typedef enum
{
    BRICK6_LOOPER_RUNTIME_SOURCE_NONE = 0,
    BRICK6_LOOPER_RUNTIME_SOURCE_RAW = 1,
    BRICK6_LOOPER_RUNTIME_SOURCE_WAV = 2
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
    uint8_t raw_slot;
    uint8_t play_auto;
    uint8_t scheduled_start_valid;
    uint8_t first_output_valid;
    uint8_t page_miss_seen;
    uint8_t state;
    uint8_t source;
    uint8_t cache_registered;
    char active_path[BRICK6_LOOPER_RUNTIME_DIAG_PATH_MAX];
} brick6_looper_runtime_diag_snapshot_t;

void brick6_looper_runtime_init(void);
void brick6_looper_runtime_service(uint32_t byte_budget);
uint8_t brick6_looper_runtime_has_pending_sd_work(void);
void brick6_looper_runtime_notify_raw_take_ready(uint8_t track_id,
                                                 uint8_t raw_slot,
                                                 const char *raw_path,
                                                 uint32_t recorded_frames,
                                                 uint8_t play_auto,
                                                 uint64_t scheduled_start_sample);
void brick6_looper_runtime_notify_preroll_take_ready(uint8_t track_id,
                                                     uint8_t raw_slot,
                                                     uint32_t expected_frames,
                                                     uint8_t play_auto,
                                                     uint64_t scheduled_start_sample);
void brick6_looper_runtime_stop_playback(uint8_t track_id);
void brick6_looper_runtime_prepare_replace(uint8_t track_id);
void brick6_looper_runtime_arm_record_start(uint8_t track_id,
                                            uint8_t raw_slot,
                                            uint8_t len_mode,
                                            uint32_t expected_frames,
                                            uint8_t play_auto,
                                            uint64_t request_sample);
void brick6_looper_runtime_arm_record_stop(uint64_t request_sample);
uint8_t brick6_looper_runtime_record_is_active_or_armed(void);
uint8_t brick6_looper_runtime_get_record_capture_track(uint8_t *out_track);
void brick6_looper_runtime_preroll_begin(uint8_t track_id, uint8_t raw_slot);
void brick6_looper_runtime_preroll_capture_from_irq(uint8_t track_id,
                                                    const int32_t *lr_interleaved,
                                                    uint32_t frames);
void brick6_looper_runtime_set_play_auto(uint8_t track_id, uint8_t play_auto);
void brick6_looper_runtime_set_stretch(uint8_t track_id,
                                       uint8_t mode,
                                       float pitch_semitones,
                                       uint16_t grain_frames);
void brick6_looper_runtime_on_transport_start(void);
void brick6_looper_runtime_on_transport_stop(void);
void brick6_looper_runtime_on_boundary_edge(uint8_t track_id, uint64_t sample_time);
uint8_t brick6_looper_runtime_next_start_offset(uint64_t block_start_sample,
                                                uint32_t block_frames,
                                                uint16_t *out_offset);
void brick6_looper_runtime_on_scheduled_start(uint64_t sample_time);
uint8_t brick6_looper_runtime_is_ready(uint8_t track_id);
uint8_t brick6_looper_runtime_is_playing(uint8_t track_id);
void brick6_looper_runtime_render_track(const track_runtime_ctx_t *ctx,
                                        float *out_l,
                                        float *out_r,
                                        uint32_t frames);
void brick6_looper_runtime_diag_get_snapshot(brick6_looper_runtime_diag_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* BRICK6_LOOPER_RUNTIME_H */
