#pragma once

#include <stdint.h>

#include "Sampler/sample_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BRICK6_SAMPLER_DIAG_ENABLE
#define BRICK6_SAMPLER_DIAG_ENABLE 0
#endif

typedef struct
{
    uint8_t cache_voice_id;
    uint16_t sample_id;
    float position;
    float step;
    uint32_t frame_pos;
    uint8_t active;
} sample_voice_reader_t;

typedef struct
{
    uint32_t render_pitch_forward_calls;
    uint32_t segments_mixed;
    uint32_t span_acquire_calls;
    uint32_t neighbor_span_acquire_calls;
    uint32_t seek_calls;
} sample_voice_reader_diag_snapshot_t;

void sample_voice_reader_reset(sample_voice_reader_t *reader);
void sample_voice_reader_bind(sample_voice_reader_t *reader,
                              uint16_t sample_id,
                              uint8_t cache_voice_id,
                              uint32_t start_frame);
void sample_voice_reader_set_step(sample_voice_reader_t *reader, float step);
void sample_voice_reader_seek(sample_voice_reader_t *reader, uint32_t frame_pos);
void sample_voice_reader_stop(sample_voice_reader_t *reader);
uint8_t sample_voice_reader_begin_block(sample_voice_reader_t *reader,
                                        uint32_t max_frames,
                                        sample_cache_block_t *out_block);
void sample_voice_reader_commit_block(sample_voice_reader_t *reader,
                                      uint32_t consumed_frames);
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
void sample_voice_reader_diag_reset(void);
void sample_voice_reader_diag_get_snapshot(sample_voice_reader_diag_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
