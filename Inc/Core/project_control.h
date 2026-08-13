#ifndef PROJECT_CONTROL_H
#define PROJECT_CONTROL_H

#include <stdint.h>
#include "Storage/persistent_control_model.h"
#include "Param/param_registry.h"

typedef enum { PROJECT_CONTROL_HALL_SCENE=0, PROJECT_CONTROL_HALL_SWITCH=1 } project_control_hall_mode_t;
typedef struct { uint8_t track; param_id_t param; float scene_value; } project_control_macro_lock_t;

void project_control_init(void);
project_control_hall_mode_t project_control_get_hall_mode(void);
uint8_t project_control_set_hall_mode(project_control_hall_mode_t mode);
uint8_t project_control_get_macro_scene(uint8_t macro);
uint8_t project_control_set_macro_scene(uint8_t macro,uint8_t scene);
uint8_t project_control_get_scene_lock(uint8_t scene,uint8_t lock,project_control_macro_lock_t*out);
uint8_t project_control_set_scene_lock(uint8_t scene,uint8_t lock,const project_control_macro_lock_t*in);
uint8_t project_control_scene_lock_is_empty(uint8_t scene,uint8_t lock);
uint8_t project_control_capture_macros(persist_control_macros_t*out);
uint8_t project_control_apply_macros(const persist_control_macros_t*in);

uint8_t project_control_set_entity_asset(uint8_t entity,uint32_t kind,const char*path);
uint8_t project_control_clear_entity_asset(uint8_t entity);
uint8_t project_control_get_entity_asset(uint8_t entity,persist_control_asset_ref_t*out);
uint16_t project_control_capture_assets(persist_control_asset_ref_t*out,uint16_t capacity);
uint8_t project_control_apply_assets(const persist_control_asset_ref_t*assets,uint16_t count,const persist_control_pattern_t*pattern);

/* Logical Project banks. Values stored in parameters and p-locks are these
 * logical indices; runtime pool/global/instrument slots are deliberately not
 * exposed as persistent identities. */
uint8_t project_control_register_sample_runtime(uint32_t kind,const char*path,uint16_t runtime_global,uint16_t*out_logical);
uint8_t project_control_register_wavetable_runtime(const char*path,uint16_t runtime_global,uint16_t*out_logical);
uint8_t project_control_register_multi_runtime(const char*path,uint16_t runtime_instrument,uint16_t*out_logical);
void project_control_unregister_sample_runtime(uint16_t runtime_global);
void project_control_unregister_wavetable_runtime(uint16_t runtime_global);
void project_control_unregister_multi_runtime(uint16_t runtime_instrument);
uint8_t project_control_resolve_sample_runtime(uint16_t logical,uint16_t*out_runtime_global,uint32_t*out_kind);
uint8_t project_control_resolve_wavetable_runtime(uint16_t logical,uint16_t*out_runtime_global);
uint8_t project_control_resolve_multi_runtime(uint16_t logical,uint16_t*out_runtime_instrument);

#endif
