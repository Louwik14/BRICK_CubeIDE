#pragma once

#include "Param/param_ids.h"
#include "Track/track_types.h"

typedef struct track_audio_runtime_ctx_s track_audio_runtime_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

uint8_t param_backend_apply_tone_prism(uint8_t track, param_id_t id, float value);
uint8_t param_backend_apply_tone_fm(uint8_t track, param_id_t id, float value);
uint8_t param_backend_apply_prepared_track_value_audio(
    uint8_t track,
    param_id_t id,
    float value);

uint8_t param_backend_apply_tone_sampler(uint8_t track, param_id_t id, float value);
uint8_t param_backend_apply_tone_looper(uint8_t track, param_id_t id, float value);
uint8_t param_backend_apply_tone_stack(uint8_t track, param_id_t id, float value);
uint8_t param_backend_apply_tone_wave(uint8_t track, param_id_t id, float value);
uint8_t param_backend_apply_tone_drum(uint8_t track,
                                      const track_audio_runtime_ctx_t *ctx,
                                      param_id_t id,
                                      float value);
uint8_t param_backend_apply_mix_track(const track_audio_runtime_ctx_t *ctx,
                                      uint8_t track,
                                      param_id_t id,
                                      float value);
uint8_t param_backend_apply_env_track(const track_audio_runtime_ctx_t *ctx, param_id_t id, float value);

#ifdef __cplusplus
}
#endif
