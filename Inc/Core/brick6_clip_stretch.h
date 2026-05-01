#ifndef BRICK6_CLIP_STRETCH_H
#define BRICK6_CLIP_STRETCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_CLIP_STRETCH_INPUT_CAPACITY_FRAMES 1024U
#define BRICK6_CLIP_STRETCH_PIPELINE_TEST_ENABLED 0U
#define BRICK6_CLIP_STRETCH_PRESERVE_PITCH_ENABLED 1U
#define BRICK6_CLIP_STRETCH_DEFAULT_GRAIN_FRAMES 256U
#define BRICK6_CLIP_STRETCH_DEFAULT_HOP_FRAMES 128U
#define BRICK6_CLIP_STRETCH_DEFAULT_SEARCH_FRAMES 16U
#define BRICK6_CLIP_STRETCH_MAX_GRAIN_FRAMES 512U
#define BRICK6_CLIP_STRETCH_MAX_SEARCH_FRAMES 16U
#define BRICK6_CLIP_STRETCH_CORRELATION_FRAMES 48U
#define BRICK6_CLIP_STRETCH_OUTPUT_RING_FRAMES 1024U

typedef enum
{
    BRICK6_CLIP_STRETCH_MODE_BYPASS = 0,
    BRICK6_CLIP_STRETCH_MODE_PRESERVE_PITCH
} brick6_clip_stretch_mode_t;

typedef enum
{
    BRICK6_CLIP_STRETCH_SYNC_LEN_OFF = 0,
    BRICK6_CLIP_STRETCH_SYNC_LEN_HALF,
    BRICK6_CLIP_STRETCH_SYNC_LEN_QUARTER,
    BRICK6_CLIP_STRETCH_SYNC_LEN_AUTO
} brick6_clip_stretch_sync_len_t;

typedef enum
{
    BRICK6_CLIP_STRETCH_STATUS_OK = 0,
    BRICK6_CLIP_STRETCH_STATUS_STARVED
} brick6_clip_stretch_status_t;

typedef struct
{
    uint32_t ratio_q16;
    uint16_t grain_size;
    uint16_t hop_size;
    uint16_t search_frames;
    brick6_clip_stretch_mode_t mode;
    brick6_clip_stretch_sync_len_t sync_len;
} brick6_clip_stretch_config_t;

typedef struct
{
    brick6_clip_stretch_config_t config;
    uint32_t write_index;
    uint32_t read_index;
    uint32_t queued_frames;
    uint32_t starve_count;
    uint32_t output_read_index;
    uint32_t output_write_origin;
    uint32_t output_fill;
    uint32_t source_pos_q16;
    uint8_t starved;
    uint8_t prev_overlap_valid;
    uint16_t window_grain_size;
    float input_interleaved[BRICK6_CLIP_STRETCH_INPUT_CAPACITY_FRAMES * 2U];
    float ola_window[BRICK6_CLIP_STRETCH_MAX_GRAIN_FRAMES];
    float prev_overlap_mono[BRICK6_CLIP_STRETCH_CORRELATION_FRAMES];
    float output_ring_l[BRICK6_CLIP_STRETCH_OUTPUT_RING_FRAMES];
    float output_ring_r[BRICK6_CLIP_STRETCH_OUTPUT_RING_FRAMES];
    float output_ring_gain[BRICK6_CLIP_STRETCH_OUTPUT_RING_FRAMES];
} brick6_clip_stretch_t;

typedef struct
{
    uint32_t phase;
    uint32_t track_id;
    uint32_t clip_state;
    uint32_t last_enter_clip_stretch_render;
    uint32_t last_exit_clip_stretch_render;
    uint32_t last_enter_push;
    uint32_t last_exit_push;
    uint32_t last_enter_emit_grain;
    uint32_t last_exit_emit_grain;
    uint32_t last_segment_frames;
    uint32_t last_push_frames;
    uint32_t last_commit_frames;
    uint32_t last_render_requested;
    uint32_t last_render_produced;
    uint32_t last_fifo_level;
    uint32_t last_ring_level;
    uint32_t starved_count;
    uint32_t guard_fail_count;
    uint32_t loop_guard_break_count;
    uint32_t last_error_code;
} brick6_clip_stretch_diag_snapshot_t;

extern volatile brick6_clip_stretch_diag_snapshot_t g_brick6_clip_stretch_diag_snapshot;

void brick6_clip_stretch_init(brick6_clip_stretch_t *stretch);
void brick6_clip_stretch_reset(brick6_clip_stretch_t *stretch);
void brick6_clip_stretch_set_config(brick6_clip_stretch_t *stretch, const brick6_clip_stretch_config_t *config);
uint32_t brick6_clip_stretch_input_capacity(const brick6_clip_stretch_t *stretch);
uint32_t brick6_clip_stretch_push_interleaved(brick6_clip_stretch_t *stretch,
                                              const float *interleaved_stereo,
                                              uint32_t frames);
uint32_t brick6_clip_stretch_push_stereo_stride(brick6_clip_stretch_t *stretch,
                                                const float *left,
                                                const float *right,
                                                uint32_t frames,
                                                uint32_t frame_stride,
                                                uint8_t right_matches_left);
uint32_t brick6_clip_stretch_render(brick6_clip_stretch_t *stretch,
                                    float *out_left,
                                    float *out_right,
                                    uint32_t frames);
uint8_t brick6_clip_stretch_is_starved(const brick6_clip_stretch_t *stretch);
brick6_clip_stretch_status_t brick6_clip_stretch_get_status(const brick6_clip_stretch_t *stretch);

#ifdef __cplusplus
}
#endif

#endif /* BRICK6_CLIP_STRETCH_H */
