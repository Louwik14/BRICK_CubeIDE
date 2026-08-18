#pragma once

#include "param_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

void param_filter_init(void);
uint8_t param_filter_is_param(param_id_t id);
uint8_t param_filter_get_track_value(param_id_t id, uint8_t track, float *out_value);
uint8_t param_filter_apply_value(param_id_t id,
                                 uint8_t track,
                                 float clamped,
                                 uint8_t update_shadow_state,
                                 uint8_t resync_lfo_base);
void param_filter_sync_ui_for_active_track(void);

float param_filter_ui127_to_attack_s(float v);
float param_filter_ui127_to_decay_s(float v);
float param_filter_ui127_to_sustain(float v);
float param_filter_ui127_to_release_s(float v);
float param_filter_ui127_to_cutoff_hz(float v);
float param_filter_ui127_to_resonance(float v);
float param_filter_ui127_to_eg_amount(float v);
float param_filter_ui127_to_keytrack(float v);
float param_filter_eq_ui127_to_db(float v);

void apply_filter_morph(float v);
void apply_filter_cutoff(float v);
void apply_filter_resonance(float v);
void apply_filter_eg_amount(float v);
void apply_filter_attack(float v);
void apply_filter_decay(float v);
void apply_filter_sustain(float v);
void apply_filter_release(float v);
void apply_filter_keytrack(float v);
void apply_filter_env_reset(float v);
void apply_filter_env_delay(float v);
void apply_filter_drive(float v);
void apply_filter_decimator_bits(float v);
void apply_filter_decimator_rate(float v);
void apply_filter_decimator_rate2(float v);

#ifdef __cplusplus
}
#endif
