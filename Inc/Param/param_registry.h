#pragma once

#include "param_store.h"
#include "UI/ui_core.h"

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

typedef struct
{
    param_id_t id;
    uint8_t track;
    float value;
} param_registry_track_edit_cmd_t;

typedef void (*param_registry_track_structure_mutation_fn_t)(void *ctx);

typedef struct
{
    param_registry_track_structure_mutation_fn_t mutation_fn;
    void *mutation_ctx;
} param_registry_track_structure_transition_cmd_t;

typedef uint8_t (*param_registry_track_transition_stage_fn_t)(void *ctx);

typedef struct
{
    param_registry_track_transition_stage_fn_t prepare_fn;
    param_registry_track_transition_stage_fn_t mutate_fn;
    param_registry_track_transition_stage_fn_t reapply_fn;
    param_registry_track_transition_stage_fn_t seq_runtime_sync_fn;
    param_registry_track_transition_stage_fn_t ui_sync_fn;
    param_registry_track_transition_stage_fn_t resume_fn;
    void *ctx;
} param_registry_track_transition_pipeline_cmd_t;

void param_registry_init(void);

/* Query surface: pure reads only. */
uint8_t param_registry_get_track_value(param_id_t id, uint8_t track, float *out_value);
float param_get(param_id_t id);
uint8_t param_registry_is_audio_fx_param(param_id_t id);
param_id_t param_registry_get_audio_fx_param(uint8_t order);

/* Command / apply / transition / post-commit surface. */
void param_registry_sync_filter_ui_for_active_track(void);
void param_registry_batch_begin(void);
void param_registry_batch_end(void);
/* Internal transition pipeline: structural mutation + reapply/sync callbacks. */
uint8_t param_registry_run_track_transition_pipeline(const param_registry_track_transition_pipeline_cmd_t *cmd);
uint8_t param_registry_run_track_transition_pipeline_for_track(const param_registry_track_transition_pipeline_cmd_t *cmd,
                                                              uint8_t track);
void param_registry_apply_track_structure_transition(const param_registry_track_structure_transition_cmd_t *cmd);
uint8_t param_registry_track_structure_transition_is_active(void);
uint8_t param_registry_track_structure_transition_is_global_active(void);
uint8_t param_registry_track_structure_transition_is_track_active(uint8_t track);
uint8_t param_registry_apply_track_edit(const param_registry_track_edit_cmd_t *cmd);
uint8_t param_registry_apply_track_value(param_id_t id, uint8_t track, float value);
uint8_t param_registry_project_track_mute(uint8_t track, uint8_t effective_muted);
uint8_t param_registry_apply_track_value_runtime_temp(param_id_t id, uint8_t track, float value);
uint8_t param_registry_apply_track_value_runtime_temp_matrix(param_id_t id,
                                                              uint8_t track,
                                                              float value,
                                                              uint8_t matrix_operation);
uint8_t param_registry_apply_track_value_runtime_temp_audio(param_id_t id, uint8_t track, float value);
void param_registry_release_track_value_runtime_temp(param_id_t id, uint8_t track);
void param_registry_clear_track_runtime_state(uint8_t track);
/* Fast path reserved for RT modulation, not a general apply entry point. */
uint8_t param_registry_apply_track_value_rt_fast(param_id_t id, uint8_t track, float value);
/* Audio-owner track target path: updates the runtime backend and the
 * audio-authoritative track cache without UI/storage side effects. */
uint8_t param_registry_apply_track_value_audio(param_id_t id, uint8_t track, float value);
/* Audio-owner target path: applies a global backend without touching the UI
 * parameter store from the audio IRQ. */
uint8_t param_registry_apply_global_value_rt_fast(param_id_t id, float value);

void param_set(param_id_t id, float value);
void param_reset(param_id_t id);

#ifdef __cplusplus
}
#endif
