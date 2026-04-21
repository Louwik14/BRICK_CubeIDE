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

#include "param_registry.h"
#include "ui_core.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_runtime.h"
#include "Core/track_runtime.h"
#include "param_store.h"
#include "Mod/mod_lfo_v1.h"
#include "Storage/undo_v1.h"
typedef struct
{
    ui_param_bank_t bank;
    uint8_t valid;
} ui_param_state_t;

static ui_param_state_t g_ui_param = {
    .bank = { .params = { PARAM_GRAN_DENSITY, PARAM_GRAN_PITCH, PARAM_GRAN_MIX, PARAM_GRAN_FREEZE } },
    .valid = 0U,
};

static uint8_t ui_param_is_track_scoped(param_id_t param);

/**
 * @brief Point d'entrée ui_param_clamp.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_param_clamp.
 *
 * @param v Paramètre d'entrée de l'API.
 * @param min Paramètre d'entrée de l'API.
 * @param max Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float ui_param_clamp(float v, float min, float max)
{
    if (v < min)
    {
        return min;
    }

    if (v > max)
    {
        return max;
    }

    return v;
}

static int8_t ui_param_signum(int16_t value)
{
    if (value > 0)
    {
        return 1;
    }

    if (value < 0)
    {
        return -1;
    }

    return 0;
}

static uint8_t ui_param_value_is_same(float a, float b)
{
    const float diff = a - b;
    return ((diff > -0.000001f) && (diff < 0.000001f)) ? 1U : 0U;
}

static uint8_t ui_param_cfg_track_family_is_available(ui_track_family_t family)
{
    if ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
    {
        return 0U;
    }

    if (!ui_track_family_is_input(family))
    {
        if (family != UI_TRACK_FAMILY_MASTER)
        {
            return 1U;
        }
    }

    if (family == ui_get_track_family(ui_get_active_track()))
    {
        return 1U;
    }

    return (uint8_t)((ui_count_tracks_with_family(family) == 0U) ? 1U : 0U);
}

static float ui_param_step_cfg_track(float current_value, int8_t direction)
{
    int16_t candidate = (int16_t)(current_value + 0.5f);

    if (direction == 0)
    {
        return current_value;
    }

    while (1)
    {
        candidate = (int16_t)(candidate + direction);

        if ((candidate < 0) || (candidate >= (int16_t)UI_TRACK_FAMILY_COUNT))
        {
            return current_value;
        }

        if (ui_param_cfg_track_family_is_available((ui_track_family_t)candidate) != 0U)
        {
            return (float)candidate;
        }
    }
}

static float ui_param_step_cfg_track_type(float current_value, int8_t direction)
{
    const ui_track_family_t active_family = ui_get_track_family(ui_get_active_track());
    const uint8_t type_count = ui_get_track_type_count_for_family(active_family);
    int16_t candidate = (int16_t)(current_value + 0.5f);

    if ((direction == 0) || (type_count == 0U))
    {
        return current_value;
    }

    candidate = (int16_t)(candidate + direction);
    if (candidate < 0)
    {
        candidate = 0;
    }
    if (candidate >= (int16_t)type_count)
    {
        candidate = (int16_t)type_count - 1;
    }

    return (float)candidate;
}


/**
 * @brief Point d'entrée ui_param_set_bank.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_param_set_bank.
 *
 * @param bank Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_param_set_bank(const ui_param_bank_t *bank)
{
    if (bank == 0)
    {
        g_ui_param.valid = 0U;
        return;
    }

    g_ui_param.bank = *bank;
    g_ui_param.valid = 0U;

    for (uint8_t i = 0U; i < 4U; i++)
    {
        if (g_ui_param.bank.params[i] < PARAM_COUNT)
        {
            g_ui_param.valid = 1U;
            break;
        }
    }
}

void ui_param_invalidate_bank(void)
{
    g_ui_param.valid = 0U;
}

void ui_param_sync_active_bank_values(void)
{
    if (param_registry_track_structure_transition_is_active() != 0U)
    {
        return;
    }

    if (g_ui_param.valid == 0U)
    {
        return;
    }

    const uint8_t active_track = ui_get_active_track();
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        const param_id_t id = g_ui_param.bank.params[i];
        if (id >= PARAM_COUNT)
        {
            continue;
        }

        if (ui_param_is_track_scoped(id) != 0U)
        {
            float value = 0.0f;
            if (param_registry_get_track_value(id, active_track, &value) != 0U)
            {
                param_store_set_active(id, value);
            }
            continue;
        }

        param_store_set_active(id, param_get(id));
    }
}

static uint8_t ui_param_seq_resolve_ref_step(seq_track_id_t *out_track,
                                             seq_step_id_t *out_ref_step,
                                             uint8_t promote_pending)
{
    if ((out_track == 0) || (out_ref_step == 0) || (ui_hall_is_seq_context(ui_get_hall_mode()) == 0U))
    {
        return 0U;
    }

    seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
    seq_track_id_t held_track = 0U;
    const uint8_t held_count = seq_edit_collect_held_steps(&held_track,
                                                           held_steps,
                                                           (uint8_t)SEQ_STEPS_PER_PAGE,
                                                           promote_pending);
    if (held_count == 0U)
    {
        return 0U;
    }

    seq_step_id_t ref_step = held_steps[0];
    for (uint8_t i = 1U; i < held_count; ++i)
    {
        if (held_steps[i] < ref_step)
        {
            ref_step = held_steps[i];
        }
    }

    *out_track = held_track;
    *out_ref_step = ref_step;
    return 1U;
}

static uint8_t ui_param_is_track_scoped(param_id_t param)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    return ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            && (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

static uint32_t ui_param_make_gesture_key(uint8_t encoder, param_id_t param)
{
    const uint32_t hall_mode = (uint32_t)ui_get_hall_mode();
    const uint32_t active_track = (uint32_t)ui_get_active_track();
    return 0x10000000UL
        | (hall_mode << 28)
        | (active_track << 20)
        | ((uint32_t)param << 4)
        | (uint32_t)(encoder & 0x0FU);
}

static float ui_param_get_active_track_value(param_id_t param)
{
    if (ui_param_is_track_scoped(param) == 0U)
    {
        return param_get(param);
    }

    return param_store_get_active(param);
}

static uint8_t ui_param_set_active_track_value(param_id_t param, float value)
{
    if (ui_param_is_track_scoped(param) == 0U)
    {
        const param_desc_t *const desc = &param_registry[param];
        const float clamped = ui_param_clamp(value, desc->min, desc->max);
        if (ui_param_value_is_same(param_get(param), clamped) != 0U)
        {
            return 1U;
        }

        (void)undo_v1_capture_before_edit(0U);
        param_set(param, clamped);
        return 1U;
    }

    const param_desc_t *const desc = &param_registry[param];
    const float clamped = ui_param_clamp(value, desc->min, desc->max);
    const uint8_t active_track = ui_get_active_track();
    float current_value = 0.0f;

    if ((param_registry_get_track_value(param, active_track, &current_value) != 0U)
            && (ui_param_value_is_same(current_value, clamped) != 0U))
    {
        param_store_set_active(param, clamped);
        return 1U;
    }

    (void)undo_v1_capture_before_edit(0U);

    if (param_registry_apply_track_value(param, active_track, clamped) == 0U)
    {
        return 0U;
    }

    uint8_t set_id = 0U;
    seq_param8_t param8 = 0U;
    if (seq_param_iface_map_param(param, &set_id, &param8) != 0U)
    {
        const seq_value16_t encoded = seq_param_iface_encode_param_value(param, clamped);
        (void)seq_param_iface_set_base_value(active_track, set_id, param8, encoded);
    }

    /* Track-scoped contract: active[] mirrors the UI edit context; runtime authority is apply_track_value(track,...). */
    param_store_set_active(param, clamped);
    return 1U;
}

