#pragma once

#include <stdint.h>
#include "Core/brick_build_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    AUDIO_TRACK_DIAG_ENG = 0,
    AUDIO_TRACK_DIAG_FILTER_IN,
    AUDIO_TRACK_DIAG_FILTER_OUT,
    AUDIO_TRACK_DIAG_DSP,
    AUDIO_TRACK_DIAG_BUS,
    AUDIO_TRACK_DIAG_STAGE_COUNT
} audio_track_diag_stage_t;

typedef struct
{
    float peak[AUDIO_TRACK_DIAG_STAGE_COUNT];
    float rms_energy[AUDIO_TRACK_DIAG_STAGE_COUNT];
    float k_weighted_energy[AUDIO_TRACK_DIAG_STAGE_COUNT];
    float signed_sum[AUDIO_TRACK_DIAG_STAGE_COUNT];
    uint32_t samples[AUDIO_TRACK_DIAG_STAGE_COUNT];
    uint32_t soft_clip_count;
    uint32_t filter_clip_count;
    uint32_t insert_clip_count;
    uint8_t active;
    uint8_t filter_active;
    uint8_t soft_clip_available;
    uint8_t _pad;
} audio_track_diag_snapshot_t;

typedef enum
{
    AUDIO_GLOBAL_DIAG_DRY_SUM = 0,
    AUDIO_GLOBAL_DIAG_SEND1,
    AUDIO_GLOBAL_DIAG_SEND2,
    AUDIO_GLOBAL_DIAG_DELAY_RETURN,
    AUDIO_GLOBAL_DIAG_REVERB_RETURN,
    AUDIO_GLOBAL_DIAG_POST_RETURNS,
    AUDIO_GLOBAL_DIAG_MASTER_FX_IN,
    AUDIO_GLOBAL_DIAG_MASTER_FX_OUT,
    AUDIO_GLOBAL_DIAG_POST_PREVIEW,
    AUDIO_GLOBAL_DIAG_POST_MASTER_GAIN,
    AUDIO_GLOBAL_DIAG_PRE_PCM24,
    AUDIO_GLOBAL_DIAG_DMA_MAIN,
    AUDIO_GLOBAL_DIAG_STAGE_COUNT
} audio_global_diag_stage_t;

typedef enum
{
    AUDIO_GLOBAL_DIAG_STATE_NA = 0,
    AUDIO_GLOBAL_DIAG_STATE_BYPASS,
    AUDIO_GLOBAL_DIAG_STATE_MEASURED
} audio_global_diag_state_t;

