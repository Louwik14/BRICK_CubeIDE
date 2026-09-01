#pragma once

#include <stdint.h>

#include "Param/param_ids.h"

typedef struct
{
    float attack, decay, sustain, release, filter_mode, retrigger;
} vca_control_state_t;

void vca_control_state_init(void);
uint8_t vca_control_state_reset(uint8_t entity);
uint8_t vca_control_state_get_param(uint8_t entity, param_id_t id,
                                    float *out_value);
uint8_t vca_control_state_set_param(uint8_t entity, param_id_t id,
                                    float value);
uint8_t vca_control_state_capture(uint8_t entity, vca_control_state_t *out_state);
uint8_t vca_control_state_validate(const vca_control_state_t *state);
uint8_t vca_control_state_restore(uint8_t entity, const vca_control_state_t *state);