static uint8_t ui_param_try_apply_seq_plock(param_id_t param,
                                            const param_desc_t *desc,
                                            int16_t delta,
                                            float min_value,
                                            float max_value)
{
    if ((ui_hall_is_seq_context(ui_get_hall_mode()) == 0U) || (desc == 0))
    {
        return 0U;
    }

    uint8_t set_id = 0U;
    seq_param8_t param8 = 0U;
    if (seq_param_iface_map_param(param, &set_id, &param8) == 0U)
    {
        return 0U;
    }

    seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
    seq_track_id_t held_track = 0U;
    const uint8_t held_count = seq_edit_collect_held_steps(&held_track,
                                                           held_steps,
                                                           (uint8_t)SEQ_STEPS_PER_PAGE,
                                                           1U);
    if (held_count == 0U)
    {
        return 0U;
    }

    const float delta_value = (float)delta * desc->step;
    const float base_track_value = ui_param_get_active_track_value(param);
    seq_plock_entry_t prior_entries[SEQ_STEPS_PER_PAGE];
    seq_value16_t target_values[SEQ_STEPS_PER_PAGE];
    uint8_t had_prior_entry[SEQ_STEPS_PER_PAGE];

    for (uint8_t i = 0U; i < held_count; ++i)
    {
        const seq_step_id_t step = held_steps[i];

        float source_value = base_track_value;
        had_prior_entry[i] = seq_edit_step_plock_find(held_track, step, set_id, param8, &prior_entries[i]);
        if (had_prior_entry[i] != 0U)
        {
            source_value = seq_param_iface_decode_param_value(param, prior_entries[i].value16);
        }

        float next_value = source_value + delta_value;
        next_value = ui_param_clamp(next_value, min_value, max_value);
        target_values[i] = seq_param_iface_encode_param_value(param, next_value);
    }

    uint8_t applied_count = 0U;
    for (; applied_count < held_count; ++applied_count)
    {
        const seq_plock_op_status_t status = seq_edit_step_plock_upsert(held_track,
                                                                        held_steps[applied_count],
                                                                        set_id,
                                                                        param8,
                                                                        target_values[applied_count],
                                                                        0U);
        if ((status == SEQ_PLOCK_OP_CREATED) || (status == SEQ_PLOCK_OP_UPDATED))
        {
            continue;
        }

        while (applied_count > 0U)
        {
            applied_count--;
            if (had_prior_entry[applied_count] != 0U)
            {
                (void)seq_model_step_plock_upsert(held_track,
                                                  held_steps[applied_count],
                                                  set_id,
                                                  param8,
                                                  prior_entries[applied_count].value16,
                                                  prior_entries[applied_count].flags);
            }
            else
            {
                (void)seq_model_step_plock_delete(held_track,
                                                  held_steps[applied_count],
                                                  set_id,
                                                  param8);
            }
        }

        return 1U;
    }

    for (uint8_t i = 0U; i < held_count; ++i)
    {
        seq_edit_step_plock_commit(held_track, held_steps[i], set_id, param8);
    }

    return 1U;
}

