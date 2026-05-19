#pragma once

#include <stdint.h>

#include "Sampler/sample_cache.h"
#include "Sampler/sample_page_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SAMPLE_KERNEL_FWD_1X = 0,
    SAMPLE_KERNEL_REV_1X,
    SAMPLE_KERNEL_PITCH_FWD_LINEAR,
    SAMPLE_KERNEL_PITCH_REV_LINEAR
} sample_kernel_type_t;

typedef struct
{
    sample_audio_key_t key;
    uint16_t sample_id;
    uint32_t start_frame;
    uint32_t region_begin;
    uint32_t region_end;
    uint32_t loop_begin;
    uint32_t loop_end;
    uint32_t fade_in_frames;
    uint32_t fade_out_frames;
    uint32_t step_q16;
    uint8_t direction;
    uint8_t loop_mode;
    uint8_t stop_on_underrun;
    sample_kernel_type_t kernel_type;
} sample_play_plan_t;

typedef enum
{
    SAMPLE_AUDIO_SEGMENT_NOT_READY = 0,
    SAMPLE_AUDIO_SEGMENT_OK,
    SAMPLE_AUDIO_SEGMENT_DONE,
    SAMPLE_AUDIO_SEGMENT_UNDERRUN
} sample_audio_segment_status_t;

typedef struct
{
    const float *l;
    const float *r;
    const float *neighbor_l;
    const float *neighbor_r;
    uint32_t frames;
    uint32_t frame_stride;
    uint32_t start_frame;
    uint32_t source_start_frame;
    uint32_t source_frame_count;
    uint32_t neighbor_start_frame;
    uint32_t neighbor_frame_count;
    uint32_t source_limit_frame;
    uint32_t source_region_begin;
    float source_position;
    float source_step;
    uint8_t is_mono;
    sample_kernel_type_t kernel_type;
    sample_audio_segment_status_t status;
} sample_audio_segment_t;

typedef struct
{
    sample_page_ref_t current_page_ref;
    sample_page_ref_t neighbor_page_ref;
    const float *current_base;
    const float *neighbor_base;
    uint32_t current_start_frame;
    uint32_t current_frame_count;
    uint32_t neighbor_start_frame;
    uint32_t neighbor_frame_count;
    uint32_t current_offset_frames;
    uint8_t current_acquired;
    uint8_t neighbor_acquired;
    uint8_t active;
} sample_audio_cursor_t;

typedef struct
{
    uint8_t cache_voice_id;
    uint8_t cache_voice_valid;
    uint16_t sample_id;
    sample_audio_key_t key;
    float position;
    float step;
    uint32_t frame_pos;
    uint8_t active;
    sample_play_plan_t plan;
    sample_audio_cursor_t audio_cursor;
    uint8_t plan_valid;
} sample_voice_reader_t;

void sample_voice_reader_reset(sample_voice_reader_t *reader);
void sample_voice_reader_bind(sample_voice_reader_t *reader,
                              uint16_t sample_id,
                              uint8_t cache_voice_id,
                              uint32_t start_frame);
uint8_t sample_voice_reader_bind_play_plan(sample_voice_reader_t *reader,
                                           const sample_play_plan_t *plan,
                                           uint8_t cache_voice_id);
void sample_voice_reader_set_step(sample_voice_reader_t *reader, float step);
void sample_voice_reader_seek(sample_voice_reader_t *reader, uint32_t frame_pos);
void sample_voice_reader_stop(sample_voice_reader_t *reader);
uint8_t sample_voice_reader_begin_block(sample_voice_reader_t *reader,
                                        uint32_t max_frames,
                                        sample_cache_block_t *out_block);
void sample_voice_reader_commit_block(sample_voice_reader_t *reader,
                                      uint32_t consumed_frames);
uint8_t sample_voice_reader_begin_segment(sample_voice_reader_t *reader,
                                          uint32_t max_frames,
                                          sample_audio_segment_t *out_segment);
void sample_voice_reader_commit_segment(sample_voice_reader_t *reader,
                                        uint32_t consumed_frames);
void sample_voice_reader_mix_fwd_1x(const sample_audio_segment_t *segment,
                                    float gain,
                                    const float *fade_gain,
                                    uint32_t fade_count,
                                    float *out_l,
                                    float *out_r,
                                    uint32_t out_offset);
void sample_voice_reader_mix_rev_1x(const sample_audio_segment_t *segment,
                                    float gain,
                                    const float *fade_gain,
                                    uint32_t fade_count,
                                    float *out_l,
                                    float *out_r,
                                    uint32_t out_offset);
void sample_voice_reader_mix_pitch_fwd_linear(const sample_audio_segment_t *segment,
                                              float gain,
                                              const float *fade_gain,
                                              uint32_t fade_count,
                                              float *out_l,
                                              float *out_r,
                                              uint32_t out_offset);
void sample_voice_reader_mix_pitch_rev_linear(const sample_audio_segment_t *segment,
                                              float gain,
                                              const float *fade_gain,
                                              uint32_t fade_count,
                                              float *out_l,
                                              float *out_r,
                                              uint32_t out_offset);
uint32_t sample_voice_reader_render_pitch_forward(sample_voice_reader_t *reader,
                                                  uint32_t region_start,
                                                  uint32_t region_end,
                                                  uint8_t *io_reverse,
                                                  uint8_t loop_forward,
                                                  float gain,
                                                  const float *fade_gain,
                                                  uint32_t fade_count,
                                                  float *out_l,
                                                  float *out_r,
                                                  uint32_t frames,
                                                  uint8_t *out_underrun);

#ifdef __cplusplus
}
#endif
