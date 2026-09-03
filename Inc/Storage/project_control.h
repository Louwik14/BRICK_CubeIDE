#ifndef PROJECT_CONTROL_H
#define PROJECT_CONTROL_H

#include <stdint.h>
#include "Storage/persistent_control_model.h"
#include "Param/param_registry.h"

typedef enum { PROJECT_CONTROL_HALL_SCENE=0, PROJECT_CONTROL_HALL_SWITCH=1 } project_control_hall_mode_t;
typedef enum {
    PROJECT_CONTROL_ASSET_FAILED = 0,
    PROJECT_CONTROL_ASSET_READY,
    PROJECT_CONTROL_ASSET_PENDING,
    PROJECT_CONTROL_ASSET_FAILED_INTERNAL
} project_control_asset_result_t;
typedef struct { uint8_t track; param_id_t param; float scene_value; } project_control_macro_lock_t;
typedef enum {
    PROJECT_CONTROL_ASSET_SAMPLER = 0,
    PROJECT_CONTROL_ASSET_WAVE_OSC1,
    PROJECT_CONTROL_ASSET_WAVE_OSC2,
    PROJECT_CONTROL_ASSET_ROLE_COUNT
} project_control_asset_role_t;

void project_control_init(void);
void project_control_reset_macros(void);
uint8_t project_control_get_default_macros(persist_control_macros_t *out);
void project_control_reset_asset_banks(void);
project_control_hall_mode_t project_control_get_hall_mode(void);
uint8_t project_control_set_hall_mode(project_control_hall_mode_t mode);
uint8_t project_control_get_macro_scene(uint8_t macro);
uint8_t project_control_set_macro_scene(uint8_t macro,uint8_t scene);
uint8_t project_control_get_scene_lock(uint8_t scene,uint8_t lock,project_control_macro_lock_t*out);
uint8_t project_control_set_scene_lock(uint8_t scene,uint8_t lock,const project_control_macro_lock_t*in);
uint8_t project_control_scene_lock_is_empty(uint8_t scene,uint8_t lock);
uint8_t project_control_scene_has_locks(uint8_t scene);
uint8_t project_control_get_scene_lock_for_param(uint8_t scene,uint8_t track,param_id_t param,project_control_macro_lock_t*out);
uint8_t project_control_assign_scene_lock(uint8_t scene,uint8_t track,param_id_t param,float value);
uint8_t project_control_clear_scene_lock(uint8_t scene,uint8_t track,param_id_t param);
uint8_t project_control_capture_macros(persist_control_macros_t*out);
const persist_control_macros_t*project_control_macros_view(void);
uint8_t project_control_apply_macros(const persist_control_macros_t*in);

uint16_t project_control_asset_count(void);
uint8_t project_control_get_asset_ordinal(uint16_t ordinal,persist_control_asset_ref_t*out);
uint8_t project_control_begin_asset_restore(void);
uint8_t project_control_validate_asset(const persist_control_asset_ref_t*asset);
uint8_t project_control_find_asset(uint32_t kind,const char*path,uint16_t*out_logical);

/* Logical Project banks are lookup tables only. Track selection authority is
 * a typed asset reference; runtime pool/global/instrument slots are resolved
 * only when publishing to AUDIO and are never parameters or p-lock targets. */
uint8_t project_control_register_sample_runtime(uint32_t kind,const char*path,uint16_t runtime_global,uint16_t*out_logical);
uint8_t project_control_register_wavetable_runtime(const char*path,uint16_t runtime_global,uint16_t*out_logical);
uint8_t project_control_register_multi_runtime(const char*path,uint16_t runtime_instrument,uint16_t*out_logical);
project_control_asset_result_t project_control_complete_multi_runtime(
    uint16_t logical,const char*path,uint16_t runtime_instrument,uint8_t success);
uint8_t project_control_remove_sample(uint16_t logical);
uint8_t project_control_remove_wavetable(uint16_t logical);
uint8_t project_control_remove_multi(uint16_t logical);
uint8_t project_control_has_sample(uint16_t logical,uint32_t *out_kind);
/* Ordered, kind-filtered projections expose UI ordinals without changing the
 * logical IDs stored by parameters and persistence. */
uint16_t project_control_sample_projection_count(uint32_t kind);
uint8_t project_control_sample_logical_at_ordinal(uint32_t kind,uint16_t ordinal,uint16_t*out_logical);
uint8_t project_control_sample_ordinal_for_logical(uint32_t kind,uint16_t logical,uint16_t*out_ordinal);
uint8_t project_control_has_wavetable(uint16_t logical);
uint8_t project_control_has_multi(uint16_t logical);
uint16_t project_control_list_samples(uint32_t kind,uint16_t *out,uint16_t capacity);
uint16_t project_control_list_wavetables(uint16_t *out,uint16_t capacity);
uint16_t project_control_list_multis(uint16_t *out,uint16_t capacity);
uint8_t project_control_get_logical_asset(uint32_t kind,uint16_t logical,persist_control_asset_ref_t*out);
uint8_t project_control_resolve_sample_runtime(uint16_t logical,uint16_t*out_runtime_global,uint32_t*out_kind);
uint8_t project_control_resolve_wavetable_runtime(uint16_t logical,uint16_t*out_runtime_global);
uint8_t project_control_resolve_multi_runtime(uint16_t logical,uint16_t*out_runtime_instrument);
uint8_t project_control_track_asset_get(uint8_t entity,
                                        project_control_asset_role_t role,
                                        persist_control_asset_ref_t *out_asset);
uint8_t project_control_track_asset_get_logical(uint8_t entity,
                                                project_control_asset_role_t role,
                                                uint16_t *out_logical);
uint8_t project_control_track_asset_select_logical(uint8_t entity,
                                                   project_control_asset_role_t role,
                                                   uint16_t logical);
uint8_t project_control_track_asset_restore(uint8_t entity,
                                            project_control_asset_role_t role,
                                            const persist_control_asset_ref_t *asset);
project_control_asset_result_t project_control_track_asset_restore_status(
    uint8_t entity, project_control_asset_role_t role,
    const persist_control_asset_ref_t *asset);
uint8_t project_control_track_assets_clear(uint8_t entity);

#endif
