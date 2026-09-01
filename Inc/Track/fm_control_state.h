#ifndef FM_CONTROL_STATE_H
#define FM_CONTROL_STATE_H

#include <stdint.h>

#include "IPC/fm_dsp_projection.h"
#include "Param/param_ids.h"
#include "Track/entity_topology.h"

typedef struct
{
    track_tone_fm_base_voice_t base;
    track_tone_fm_macros_t macros;
} fm_control_state_t;

void fm_control_state_init(void);
uint8_t fm_control_state_reset(uint8_t entity);
uint8_t fm_control_state_get(uint8_t entity, fm_control_state_t *out_state);
uint8_t fm_control_state_restore(uint8_t entity,
                                 const fm_control_state_t *state);
uint8_t fm_control_state_validate(const fm_control_state_t *state);
uint8_t fm_control_state_set_public_param(uint8_t entity,
                                          param_id_t id,
                                          float value);
uint8_t fm_control_state_get_public_param(uint8_t entity,
                                          param_id_t id,
                                          float *out_value);

#endif
