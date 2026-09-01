#pragma once

#include "Param/param_ids.h"
#include "Param/param_value_policy.h"
#include "Track/track_types.h"
#include "Track/audio_fx_control_state.h"

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

} param_desc_t;

extern const param_desc_t param_registry[PARAM_COUNT];

typedef enum
{
    PARAM_CONTROL_OWNER_AUDIO_ONLY = 0,
    PARAM_CONTROL_OWNER_TONE,
    PARAM_CONTROL_OWNER_FM,
    PARAM_CONTROL_OWNER_MUTE,
    PARAM_CONTROL_OWNER_AUDIO_FX,
    PARAM_CONTROL_OWNER_FILTER,
    PARAM_CONTROL_OWNER_VCA,
    PARAM_CONTROL_OWNER_ENV3,
    PARAM_CONTROL_OWNER_MIX,
    PARAM_CONTROL_OWNER_POLYPHONY
} param_control_owner_kind_t;

typedef struct
{
    param_id_t id;
    uint8_t track;
    param_control_owner_kind_t owner_kind;
    float canonical_value;
} param_registry_prepared_track_target_t;

typedef struct { param_id_t id; float value; } param_registry_prepared_value_t;

void param_registry_init(void);

/* Query surface: pure reads only. */
uint8_t param_registry_get_track_value(param_id_t id, uint8_t track, float *out_value);
uint8_t param_registry_query_global(param_id_t id, float *out_value);
uint8_t param_registry_is_audio_fx_param(param_id_t id);
param_id_t param_registry_get_audio_fx_param(uint8_t order);

/* Command / post-commit surface. */
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
    const param_registry_prepared_track_target_t *prepared);
uint8_t param_registry_install_prepared_global_control_target(
    param_id_t id, float canonical_value);
uint8_t param_registry_prepare_track_control_target(
    param_id_t id, uint8_t track, float value,
    audio_fx_control_prepare_context_t *audio_fx_context,
    param_registry_prepared_track_target_t *out_target);
/* Global counterpart used after an AUDIO-owned bulk restore commit. */
uint8_t param_registry_publish_track_base_audio(param_id_t id,
                                                 uint8_t track,
                                                 float value);
uint8_t param_registry_is_lfo_param(param_id_t id);
/* Control-side projection: converts a canonical global value into the
 * complete command payload consumed by AUDIO. */
uint8_t param_registry_prepare_global_audio_command(param_id_t id,
                                                    float canonical_value,
                                                    uint8_t modfx_model,
                                                    float *out_command_value);
uint8_t param_registry_commit_global(param_id_t id, float value);
uint8_t param_registry_reset_global(param_id_t id);
#ifdef __cplusplus
}
#endif