static uint8_t ui_param_try_apply_live_rec_plock(param_id_t param,
                                                 const param_desc_t *desc,
                                                 int16_t delta,
                                                 float min_value,
                                                 float max_value)
{
    if ((desc == 0) || (ui_param_is_track_scoped(param) == 0U))
    {
        return 0U;
    }

    seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
    seq_track_id_t held_track = 0U;
    if (seq_edit_collect_held_steps(&held_track,
                                    held_steps,
                                    (uint8_t)SEQ_STEPS_PER_PAGE,
                                    1U) != 0U)
    {
        return 0U;
    }

    const seq_track_id_t track = ui_get_active_track();
    seq_step_id_t step = 0U;
    if ((track >= SEQ_TRACK_COUNT) || (seq_runtime_get_playhead_step(track, &step) == 0U))
    {
        return 0U;
    }

    uint8_t set_id = 0U;
    seq_param8_t param8 = 0U;
    if (seq_param_iface_map_param(param, &set_id, &param8) == 0U)
    {
        return 0U;
    }

    float source_value = ui_param_get_active_track_value(param);
    seq_plock_entry_t existing;
    if (seq_edit_step_plock_find(track, step, set_id, param8, &existing) != 0U)
    {
        source_value = seq_param_iface_decode_param_value(param, existing.value16);
    }

    float next_value = source_value + ((float)delta * desc->step);
    next_value = ui_param_clamp(next_value, min_value, max_value);
    const seq_value16_t encoded = seq_param_iface_encode_param_value(param, next_value);

    if (seq_runtime_live_rec_param_write(track, set_id, param8, encoded) == 0U)
    {
        return 0U;
    }

    param_store_set_active(param, next_value);
    return 1U;
}

