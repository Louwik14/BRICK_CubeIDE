#pragma once

#include "param_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    PARAM_REGISTRY_RUNTIME_UI_VALUE_VALID = (uint8_t)(1U << 0),
    PARAM_REGISTRY_RUNTIME_AUDIO_PENDING = (uint8_t)(1U << 1),
    PARAM_REGISTRY_RUNTIME_AUDIO_PENDING_GLOBAL = (uint8_t)(1U << 2)
};

typedef struct
{
    float base_value;
    uint8_t flags;
} param_registry_runtime_ui_value_t;

void param_registry_control_shadow_init(void);
uint8_t param_registry_control_shadow_get(uint8_t track, param_id_t id, float *out_value);
uint8_t param_registry_control_shadow_ui_value_get(uint8_t track,
                                            param_id_t id,
                                            param_registry_runtime_ui_value_t *out_value);
void param_registry_control_shadow_set(uint8_t track, param_id_t id, float value);
void param_registry_control_shadow_mark_pending(uint8_t track, param_id_t id);
void param_registry_control_shadow_set_pending(uint8_t track, param_id_t id, float value);
void param_registry_control_shadow_set_pending_global(param_id_t id, float value);
void param_registry_control_shadow_mark_published(uint8_t track, param_id_t id);
uint8_t param_registry_control_shadow_is_pending(uint8_t track, param_id_t id);
uint8_t param_registry_control_shadow_is_pending_global(param_id_t id);
void param_registry_control_shadow_clear_track(uint8_t track);
/* Query helper: pure read of cache/default, no write, no resync. */
uint8_t param_registry_control_shadow_get_or_default(const param_desc_t *registry,
                                              param_id_t id,
                                              uint8_t track,
                                              float *out_value);

#ifdef __cplusplus
}
#endif
