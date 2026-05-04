#pragma once

#include "param_store.h"
#include "Core/track_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t param_backend_apply_tone_plaits(uint8_t track, param_id_t id, float value, uint8_t update_base_state);
uint8_t param_backend_apply_tone_braids(uint8_t track, param_id_t id, float value, uint8_t update_base_state);
uint8_t param_backend_is_midi_cc_id(param_id_t id);
uint8_t param_backend_midi_cc_number_from_id(param_id_t id);
uint8_t param_backend_track_supports_midi_tone_ctx(const track_runtime_ctx_t *ctx);
uint8_t param_backend_track_supports_midi_tone_descriptor(const track_runtime_descriptor_t *descriptor);
uint8_t param_backend_send_midi_cc(uint8_t track, param_id_t id, float value);
uint8_t param_backend_apply_track_value(uint8_t track, param_id_t id, float value, uint8_t update_base_state);

uint8_t param_backend_apply_tone_sampler(uint8_t track, param_id_t id, float value, uint8_t update_base_state);
uint8_t param_backend_apply_tone_drum(uint8_t track,
                                      const track_runtime_ctx_t *ctx,
                                      param_id_t id,
                                      float value,
                                      uint8_t update_base_state);
uint8_t param_backend_apply_buffer_track(const track_runtime_ctx_t *ctx,
                                         uint8_t track,
                                         param_id_t id,
                                         float value);
uint8_t param_backend_apply_mix_track(const track_runtime_ctx_t *ctx,
                                      uint8_t track,
                                      param_id_t id,
                                      float value,
                                      uint8_t update_base_state);
uint8_t param_backend_apply_colors_track(const track_runtime_ctx_t *ctx, param_id_t id, float value);

#ifdef __cplusplus
}
#endif
