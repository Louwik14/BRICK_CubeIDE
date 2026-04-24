#ifndef PARAM_MACRO_H
#define PARAM_MACRO_H

#include <stdint.h>

#include "param_store.h"

typedef struct
{
    uint8_t bank;
    uint8_t macro;
    uint8_t slot;
    uint8_t track;
    param_id_t param;
    float base_value;
    float scene_value;
    float amount;
    float resolved_value;
} param_macro_resolution_t;

void param_macro_init(void);
float param_macro_lerp(float base_value, float scene_value, float amount);
uint8_t param_macro_slot_target_is_supported(uint8_t track, param_id_t param);
void param_macro_sync_active_bank(void);
uint8_t param_macro_set_amount(uint8_t macro, float amount);
uint8_t param_macro_adjust_amount(uint8_t macro, int16_t delta);
float param_macro_get_amount(uint8_t macro);
uint8_t param_macro_resolve_slot(uint8_t bank,
                                 uint8_t macro,
                                 uint8_t slot,
                                 param_macro_resolution_t *out_resolution);
uint8_t param_macro_apply_resolution(const param_macro_resolution_t *resolution);
uint8_t param_macro_apply_slot(uint8_t bank, uint8_t macro, uint8_t slot, float amount);

#endif /* PARAM_MACRO_H */
