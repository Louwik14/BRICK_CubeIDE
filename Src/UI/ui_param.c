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
#include "IPC/live_clock_control.h"
#include "IPC/live_parameter_audio_publication.h"
#include "Param/live_parameter_migration.h"
#include "IPC/live_parameter_event.h"
#include "Sampler/brick6_sampler_multi_contract.h"
#include "Param/engine_model_catalog.h"
#include "Track/synth_polyphony.h"
#include "UI/ui_core_feedback.h"
#include "Track/track_state.h"
#include "encoders.h"
#include "NoteFx/note_fx_state.h"
#include "param_store.h"
#include "Param/param_registry_runtime_state.h"
#include "Platform/memory_layout.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Mod/mod_matrix_control.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_global_pool.h"
#include "pages/ui_page_template_play.h"
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

typedef struct
{
    seq_track_id_t track;
    seq_step_id_t step;
    uint8_t set_id;
    seq_param_slot_t param_slot;
} ui_param_live_rec_ctx_t;

static uint8_t ui_param_is_track_scoped(param_id_t param);
static uint8_t ui_param_track_accepts_relative_param(uint8_t track, param_id_t param);
static uint8_t ui_param_is_seq_runtime_track_param(param_id_t param);
static uint8_t ui_param_get_track_edit_value(param_id_t param, uint8_t track, float *out_value);
static float ui_param_get_active_track_value(param_id_t param, uint8_t active_track);
static uint8_t ui_param_resolve_seq_slot(uint8_t track,
                                         param_id_t param,
                                         uint8_t *out_set_id,
                                         seq_param_slot_t *out_param_slot);
static uint8_t ui_param_resolve_edit_bounds(param_id_t param, uint8_t track, float *out_min, float *out_max);
static uint8_t ui_param_resolve_effective_edit_track(param_id_t param, uint8_t active_track);
static uint8_t ui_param_resolve_play_context(param_id_t param,
                                             uint8_t active_track,
                                             ui_page_template_play_context_t *out_context);
static uint8_t ui_param_live_rec_resolve_context(param_id_t param,
                                                 uint8_t active_track,
                                                 ui_param_live_rec_ctx_t *out_ctx);
static uint8_t ui_param_set_track_value(uint8_t encoder,
                                        param_id_t param,
                                        float value,
                                        uint8_t track,
                                        uint8_t update_active_mirror);

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
    const uint8_t control_track = (scoped != 0U) ? track : 0U;
    if (control_track >= SEQ_LANE_CAPACITY)
    {
        return 0U;
    }

    return param_registry_control_value_get(control_track, param, out_value);
}

static uint8_t ui_param_step_value_find(seq_track_id_t track,
                                        seq_step_id_t step,
                                        param_id_t param,
                                        uint8_t set_id,
                                        seq_param_slot_t param_slot,
                                        seq_plock_entry_t *out_entry)
{
    if (track_runtime_get_param_rule(param).domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
    {
        if (out_entry == NULL) return 0U;
        seq_value16_t value16 = 0U;
        if (seq_edit_step_play_find(track, step, param, &value16) == 0U) return 0U;
        memset(out_entry, 0, sizeof(*out_entry));
        out_entry->value16 = value16;
        return 1U;
    }
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
    if (track_runtime_get_param_rule(param).domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
    {
        return seq_edit_step_play_upsert(track, step, param, value16);
    }
    return seq_edit_step_plock_upsert(track, step, set_id, param_slot, value16, flags);
}

static void ui_param_step_value_commit(seq_track_id_t track,
                                       seq_step_id_t step,
                                       param_id_t param,
                                       uint8_t set_id,
                                       seq_param_slot_t param_slot)
{
    if (track_runtime_get_param_rule(param).domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
    {
        seq_edit_step_play_commit(track, step, param);
        return;
    }
    seq_edit_step_plock_commit(track, step, set_id, param_slot);
}

static seq_plock_op_status_t ui_param_step_value_delete(seq_track_id_t track,
                                                         seq_step_id_t step,
                                                         param_id_t param,
                                                         uint8_t set_id,
                                                         seq_param_slot_t param_slot)
{
    return (track_runtime_get_param_rule(param).domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
        ? seq_edit_step_play_delete(track, step, param)
        : seq_edit_step_plock_delete(track, step, set_id, param_slot);
}

static uint8_t ui_param_control_value_set(param_id_t param,
                                          uint8_t track,
                                          float value,
                                          uint8_t update_active_mirror)
{
    if ((param >= PARAM_COUNT)
            || (live_parameter_is_audio_owned(param) == 0U))
    {
        return 0U;
    }

    const uint8_t scoped = ui_param_is_track_scoped(param);
    const uint8_t control_track = (scoped != 0U) ? track : 0U;
    if (control_track >= SEQ_LANE_CAPACITY)
    {
        return 0U;
    }

    param_registry_control_value_set(control_track, param, value);
    if (update_active_mirror != 0U)
    {
        param_store_set_active(param, value);
    }
    return 1U;
}


/* Parameter access, encoder interaction and sequencer feedback remain in their original sequence.
 * Private fragments share this translation unit to preserve UI state and call order. */

#include "Param/ui_param_binding_feedback.inc"

#include "Param/ui_param_seq_runtime.inc"

#include "Param/ui_param_bank_context.inc"

#include "Param/ui_param_value_access.inc"

#include "Param/ui_param_encoder_apply.inc"

#include "Param/ui_param_feedback_input.inc"
