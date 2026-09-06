/**
 * @file ui_param.c
 * @brief Module applicatif ui_param.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à ui_param.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "ui_param.h"

#include <string.h>
#include <math.h>

#include "param_registry.h"
#include "ui_core.h"
#include "ui_track_catalog.h"
#include "buttons.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_division_catalog.h"
#include "Seq/seq_model.h"
#include "Keyboard/keyboard_runtime.h"
#include "Track/track_runtime.h"
#include "Track/entity_topology.h"
#include "Storage/project_control.h"
#include "Storage/audio_recorder.h"
#include "IPC/live_clock_control.h"
#include "App/live_parameter_audio_publication.h"
#include "Param/live_parameter_migration.h"
#include "IPC/live_parameter_event.h"
#include "Sampler/brick6_sampler_multi_contract.h"
#include "Param/engine_model_catalog.h"
#include "Track/synth_polyphony.h"
#include "UI/ui_core_feedback.h"
#include "UI/ui_service_wakeup.h"
#include "Track/track_state.h"
#include "encoders.h"
#include "NoteFx/note_fx_state.h"
#include "Track/tone_program_control.h"
#include "Param/param_global_control.h"
#include "Platform/memory_layout.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Mod/mod_matrix_control.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_global_pool.h"
#include "pages/ui_page_template_play.h"
#include "App/control_domain.h"
#include "stm32h7xx_hal.h"

#define UI_PARAM_VALUE_FLASH_DURATION_MS 800U
#define UI_PARAM_STEPPED_ENCODER_DIVIDER 4

typedef struct
{
    ui_param_bank_t bank;
    uint8_t valid;
} ui_param_state_t;

typedef struct
{
    param_id_t param;
    uint8_t track;
    float value;
    uint8_t kind;
    uint32_t until_ms;
    uint8_t active;
} ui_param_value_flash_slot_t;

static ui_param_state_t g_ui_param = {
    .bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
    .valid = 0U,
};
static ui_param_value_flash_slot_t g_ui_param_value_flash[4];
static param_id_t g_ui_param_tweak_param = PARAM_COUNT;
static uint8_t g_ui_param_tweak_slot = 0xFFU;
static uint32_t g_ui_param_tweak_until_ms;
static uint8_t g_ui_param_bank_track = 0xFFU;
static int16_t g_ui_param_stepped_encoder_accum[4];
static uint32_t g_ui_param_stepped_encoder_key[4];
static uint8_t g_ui_param_fm_operator;

uint8_t ui_param_is_local_control(param_id_t id)
{
    return (uint8_t)((id >= UI_PARAM_LOCAL_ASSET)
        && (id <= UI_PARAM_LOCAL_FM_OPERATOR));
}

static uint8_t ui_param_local_control_get(param_id_t id, uint8_t track,
                                          float *out_value)
{
    if ((out_value == NULL) || (track >= BRICK_ENTITY_CAPACITY)) return 0U;
    if ((id == UI_PARAM_LOCAL_ASSET) || (id == UI_PARAM_LOCAL_WAVE_OSC1)
            || (id == UI_PARAM_LOCAL_WAVE_OSC2))
    {
        const project_control_asset_role_t role = (id == UI_PARAM_LOCAL_ASSET)
            ? PROJECT_CONTROL_ASSET_SAMPLER
            : (id == UI_PARAM_LOCAL_WAVE_OSC1
                ? PROJECT_CONTROL_ASSET_WAVE_OSC1
                : PROJECT_CONTROL_ASSET_WAVE_OSC2);
        uint16_t logical = 0U;
        if (project_control_track_asset_get_logical(track, role, &logical) == 0U)
            return 0U;
        *out_value = (float)logical;
        return 1U;
    }
    if ((id == UI_PARAM_LOCAL_LOOPER_ARM)
            || (id == UI_PARAM_LOCAL_LOOPER_LENGTH)
            || (id == UI_PARAM_LOCAL_LOOPER_PLAY))
    {
        audio_recorder_looper_config_t config;
        if (audio_recorder_control_get_looper_config(track, &config) == 0U)
            return 0U;
        *out_value = (float)((id == UI_PARAM_LOCAL_LOOPER_ARM)
            ? config.arm_mode : ((id == UI_PARAM_LOCAL_LOOPER_LENGTH)
                ? config.length_mode : config.play_auto));
        return 1U;
    }
    if (id == UI_PARAM_LOCAL_FM_OPERATOR)
    {
        *out_value = (float)g_ui_param_fm_operator;
        return 1U;
    }
    return 0U;
}

static uint8_t ui_param_local_control_apply(param_id_t id, uint8_t track,
                                             int16_t delta)
{
    float current = 0.0f;
    (void)ui_param_local_control_get(id, track, &current);
    int32_t next = (int32_t)(current + 0.5f) + delta;
    if ((id == UI_PARAM_LOCAL_ASSET) || (id == UI_PARAM_LOCAL_WAVE_OSC1)
            || (id == UI_PARAM_LOCAL_WAVE_OSC2))
    {
        uint16_t list[SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS];
        uint16_t count = 0U;
        project_control_asset_role_t role = PROJECT_CONTROL_ASSET_SAMPLER;
        if (id == UI_PARAM_LOCAL_WAVE_OSC1 || id == UI_PARAM_LOCAL_WAVE_OSC2)
        {
            role = (id == UI_PARAM_LOCAL_WAVE_OSC1)
                ? PROJECT_CONTROL_ASSET_WAVE_OSC1 : PROJECT_CONTROL_ASSET_WAVE_OSC2;
            count = project_control_list_wavetables(list,
                SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS);
        }
        else if (ui_get_track_type(track) == TRACK_TYPE_MULTI)
            count = project_control_list_multis(list,
                SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS);
        else
            count = project_control_list_samples(
                (ui_get_track_type(track) == TRACK_TYPE_STREAM)
                    ? PERSIST_ASSET_SAMPLE_STREAM : PERSIST_ASSET_SAMPLE_RAM,
                list, SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS);
        if (count == 0U) return 0U;
        uint16_t pos = 0U;
        while ((pos < count) && (list[pos] != (uint16_t)(current + 0.5f))) ++pos;
        if (pos == count) pos = (delta > 0) ? 0U : (uint16_t)(count - 1U);
        else
        {
            int32_t moved = (int32_t)pos + delta;
            if (moved < 0) moved = 0;
            if (moved >= (int32_t)count) moved = (int32_t)count - 1;
            pos = (uint16_t)moved;
        }
        const control_asset_intent_t intent = {
            .operation = CONTROL_ASSET_SELECT_TRACK_LOGICAL,
            .track = track,
            .role = (uint8_t)role,
            .logical = list[pos]
        };
        return control_domain_request_asset(&intent);
    }
    if ((id == UI_PARAM_LOCAL_LOOPER_ARM)
            || (id == UI_PARAM_LOCAL_LOOPER_LENGTH)
            || (id == UI_PARAM_LOCAL_LOOPER_PLAY))
    {
        audio_recorder_looper_config_t config;
        if (audio_recorder_control_get_looper_config(track, &config) == 0U)
            return 0U;
        const int32_t maximum = (id == UI_PARAM_LOCAL_LOOPER_ARM) ? 2
            : ((id == UI_PARAM_LOCAL_LOOPER_LENGTH) ? 5 : 1);
        if (next < 0) next = 0;
        if (next > maximum) next = maximum;
        if (id == UI_PARAM_LOCAL_LOOPER_ARM) config.arm_mode = (uint8_t)next;
        else if (id == UI_PARAM_LOCAL_LOOPER_LENGTH) config.length_mode = (uint8_t)next;
        else config.play_auto = (uint8_t)next;
        const control_track_intent_t intent = {
            .operation = CONTROL_TRACK_SET_LOOPER_CONFIG,
            .track = track,
            .value0 = config.arm_mode,
            .value1 = config.length_mode,
            .value2 = config.play_auto
        };
        return control_domain_request_track(&intent);
    }
    if (id == UI_PARAM_LOCAL_FM_OPERATOR)
    {
        if (next < 0) next = 0;
        if (next >= (int32_t)PARAM_FM_OPERATOR_COUNT)
            next = (int32_t)PARAM_FM_OPERATOR_COUNT - 1;
        g_ui_param_fm_operator = (uint8_t)next;
        return 1U;
    }
    return 0U;
}

typedef struct
{
    seq_track_id_t track;
    seq_step_id_t step;
    uint8_t set_id;
    seq_param_slot_t param_slot;
} ui_param_live_rec_ctx_t;

static uint8_t ui_param_is_track_scoped(param_id_t param);
static uint8_t ui_param_track_accepts_relative_param(uint8_t track, param_id_t param);
static uint8_t ui_param_get_track_edit_value(param_id_t param, uint8_t track, float *out_value);
static float ui_param_get_active_track_value(param_id_t param, uint8_t active_track);
static uint8_t ui_param_resolve_seq_slot(uint8_t track,
                                         param_id_t param,
                                         uint8_t *out_set_id,
                                         seq_param_slot_t *out_param_slot);
static uint8_t ui_param_resolve_edit_bounds(param_id_t param, uint8_t track, float *out_min, float *out_max);
static uint8_t ui_param_resolve_effective_edit_track(param_id_t param, uint8_t active_track);
static uint8_t ui_param_live_rec_resolve_context(param_id_t param,
                                                 uint8_t active_track,
                                                 ui_param_live_rec_ctx_t *out_ctx);
static uint8_t ui_param_set_track_value(uint8_t encoder,
                                        param_id_t param,
                                        float value,
                                        uint8_t track);

static uint8_t ui_param_control_value_get(param_id_t param,
                                          uint8_t track,
                                          float *out_value)
{
    if ((out_value == 0) || (param >= PARAM_COUNT)
            || (live_parameter_is_audio_owned(param) == 0U))
    {
        return 0U;
    }

    const uint8_t scoped = ui_param_is_track_scoped(param);
    if (scoped == 0U)
        return param_registry_query_global(param, out_value);
    const uint8_t control_track = (scoped != 0U) ? track : 0U;
    if (control_track >= SEQ_LANE_CAPACITY)
    {
        return 0U;
    }

    return param_registry_get_track_value(param, control_track, out_value);
}

static uint8_t ui_param_step_value_find(seq_track_id_t track,
                                        seq_step_id_t step,
                                        param_id_t param,
                                        uint8_t set_id,
                                        seq_param_slot_t param_slot,
                                        seq_plock_entry_t *out_entry)
{
    return seq_edit_step_plock_find(track, step, set_id, param_slot, out_entry);
}

static seq_plock_op_status_t ui_param_step_value_upsert(seq_track_id_t track,
                                                         seq_step_id_t step,
                                                         param_id_t param,
                                                         uint8_t set_id,
                                                         seq_param_slot_t param_slot,
                                                         seq_value16_t value16,
                                                         uint8_t flags)
{
    seq_plock_entry_t existing;
    const uint8_t had_entry = ui_param_step_value_find(track, step, param,
                                                       set_id, param_slot,
                                                       &existing);
    const control_seq_intent_t intent = {
        .operation = CONTROL_SEQ_PLOCK_UPSERT,
        .track = (uint8_t)track,
        .step = (uint8_t)step,
        .set_id = set_id,
        .param_slot = param_slot,
        .flags = flags,
        .value16 = value16
    };
    if (control_domain_request_seq(&intent) == 0U)
        return SEQ_PLOCK_OP_INVALID;
    return (had_entry != 0U) ? SEQ_PLOCK_OP_UPDATED : SEQ_PLOCK_OP_CREATED;
}

static seq_plock_op_status_t ui_param_step_value_delete(seq_track_id_t track,
                                                         seq_step_id_t step,
                                                         param_id_t param,
                                                         uint8_t set_id,
                                                         seq_param_slot_t param_slot)
{
    seq_plock_entry_t existing;
    const uint8_t had_entry = ui_param_step_value_find(track, step, param,
                                                       set_id, param_slot,
                                                       &existing);
    const control_seq_intent_t intent = {
        .operation = CONTROL_SEQ_PLOCK_DELETE,
        .track = (uint8_t)track,
        .step = (uint8_t)step,
        .set_id = set_id,
        .param_slot = param_slot
    };
    if (control_domain_request_seq(&intent) == 0U)
        return SEQ_PLOCK_OP_INVALID;
    return (had_entry != 0U) ? SEQ_PLOCK_OP_DELETED : SEQ_PLOCK_OP_NOT_FOUND;
}

/* Parameter access, encoder interaction and sequencer feedback remain in their original sequence.
 * Private fragments share this translation unit to preserve UI state and call order. */

#include "Param/ui_param_binding_feedback.inc"

#include "Param/ui_param_seq_runtime.inc"

#include "Param/ui_param_bank_context.inc"

#include "Param/ui_param_value_access.inc"

#include "Param/ui_param_encoder_apply.inc"

#include "Param/ui_param_feedback_input.inc"
