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
#include "Sampler/sample_cache.h"
#include "Sampler/sample_voice_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BRICK6_SAMPLER_DIAG_ENABLE
#define BRICK6_SAMPLER_DIAG_ENABLE 0
#endif

typedef struct
{
    uint32_t fast_path_blocks;
    uint32_t slow_path_blocks;
    uint32_t mixed_segments;
    uint32_t segment_cursor_blocks;
    uint32_t render_track_calls;
    uint32_t active_voices;
    uint32_t max_active_voices;
} brick6_sampler_runtime_diag_snapshot_t;

extern volatile sample_cache_diag_snapshot_t g_sample_cache_diag_snapshot;
extern volatile sample_voice_reader_diag_snapshot_t g_sample_voice_reader_diag_snapshot;
extern volatile brick6_sampler_runtime_diag_snapshot_t g_brick6_sampler_runtime_diag_snapshot;

void brick6_sampler_runtime_init(void);
void brick6_sampler_runtime_reset_track(uint8_t track_id);
void brick6_sampler_runtime_set_sample(uint8_t track_id, uint16_t sample_id);
void brick6_sampler_runtime_set_gain(uint8_t track_id, float gain);
void brick6_sampler_runtime_set_start(uint8_t track_id, float start);
void brick6_sampler_runtime_set_end(uint8_t track_id, float end);
void brick6_sampler_runtime_set_mode(uint8_t track_id, uint8_t mode);
void brick6_sampler_runtime_set_tune(uint8_t track_id, float tune);
void brick6_sampler_runtime_set_fade_in(uint8_t track_id, float fade_in);
void brick6_sampler_runtime_set_fade_out(uint8_t track_id, float fade_out);
void brick6_sampler_runtime_set_slice_count(uint8_t track_id, uint8_t slice_count);
void brick6_sampler_runtime_trigger(uint8_t track_id);
void brick6_sampler_runtime_trigger_note(uint8_t track_id, uint8_t note);
void brick6_sampler_runtime_stop(uint8_t track_id);
void brick6_sampler_runtime_render_track(const track_runtime_ctx_t *ctx,
                                         float *out_l,
                                         float *out_r,
                                         uint32_t frames);
void brick6_sampler_runtime_diag_reset(void);
void brick6_sampler_runtime_diag_get_snapshot(brick6_sampler_runtime_diag_snapshot_t *out_snapshot);
void sampler_perf_diag_capture(void);

#ifdef __cplusplus
}
#endif