typedef struct
{
    float peak_l[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    float peak_r[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    float energy_l[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    float energy_r[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    uint32_t samples[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    uint32_t nonfinite_count[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    uint32_t over_full_scale_count[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    uint8_t state[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    uint8_t active_audio_tracks;
    uint8_t _pad[3];
    uint32_t final_clip_count;
    float final_clip_max_over;
    uint32_t master_fx_clamp_count;
    float master_fx_clamp_max_over;
    uint32_t delay_clamp_count;
    float delay_clamp_max_over;
    uint8_t delay_clamp_available;
    uint8_t reverb_clamp_available;
    uint8_t master_fx_clamp_available;
    uint8_t final_clip_available;
} audio_global_diag_snapshot_t;

#if BRICK_TEST_BUILD

void audio_track_diag_open(uint8_t logical_track, uint8_t mix_track, uint8_t soft_clip_available);
void audio_track_diag_close(void);
void audio_track_diag_select(uint8_t logical_track, uint8_t mix_track, uint8_t soft_clip_available);
uint8_t audio_track_diag_is_enabled(void);
uint8_t audio_track_diag_is_selected_mix_track(uint8_t mix_track);
uint8_t audio_track_diag_is_selected_logical_track(uint8_t logical_track);
void audio_track_diag_set_lane_active(uint8_t active);
void audio_track_diag_set_filter_active(uint8_t active);
void audio_track_diag_measure_stereo(audio_track_diag_stage_t stage,
                                     const float *left,
                                     const float *right,
                                     uint32_t frames);
void audio_track_diag_measure_mono(audio_track_diag_stage_t stage,
                                   const float *mono,
                                   uint32_t frames);
void audio_track_diag_measure_sample(audio_track_diag_stage_t stage, float left, float right);
void audio_track_diag_end_block(uint32_t frames);
void audio_track_diag_filter_scope(uint8_t enabled);
void audio_track_diag_report_filter_clip(void);
void audio_track_diag_report_stack_soft_clips(uint8_t logical_track, uint32_t count);
void audio_track_diag_report_insert_clip(uint8_t logical_track);
void audio_track_diag_reset(uint8_t logical_track);
void audio_track_diag_reset_all(void);
uint8_t audio_track_diag_read(uint8_t logical_track, audio_track_diag_snapshot_t *out);
void audio_global_diag_set_active_tracks(uint8_t count);
void audio_global_diag_set_stage_state(audio_global_diag_stage_t stage,
                                       audio_global_diag_state_t state);
void audio_global_diag_measure_stereo(audio_global_diag_stage_t stage,
                                      const float *left,
                                      const float *right,
                                      uint32_t frames);
void audio_global_diag_measure_sample(audio_global_diag_stage_t stage,
                                      float left,
                                      float right);
void audio_global_diag_measure_three(audio_global_diag_stage_t stage1,
                                     const float *left1,
                                     const float *right1,
                                     audio_global_diag_stage_t stage2,
                                     const float *left2,
                                     const float *right2,
                                     audio_global_diag_stage_t stage3,
                                     const float *left3,
                                     const float *right3,
                                     uint32_t frames);
void audio_global_diag_report_final_pcm24(float input, float clipped);
void audio_global_diag_report_master_fx_clamp(float input, float clipped);
void audio_global_diag_report_delay_clamp(float input, float clipped);
void audio_global_diag_end_block(uint32_t frames);
void audio_global_diag_reset(void);
uint8_t audio_track_diag_read_coherent(uint8_t logical_track,
                                       audio_track_diag_snapshot_t *track,
                                       audio_global_diag_snapshot_t *global);

#else

#define audio_track_diag_open(...) ((void)0)
#define audio_track_diag_close() ((void)0)
#define audio_track_diag_select(...) ((void)0)
#define audio_track_diag_is_enabled() (0U)
#define audio_track_diag_is_selected_mix_track(...) (0U)
#define audio_track_diag_is_selected_logical_track(...) (0U)
#define audio_track_diag_set_lane_active(...) ((void)0)
#define audio_track_diag_set_filter_active(...) ((void)0)
#define audio_track_diag_measure_stereo(...) ((void)0)
#define audio_track_diag_measure_mono(...) ((void)0)
#define audio_track_diag_measure_sample(...) ((void)0)
#define audio_track_diag_end_block(...) ((void)0)
#define audio_track_diag_filter_scope(...) ((void)0)
#define audio_track_diag_report_filter_clip() ((void)0)
#define audio_track_diag_report_stack_soft_clips(...) ((void)0)
#define audio_track_diag_report_insert_clip(...) ((void)0)
#define audio_track_diag_reset(...) ((void)0)
#define audio_track_diag_reset_all() ((void)0)
#define audio_track_diag_read(...) (0U)
#define audio_global_diag_set_active_tracks(...) ((void)0)
#define audio_global_diag_set_stage_state(...) ((void)0)
#define audio_global_diag_measure_stereo(...) ((void)0)
#define audio_global_diag_measure_sample(...) ((void)0)
#define audio_global_diag_measure_three(...) ((void)0)
#define audio_global_diag_report_final_pcm24(...) ((void)0)
#define audio_global_diag_report_master_fx_clamp(...) ((void)0)
#define audio_global_diag_report_delay_clamp(...) ((void)0)
#define audio_global_diag_end_block(...) ((void)0)
#define audio_global_diag_reset() ((void)0)
#define audio_track_diag_read_coherent(...) (0U)

#endif

#ifdef __cplusplus
}
#endif
