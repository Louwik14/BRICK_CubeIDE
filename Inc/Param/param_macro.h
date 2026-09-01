#ifndef PARAM_MACRO_H
#define PARAM_MACRO_H

#include <stdint.h>

#include "Param/param_ids.h"

typedef struct
{
    uint8_t scene;
    uint8_t lock;
    uint8_t track;
    param_id_t param;
    float base_value;
    float scene_value;
    float amount;
    float resolved_value;
} param_macro_resolution_t;

void param_macro_init(void);
float param_macro_lerp(float base_value, float scene_value, float amount);
uint8_t param_macro_lock_target_is_supported(uint8_t track, param_id_t param);
uint8_t param_macro_sync_scene_sources(void);
uint8_t param_macro_set_amount(uint8_t macro, float amount);
uint8_t param_macro_adjust_amount(uint8_t macro, int16_t delta);
float param_macro_get_amount(uint8_t macro);
uint8_t param_macro_set_scene_source_amount(uint8_t scene, float amount);
void param_macro_release_scene_source(uint8_t scene);
uint8_t param_macro_get_ui_held_scene(uint8_t macro, uint8_t *out_scene);
uint8_t param_macro_resolve_lock(uint8_t scene,
                                 uint8_t lock,
                                 param_macro_resolution_t *out_resolution);
uint8_t param_macro_apply_resolution(const param_macro_resolution_t *resolution);

#endif /* PARAM_MACRO_H */