uint8_t ui_param_try_get_seq_plock_feedback(param_id_t param, float *out_value, uint8_t *out_inverted)
{
    if (out_inverted != 0)
    {
        *out_inverted = 0U;
    }

    if (out_value == 0)
    {
        return 0U;
    }

    uint8_t set_id = 0U;
    seq_param8_t param8 = 0U;
    if (seq_param_iface_map_param(param, &set_id, &param8) == 0U)
    {
        return 0U;
    }

    seq_track_id_t ref_track = 0U;
    seq_step_id_t ref_step = 0U;
    if (ui_param_seq_resolve_ref_step(&ref_track, &ref_step, 0U) == 0U)
    {
        return 0U;
    }

    seq_plock_entry_t existing;
    if (seq_edit_step_plock_find(ref_track, ref_step, set_id, param8, &existing) == 0U)
    {
        return 0U;
    }

    *out_value = seq_param_iface_decode_param_value(param, existing.value16);
    if (out_inverted != 0)
    {
        *out_inverted = 1U;
    }

    return 1U;
}

/**
 * @brief Point d'entrée ui_param_handle_encoder.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_param_handle_encoder.
 *
 * @param encoder Paramètre d'entrée de l'API.
 * @param delta Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_param_handle_encoder(uint8_t encoder, int16_t delta)
{
    if ((g_ui_param.valid == 0U) || (delta == 0) || (encoder >= 4U))
    {
        return;
    }

    const param_id_t param = g_ui_param.bank.params[encoder];
    if (param >= PARAM_COUNT)
    {
        return;
    }

    undo_v1_begin_gesture(ui_param_make_gesture_key(encoder, param));

    const param_desc_t *desc = &param_registry[param];
    float min_value = desc->min;
    float max_value = desc->max;

    if (param == PARAM_CFG_TRACK)
    {
        max_value = (float)((uint8_t)UI_TRACK_FAMILY_COUNT - 1U);
    }
    else if (param == PARAM_CFG_TRACK_TYPE)
    {
        const ui_track_family_t active_family = ui_get_track_family(ui_get_active_track());
        const uint8_t type_count = ui_get_track_type_count_for_family(active_family);
        max_value = (type_count > 0U) ? (float)(type_count - 1U) : 0.0f;
    }
    else if ((param == PARAM_LFO1_DEST) || (param == PARAM_LFO2_DEST))
    {
        const uint16_t count = mod_lfo_v1_dest_count(ui_get_active_track());
        max_value = (count > 0U) ? (float)(count - 1U) : 0.0f;
    }

    if (ui_param_try_apply_seq_plock(param, desc, delta, min_value, max_value) != 0U)
    {
        return;
    }

    if (ui_param_try_apply_live_rec_plock(param, desc, delta, min_value, max_value) != 0U)
    {
        return;
    }

    float value = ui_param_get_active_track_value(param);

    if (param == PARAM_CFG_TRACK)
    {
        value = ui_param_step_cfg_track(value, ui_param_signum(delta));
        if (ui_param_value_is_same(value, ui_param_get_active_track_value(param)) != 0U)
        {
            return;
        }
        ui_param_set_active_track_value(param, value);
        return;
    }

    if (param == PARAM_CFG_TRACK_TYPE)
    {
        value = ui_param_step_cfg_track_type(value, ui_param_signum(delta));
        if (ui_param_value_is_same(value, ui_param_get_active_track_value(param)) != 0U)
        {
            return;
        }
        ui_param_set_active_track_value(param, value);
        return;
    }

    {
        const float current_value = value;
        value += (float)delta * desc->step;
        value = ui_param_clamp(value, min_value, max_value);

        if (ui_param_value_is_same(value, current_value) != 0U)
        {
            return;
        }
    }

    ui_param_set_active_track_value(param, value);
}
