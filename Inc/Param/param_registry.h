#pragma once

#include "param_store.h"
#include "Param/param_value_policy.h"
#include "Track/track_types.h"

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

typedef struct param_desc
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

    param_value_policy_t value_policy;

    void (*apply)(float value);

} param_desc_t;

extern const param_desc_t param_registry[PARAM_COUNT];

typedef struct
{
    param_id_t id;
    uint8_t track;
    float value;
} param_registry_track_edit_cmd_t;

typedef struct
{
    param_id_t id;
    float value;
} param_registry_prepared_value_t;

void param_registry_init(void);

/* Query surface: pure reads only. */
uint8_t param_registry_get_track_value(param_id_t id, uint8_t track, float *out_value);
float param_get(param_id_t id);
uint8_t param_registry_is_audio_fx_param(param_id_t id);
param_id_t param_registry_get_audio_fx_param(uint8_t order);

/* Command / apply / post-commit surface. */
void param_registry_sync_filter_ui_for_active_track(void);
void param_registry_batch_begin(void);
void param_registry_batch_end(void);
uint8_t param_registry_apply_track_edit(const param_registry_track_edit_cmd_t *cmd);
uint8_t param_registry_apply_track_value(param_id_t id, uint8_t track, float value);
/* Common validation/conversion seam.  Prepared values are clamped canonical
 * values and contain no transport or runtime side effect. */
uint8_t param_registry_prepare_value(param_id_t id,
                                     float value,
                                     param_registry_prepared_value_t *out_value);
uint8_t param_registry_track_value_is_audio_command(param_id_t id,uint8_t track);
/* CONTROL target-only seam used by bulk preparation/commit work.  It performs
 * no queue publication, retry bookkeeping, transition or UI synchronization. */
uint8_t param_registry_install_prepared_track_control_target(
    const param_registry_prepared_value_t *prepared,
    uint8_t track);
/* Global counterpart used after an AUDIO-owned bulk restore commit. */
uint8_t param_registry_install_prepared_global_control_target(
    const param_registry_prepared_value_t *prepared);
/* AUDIO projection only.  effective_muted is derived from local ownership and
 * must never be written back into the local CONTROL mute authority. */
uint8_t param_registry_project_track_effective_mute(uint8_t track,
                                                    uint8_t effective_muted);
uint8_t param_registry_project_track_base_audio(param_id_t id,
                                                uint8_t track,
                                                float value);
uint8_t param_registry_publish_track_base_audio(param_id_t id,
                                                uint8_t track,
                                                float value);
uint8_t param_registry_is_lfo_param(param_id_t id);
void param_registry_release_track_value_runtime_temp(param_id_t id, uint8_t track);
void param_registry_clear_track_runtime_state(uint8_t track);
/* Control-side projection: converts a canonical global value into the
 * complete command payload consumed by AUDIO. */
uint8_t param_registry_prepare_global_audio_command(param_id_t id,
                                                    float canonical_value,
                                                    float *out_command_value);
void param_set(param_id_t id, float value);
void param_reset(param_id_t id);
#ifdef __cplusplus
}
#endif
