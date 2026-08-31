#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Mod/mod_matrix.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void mod_matrix_set_defaults(
    track_mod_matrix_slot_t slots[MOD_MATRIX_SLOT_COUNT], uint8_t *selected_slot)
{
    if (slots != NULL)
    {
        for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
        {
            slots[slot].enabled = 0U;
            slots[slot].source = (uint8_t)MOD_MATRIX_SOURCE_NONE;
            slots[slot].destination = MOD_DESTINATION_NONE;
            slots[slot].depth = 0.0f;
        }
        slots[0].source = (uint8_t)MOD_MATRIX_SOURCE_LFO1;
        slots[1].source = (uint8_t)MOD_MATRIX_SOURCE_LFO2;
        slots[2].source = (uint8_t)MOD_MATRIX_SOURCE_LFO3;
        slots[3].source = (uint8_t)MOD_MATRIX_SOURCE_ENV3;
    }
    if (selected_slot != NULL) *selected_slot = 0U;
}

uint8_t mod_matrix_set_selected_slot(uint8_t track, float value);
uint8_t mod_matrix_get_selected_slot(uint8_t track, float *out_value);
uint8_t mod_matrix_set_selected_slot_destination_index(uint8_t track, float value);
uint8_t mod_matrix_set_selected_slot_depth(uint8_t track, float value);
uint8_t mod_matrix_set_selected_slot_source(uint8_t track, float value);
uint8_t mod_matrix_get_selected_slot_destination_index(uint8_t track, float *out_value);
uint8_t mod_matrix_get_selected_slot_depth(uint8_t track, float *out_value);
uint8_t mod_matrix_get_selected_slot_source(uint8_t track, float *out_value);
uint8_t mod_matrix_set_slot_destination_index(uint8_t track, uint8_t slot, float value);
uint8_t mod_matrix_set_slot_depth(uint8_t track, uint8_t slot, float value);
uint8_t mod_matrix_set_slot_source(uint8_t track, uint8_t slot, float value);
uint8_t mod_matrix_set_slot_enabled(uint8_t track, uint8_t slot, float value);
uint8_t mod_matrix_set_slot_state(uint8_t track, uint8_t slot, uint8_t source,
                                  mod_destination_address_t destination, float depth,
                                  uint8_t enabled);
uint8_t mod_matrix_get_slot_destination_index(uint8_t track, uint8_t slot, float *out_value);
uint8_t mod_matrix_get_slot_depth(uint8_t track, uint8_t slot, float *out_value);
uint8_t mod_matrix_get_slot_source(uint8_t track, uint8_t slot, float *out_value);
uint8_t mod_matrix_set_multi_source(uint8_t track, uint8_t op, uint8_t input, float value);
uint8_t mod_matrix_get_multi_source(uint8_t track, uint8_t op, uint8_t input, float *out_value);
uint8_t mod_matrix_set_slew_source(uint8_t track, uint8_t op, float value);
uint8_t mod_matrix_get_slew_source(uint8_t track, uint8_t op, float *out_value);
uint8_t mod_matrix_set_slew_amount(uint8_t track, uint8_t op, float value);
uint8_t mod_matrix_get_slew_amount(uint8_t track, uint8_t op, float *out_value);

#ifdef __cplusplus
}
#endif
