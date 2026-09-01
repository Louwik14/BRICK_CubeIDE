#pragma once

#include <stdint.h>

#include "Param/param_ids.h"

typedef struct { float level, pan, send1, send2, send3; } mixer_control_state_t;

void mixer_control_state_init(void);
uint8_t mixer_control_state_reset(uint8_t entity);
uint8_t mixer_control_state_get_param(uint8_t entity, param_id_t id,
                                      float *out_value);
uint8_t mixer_control_state_set_param(uint8_t entity, param_id_t id,
                                      float value);
uint8_t mixer_control_state_capture(uint8_t entity, mixer_control_state_t *out_state);
uint8_t mixer_control_state_validate(const mixer_control_state_t *state);
uint8_t mixer_control_state_restore(uint8_t entity, const mixer_control_state_t *state);
