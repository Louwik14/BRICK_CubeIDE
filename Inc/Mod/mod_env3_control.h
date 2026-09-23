#pragma once

#include <stdint.h>

#include "Param/param_ids.h"

/* Retrigger is the current CONTROL state projected to the persistent
 * retrigger_hard field and to AUDIO. */
typedef struct { float attack, decay, sustain, release, retrigger; } mod_env3_control_state_t;

void mod_env3_control_init(void);
uint8_t mod_env3_control_reset(uint8_t entity);
void mod_env3_control_make_default(mod_env3_control_state_t *out);
uint8_t mod_env3_control_get_param(uint8_t entity, param_id_t id,
                                   float *out_value);
uint8_t mod_env3_control_set_param(uint8_t entity, param_id_t id, float value);
uint8_t mod_env3_control_capture(uint8_t entity, mod_env3_control_state_t *out_state);
uint8_t mod_env3_control_prepare(const mod_env3_control_state_t *state,
                                 mod_env3_control_state_t *out_canonical);
uint8_t mod_env3_control_restore(uint8_t entity, const mod_env3_control_state_t *state);
