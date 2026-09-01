#pragma once

#include "param_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float morph, cutoff, resonance, eg_amount;
    float attack, decay, sustain, release;
    float keytrack, env_reset, env_delay;
    float drive, decimator_bits, decimator_rate, decimator_rate2;
    float retrigger;
} param_filter_control_state_t;

void param_filter_init(void);
uint8_t param_filter_control_reset(uint8_t track);
uint8_t param_filter_is_param(param_id_t id);
uint8_t param_filter_control_get(uint8_t track, param_id_t id,
                                 float *out_value);
uint8_t param_filter_control_set(uint8_t track, param_id_t id, float value);
uint8_t param_filter_control_capture(uint8_t track,
                                     param_filter_control_state_t *out_state);
uint8_t param_filter_control_validate(const param_filter_control_state_t *state);
uint8_t param_filter_control_restore(uint8_t track,
                                     const param_filter_control_state_t *state);
uint8_t param_filter_apply_value(param_id_t id,
                                 uint8_t track,
                                 float clamped,
                                 uint8_t update_control_value);

float param_filter_eq_ui127_to_db(float v);

#ifdef __cplusplus
}
#endif
