#pragma once

#include "param_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    PARAM_DISPLAY_FLOAT,
    PARAM_DISPLAY_DB,
    PARAM_DISPLAY_PERCENT,
    PARAM_DISPLAY_BOOL,
    PARAM_DISPLAY_ENUM,
    PARAM_DISPLAY_TIME_MS,
    PARAM_DISPLAY_RATIO,
    PARAM_DISPLAY_INT
} param_display_type_t;

typedef enum
{
    PARAM_TYPE_FLOAT,
    PARAM_TYPE_INT,
    PARAM_TYPE_ENUM,
    PARAM_TYPE_BOOL,
    PARAM_TYPE_BIPOLAR
} param_type_t;

typedef struct
{
    param_id_t id;

    const char *name;

    param_type_t type;

    float min;
    float max;
    float step;

    float default_value;

    param_display_type_t display_type;

    const char *unit;
    const char *const *labels;

    void (*apply)(float value);

} param_desc_t;

extern const param_desc_t param_registry[PARAM_COUNT];

void param_registry_init(void);
void param_registry_sync_filter_ui_for_active_track(void);
void param_registry_sync_ui_for_active_track(void);
void param_registry_batch_begin(void);
void param_registry_batch_end(void);
void param_registry_capture_runtime_mix_targets(uint8_t *out_mix_tracks);
void param_registry_finalize_track_structure_change(const uint8_t *previous_mix_tracks);
void param_registry_track_structure_transition_begin(void);
void param_registry_track_structure_transition_end(void);
uint8_t param_registry_track_structure_transition_is_active(void);
uint8_t param_registry_get_track_value(param_id_t id, uint8_t track, float *out_value);
uint8_t param_registry_apply_track_value(param_id_t id, uint8_t track, float value);
uint8_t param_registry_apply_track_value_rt_fast(param_id_t id, uint8_t track, float value);
uint8_t param_registry_is_legacy_physical_mix_param(param_id_t id);

float param_get(param_id_t id);
void param_set(param_id_t id, float value);
void param_reset(param_id_t id);

#ifdef __cplusplus
}
#endif
