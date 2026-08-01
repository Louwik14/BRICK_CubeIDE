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
#include "ui_track_catalog.h"
#include "Audio/fx_master_macro.h"
#include "buttons.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_model.h"
#include "Keyboard/keyboard_runtime.h"
#include "Core/track_runtime.h"
#include "Core/synth_polyphony.h"
#include "UI/ui_core_feedback.h"
#include "Core/track_state.h"
#include "NoteFx/note_fx_state.h"
#include "param_store.h"
#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_matrix.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_global_pool.h"
#include "Storage/undo_v2.h"
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
    .bank = { .params = { PARAM_GRAN_DENSITY, PARAM_GRAN_PITCH, PARAM_GRAN_MIX, PARAM_GRAN_FREEZE } },
    .valid = 0U,
};
static ui_param_value_flash_slot_t g_ui_param_value_flash[4];
static uint8_t g_ui_param_bank_track = 0xFFU;
static uint8_t g_ui_param_encoder_edit_group_active = 0U;
static uint32_t g_ui_param_encoder_edit_group_key = 0U;
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
static uint8_t ui_param_resolve_seq_slot(uint8_t track,
                                         param_id_t param,
                                         uint8_t *out_set_id,
                                         seq_param_slot_t *out_param_slot);
static uint32_t ui_param_make_encoder_group_gesture_key(const ui_param_encoder_context_t *ctx);
static uint32_t ui_param_get_active_undo_gesture_key(uint8_t encoder, param_id_t param, uint8_t active_track);
static void ui_param_ensure_undo_transaction(uint8_t encoder, param_id_t param, uint8_t active_track);
static uint8_t ui_param_resolve_edit_bounds(param_id_t param, uint8_t track, float *out_min, float *out_max);
static uint8_t ui_param_resolve_effective_edit_track(param_id_t param, uint8_t active_track);
static uint8_t ui_param_resolve_play_context(param_id_t param,
                                             uint8_t active_track,
                                             ui_page_template_play_context_t *out_context);
static uint8_t ui_param_macro_fx_quantize_edit(uint8_t track,
                                                param_id_t param,
                                                float current_value,
                                                int16_t delta,
                                                float *out_value);
static uint8_t ui_param_macro_fx_step_type(uint8_t track,
                                            param_id_t param,
                                            float current_value,
                                            int16_t delta,
                                            float *out_value);
static uint8_t ui_param_set_track_value(uint8_t encoder,
                                        param_id_t param,
                                        float value,
                                        uint8_t track,
                                        uint8_t update_active_mirror);

static uint8_t ui_param_is_stack_osc_tune(param_id_t param)
{
    return ((param == PARAM_STACK_OSC1_TUNE)
            || (param == PARAM_STACK_OSC2_TUNE)
            || (param == PARAM_STACK_OSC3_TUNE)) ? 1U : 0U;
}

static uint8_t ui_param_is_prism_tune(param_id_t param, uint8_t track)
{
    return (uint8_t)(((param == PARAM_PRISM_COARSE) || (param == PARAM_PRISM_OSC2_COARSE))
            && (track < UI_TRACK_COUNT)
            && (ui_get_track_family(track) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(track) == UI_TRACK_TYPE_PRISM));
}

static param_id_t ui_param_prism_fine_for_tune(param_id_t param)
{
    return (param == PARAM_PRISM_OSC2_COARSE) ? PARAM_PRISM_OSC2_FINE : PARAM_PRISM_FINE;
}

static float ui_param_prism_tune_normalized_from_parts(float coarse, float fine)
{
    float semitones = ((coarse - 0.5f) * 48.0f) + ((fine - 0.5f) * 2.0f);
    if (semitones < -24.0f)
    {
        semitones = -24.0f;
    }
    else if (semitones > 24.0f)
    {
        semitones = 24.0f;
    }

    float normalized = (semitones / 48.0f) + 0.5f;
    if (normalized < 0.0f)
    {
        normalized = 0.0f;
    }
    else if (normalized > 1.0f)
    {
        normalized = 1.0f;
    }
    return normalized;
}

static uint8_t ui_param_bank_is_same(const ui_param_bank_t *bank)
{
    if ((bank == 0) || (g_ui_param.valid == 0U))
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < 4U; ++i)
    {
        if (g_ui_param.bank.params[i] != bank->params[i])
        {
            return 0U;
        }
    }

    return 1U;
}

void ui_param_clear_value_flash(void)
{
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        g_ui_param_value_flash[i].active = 0U;
        g_ui_param_value_flash[i].param = PARAM_COUNT;
        g_ui_param_value_flash[i].track = 0U;
        g_ui_param_value_flash[i].value = 0.0f;
        g_ui_param_value_flash[i].kind = (uint8_t)UI_PARAM_VALUE_FLASH_DIRECT;
        g_ui_param_value_flash[i].until_ms = 0U;
    }
}

void ui_param_note_user_value_flash(uint8_t slot,
                                    param_id_t param,
                                    uint8_t track,
                                    float value,
                                    ui_param_value_flash_kind_t kind)
{
    if ((slot >= 4U) || (param >= PARAM_COUNT) || (track >= UI_TRACK_COUNT))
    {
        return;
    }

    g_ui_param_value_flash[slot].param = param;
    g_ui_param_value_flash[slot].track = track;
    g_ui_param_value_flash[slot].value = value;
    g_ui_param_value_flash[slot].kind = (uint8_t)kind;
    g_ui_param_value_flash[slot].until_ms = HAL_GetTick() + UI_PARAM_VALUE_FLASH_DURATION_MS;
    g_ui_param_value_flash[slot].active = 1U;
}

uint8_t ui_param_get_slot_value_flash(uint8_t slot,
                                      param_id_t param,
                                      uint8_t track,
                                      float *out_value,
                                      ui_param_value_flash_kind_t *out_kind)
{
    if ((slot >= 4U) || (param >= PARAM_COUNT) || (out_value == 0))
    {
        return 0U;
    }

    ui_param_value_flash_slot_t *const flash = &g_ui_param_value_flash[slot];
    if ((flash->active == 0U)
            || (flash->param != param)
            || (flash->track != track)
            || ((int32_t)(flash->until_ms - HAL_GetTick()) <= 0))
    {
        flash->active = 0U;
        return 0U;
    }

    *out_value = flash->value;
    if (out_kind != 0)
    {
        *out_kind = (ui_param_value_flash_kind_t)flash->kind;
    }
    return 1U;
}

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

static void ui_param_reset_stepped_encoder_accum(void)
{
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        g_ui_param_stepped_encoder_accum[i] = 0;
        g_ui_param_stepped_encoder_key[i] = 0UL;
    }
}

static uint8_t ui_param_seq_div_ui_to_runtime(float value)
{
    const uint8_t index = (uint8_t)(ui_param_clamp(value, 0.0f, 3.0f) + 0.5f);
    switch (index)
    {
        case 1U:
            return 2U;
        case 2U:
            return 4U;
        case 3U:
            return 8U;
        case 0U:
        default:
            return 1U;
    }
}

static float ui_param_seq_div_runtime_to_ui(uint8_t div)
{
    switch (div)
    {
        case 2U:
            return 1.0f;
        case 4U:
            return 2.0f;
        case 8U:
            return 3.0f;
        case 1U:
        default:
            return 0.0f;
    }
}

static uint8_t ui_param_is_seq_runtime_track_param(param_id_t param)
{
    switch (param)
    {
        case PARAM_SEQ_LENGTH:
        case PARAM_SEQ_DIV:
        case PARAM_SEQ_QUANT:
        case PARAM_SEQ_SWING:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t ui_param_seq_runtime_track_is_valid(uint8_t track)
{
    return (uint8_t)((track < SEQ_TRACK_COUNT)
            && (track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_MASTER) == 0U)
            && (track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_FX) == 0U));
}

static uint8_t ui_param_get_seq_runtime_track_value(param_id_t param, uint8_t track, float *out_value)
{
    if ((out_value == 0) || (ui_param_seq_runtime_track_is_valid(track) == 0U))
    {
        return 0U;
    }

    uint8_t value = 0U;
    switch (param)
    {
        case PARAM_SEQ_DIV:
            if (seq_runtime_get_track_div(track, &value) == 0U)
            {
                return 0U;
            }
            *out_value = ui_param_seq_div_runtime_to_ui(value);
            return 1U;

        case PARAM_SEQ_LENGTH:
            *out_value = (float)seq_model_get_track_length(track);
            return 1U;

        case PARAM_SEQ_QUANT:
            if (seq_runtime_get_track_quant(track, &value) == 0U)
            {
                return 0U;
            }
            *out_value = (float)value;
            return 1U;

        case PARAM_SEQ_SWING:
            if (seq_runtime_get_track_swing(track, &value) == 0U)
            {
                return 0U;
            }
            *out_value = (float)value;
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t ui_param_apply_seq_runtime_track_value(param_id_t param, uint8_t track, float value)
{
    if ((ui_param_seq_runtime_track_is_valid(track) == 0U)
            || (seq_edit_track_sequence_is_locked((seq_track_id_t)track) != 0U))
    {
        return 0U;
    }

    switch (param)
    {
        case PARAM_SEQ_DIV:
            seq_runtime_set_track_div(track, ui_param_seq_div_ui_to_runtime(value));
            return 1U;

        case PARAM_SEQ_LENGTH:
            seq_model_set_track_length(track, (uint8_t)(ui_param_clamp(value, 1.0f, (float)SEQ_MAX_STEPS) + 0.5f));
            seq_runtime_on_track_length_changed(track);
            return 1U;

        case PARAM_SEQ_QUANT:
            seq_runtime_set_track_quant(track, (uint8_t)(ui_param_clamp(value, 0.0f, 100.0f) + 0.5f));
            return 1U;

        case PARAM_SEQ_SWING:
            seq_runtime_set_track_swing(track, (uint8_t)(ui_param_clamp(value, 0.0f, 100.0f) + 0.5f));
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t ui_param_relative_multi_track_is_record_context_blocked(void)
{
    if (seq_runtime_rec_is_armed() != 0U)
    {
        return 1U;
    }

    if (ui_hall_is_seq_context(ui_get_hall_mode()) != 0U)
    {
        seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
        seq_track_id_t held_track = 0U;
        if (seq_edit_collect_held_steps(&held_track, held_steps, (uint8_t)SEQ_STEPS_PER_PAGE, 1U) != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t ui_param_cfg_track_family_is_available(ui_track_family_t family, uint8_t active_track)
{
    return (uint8_t)(ui_track_catalog_family_has_available_type(active_track,
                                                                family,
                                                                track_state_get_configs()) ? 1U : 0U);
}

static float ui_param_step_cfg_track(float current_value, int8_t direction, uint8_t active_track)
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

        if (ui_param_cfg_track_family_is_available((ui_track_family_t)candidate, active_track) != 0U)
        {
            return (float)candidate;
        }
    }
}

static float ui_param_step_cfg_track_type(float current_value,
                                          int8_t direction,
                                          uint8_t active_track)
{
    const ui_track_family_t active_family = ui_get_track_family(active_track);
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

static uint8_t ui_param_macro_fx_type_slot_from_param(param_id_t param, uint8_t *out_slot)
{
    if ((param != PARAM_MACRO_FX1_TYPE)
            && (param != PARAM_MACRO_FX2_TYPE)
            && (param != PARAM_MACRO_FX3_TYPE)
            && (param != PARAM_MACRO_FX4_TYPE))
    {
        return 0U;
    }
    if (out_slot != 0)
    {
        *out_slot = (uint8_t)((param - PARAM_MACRO_FX1_TYPE) / 4U);
    }
    return 1U;
}

static uint8_t ui_param_macro_fx_type_is_used_by_other_slot(uint8_t track, uint8_t slot, uint8_t type)
{
    if ((type != (uint8_t)FX_MASTER_MACRO_STUTTER)
            && (type != (uint8_t)FX_MASTER_MACRO_FREEZE))
    {
        return 0U;
    }

    for (uint8_t other = 0U; other < 4U; ++other)
    {
        float value = 0.0f;
        const param_id_t other_param = (param_id_t)(PARAM_MACRO_FX1_TYPE + (other * 4U));
        if ((other != slot)
                && (param_registry_get_track_value(other_param, track, &value) != 0U)
                && ((uint8_t)(value + 0.5f) == type))
        {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t ui_param_macro_fx_step_type(uint8_t track,
                                            param_id_t param,
                                            float current_value,
                                            int16_t delta,
                                            float *out_value)
{
    uint8_t slot = 0U;
    if ((out_value == 0)
            || (delta == 0)
            || (track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_FX) == 0U)
            || (track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_FX) == 0U)
            || (ui_param_macro_fx_type_slot_from_param(param, &slot) == 0U))
    {
        return 0U;
    }

    const int8_t direction = ui_param_signum(delta);
    int16_t candidate = (int16_t)(current_value + 0.5f);
    for (uint8_t step = 0U; step < (uint8_t)FX_MASTER_MACRO_TYPE_COUNT; ++step)
    {
        candidate = (int16_t)(candidate + direction);
        if (candidate < 0)
        {
            candidate = 0;
        }
        if (candidate >= (int16_t)FX_MASTER_MACRO_TYPE_COUNT)
        {
            candidate = (int16_t)FX_MASTER_MACRO_TYPE_COUNT - 1;
        }

        if (ui_param_macro_fx_type_is_used_by_other_slot(track, slot, (uint8_t)candidate) == 0U)
        {
            *out_value = (float)candidate;
            return 1U;
        }

        if ((candidate == 0) || (candidate == ((int16_t)FX_MASTER_MACRO_TYPE_COUNT - 1)))
        {
            break;
        }
    }

    *out_value = current_value;
    return 1U;
}

static uint8_t ui_param_macro_fx_slot_from_param(param_id_t param, uint8_t *out_slot, uint8_t *out_macro)
{
    if ((param < PARAM_MACRO_FX1_TYPE) || (param > PARAM_MACRO_FX4_B))
    {
        return 0U;
    }

    const uint8_t offset = (uint8_t)(param - PARAM_MACRO_FX1_TYPE);
    const uint8_t macro = (uint8_t)(offset % 4U);
    if (macro == 0U)
    {
        return 0U;
    }

    if (out_slot != 0)
    {
        *out_slot = (uint8_t)(offset / 4U);
    }
    if (out_macro != 0)
    {
        *out_macro = macro;
    }
    return 1U;
}

static uint8_t ui_param_macro_fx_discrete_count(uint8_t fx_type, uint8_t macro)
{
    if (macro == 1U)
    {
        return (fx_type == (uint8_t)FX_MASTER_MACRO_STUTTER) ? 2U : 0U;
    }

    if (macro == 2U)
    {
        switch (fx_type)
        {
            case FX_MASTER_MACRO_CRUSH:
                return 13U;
            case FX_MASTER_MACRO_PUMP:
            case FX_MASTER_MACRO_CHOP:
                return 10U;
            case FX_MASTER_MACRO_FREEZE:
            case FX_MASTER_MACRO_STUTTER:
                return 8U;
            default:
                return 0U;
        }
    }

    if (macro == 3U)
    {
        switch (fx_type)
        {
            case FX_MASTER_MACRO_FREEZE:
            case FX_MASTER_MACRO_RING:
                return 4U;
            case FX_MASTER_MACRO_CHOP:
                return 3U;
            case FX_MASTER_MACRO_STUTTER:
                return 8U;
            default:
                return 0U;
        }
    }

    return 0U;
}

static uint8_t ui_param_macro_fx_raw_to_step(float value, uint8_t count)
{
    const float clamped = ui_param_clamp(value, 0.0f, 127.0f);
    const uint32_t raw = (uint32_t)(clamped + 0.5f);
    return (uint8_t)((raw * (uint32_t)(count - 1U) + 63U) / 127U);
}

static float ui_param_macro_fx_step_to_raw(uint8_t step, uint8_t count)
{
    if (count <= 1U)
    {
        return 0.0f;
    }

    if (step >= count)
    {
        step = (uint8_t)(count - 1U);
    }

    return (float)(((uint32_t)step * 127U + ((uint32_t)(count - 1U) / 2U)) / (uint32_t)(count - 1U));
}

static uint8_t ui_param_macro_fx_quantize_edit(uint8_t track,
                                                param_id_t param,
                                                float current_value,
                                                int16_t delta,
                                                float *out_value)
{
    uint8_t slot = 0U;
    uint8_t macro = 0U;
    if ((out_value == 0)
            || (delta == 0)
            || (track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_FX) == 0U)
            || (track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_FX) == 0U)
            || (ui_param_macro_fx_slot_from_param(param, &slot, &macro) == 0U))
    {
        return 0U;
    }

    float fx_type_value = 0.0f;
    const param_id_t type_param = (param_id_t)(PARAM_MACRO_FX1_TYPE + (slot * 4U));
    if (param_registry_get_track_value(type_param, track, &fx_type_value) == 0U)
    {
        return 0U;
    }

    const uint8_t count = ui_param_macro_fx_discrete_count((uint8_t)(fx_type_value + 0.5f), macro);
    if (count < 2U)
    {
        return 0U;
    }

    int16_t step = (int16_t)ui_param_macro_fx_raw_to_step(current_value, count);
    step = (int16_t)(step + delta);
    if (step < 0)
    {
        step = 0;
    }
    if (step >= (int16_t)count)
    {
        step = (int16_t)count - 1;
    }

    *out_value = ui_param_macro_fx_step_to_raw((uint8_t)step, count);
    return 1U;
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
        g_ui_param_bank_track = 0xFFU;
        ui_param_clear_value_flash();
        ui_param_reset_stepped_encoder_accum();
        return;
    }

    const uint8_t same_bank = ui_param_bank_is_same(bank);
    const uint8_t active_track = ui_get_active_track();
    const uint8_t same_track = (g_ui_param_bank_track == active_track) ? 1U : 0U;
    g_ui_param.bank = *bank;
    g_ui_param_bank_track = active_track;
    g_ui_param.valid = 0U;

    for (uint8_t i = 0U; i < 4U; i++)
    {
        if (g_ui_param.bank.params[i] < PARAM_COUNT)
        {
            g_ui_param.valid = 1U;
            break;
        }
    }

    if ((same_bank == 0U) || (same_track == 0U))
    {
        ui_param_clear_value_flash();
        ui_param_reset_stepped_encoder_accum();
    }
}

void ui_param_invalidate_bank(void)
{
    g_ui_param.valid = 0U;
    g_ui_param_bank_track = 0xFFU;
    ui_param_clear_value_flash();
    ui_param_reset_stepped_encoder_accum();
}

void ui_param_sync_active_bank_values(void)
{
    const uint8_t active_track = ui_get_active_track();
    if ((param_registry_track_structure_transition_is_global_active() != 0U)
            || (param_registry_track_structure_transition_is_track_active(active_track) != 0U))
    {
        return;
    }

    if (g_ui_param.valid == 0U)
    {
        return;
    }

    for (uint8_t i = 0U; i < 4U; ++i)
    {
        const param_id_t id = g_ui_param.bank.params[i];
        if (id >= PARAM_COUNT)
        {
            continue;
        }

        if (ui_param_is_track_scoped(id) != 0U)
        {
            /* Query seam: sync the UI mirror from the track-aware value surface. */
            float value = 0.0f;
            if (ui_param_get_track_edit_value(id, active_track, &value) != 0U)
            {
                param_store_set_active(id, value);
            }
            continue;
        }

        param_store_set_active(id, param_get(id));
    }
}

void ui_param_sync_active_track_mirror_from_runtime(void)
{
    const uint8_t active_track = ui_get_active_track();
    if ((param_registry_track_structure_transition_is_global_active() != 0U)
            || (param_registry_track_structure_transition_is_track_active(active_track) != 0U))
    {
        return;
    }

    const float seq_length = (float)seq_model_get_track_length(active_track);
    uint8_t track_div = 1U;
    uint8_t track_quant = 0U;
    uint8_t track_swing = 0U;

    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        const param_id_t id = (param_id_t)raw_id;
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE)
                || (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
        {
            continue;
        }

        if ((id >= PARAM_FILTER_TYPE) && (id <= PARAM_FILTER_DECIMATOR_RATE2))
        {
            continue;
        }

        /* Query seam: read track-aware value without mutating the runtime. */
        float value = 0.0f;
        if (param_registry_get_track_value(id, active_track, &value) != 0U)
        {
            param_store_set_active(id, value);
        }
    }

    param_store_set_active(PARAM_SEQ_LENGTH, seq_length);
    if (seq_runtime_get_track_div(active_track, &track_div) != 0U)
    {
        param_store_set_active(PARAM_SEQ_DIV, (track_div == 1U) ? 0.0f
                                                                 : (track_div == 2U) ? 1.0f
                                                                 : (track_div == 4U) ? 2.0f
                                                                 : (track_div == 8U) ? 3.0f
                                                                 : 0.0f);
    }
    if (seq_runtime_get_track_quant(active_track, &track_quant) != 0U)
    {
        param_store_set_active(PARAM_SEQ_QUANT, (float)track_quant);
    }
    if (seq_runtime_get_track_swing(active_track, &track_swing) != 0U)
    {
        param_store_set_active(PARAM_SEQ_SWING, (float)track_swing);
    }
    param_registry_sync_filter_ui_for_active_track();
}

void ui_param_capture_encoder_context(ui_param_encoder_context_t *out_ctx)
{
    if (out_ctx == 0)
    {
        return;
    }

    out_ctx->bank = g_ui_param.bank;
    out_ctx->valid = g_ui_param.valid;
    out_ctx->active_track = ui_get_active_track();
    out_ctx->shift_down = (uint8_t)(button_down(BTN_SHIFT) != 0U);
}

void ui_param_begin_encoder_edit_group(const ui_param_encoder_context_t *ctx)
{
    g_ui_param_encoder_edit_group_key = ui_param_make_encoder_group_gesture_key(ctx);
    g_ui_param_encoder_edit_group_active = 1U;
}

void ui_param_end_encoder_edit_group(void)
{
    if (undo_v2_is_transaction_open() != 0U)
    {
        (void)undo_v2_commit_transaction();
    }
    g_ui_param_encoder_edit_group_active = 0U;
}

uint8_t ui_param_get_active_bank_param(uint8_t encoder, param_id_t *out_param)
{
    if ((out_param == 0) || (g_ui_param.valid == 0U) || (encoder >= 4U))
    {
        return 0U;
    }

    *out_param = g_ui_param.bank.params[encoder];
    return (*out_param < PARAM_COUNT) ? 1U : 0U;
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

void ui_param_seq_plock_feedback_frame_begin(ui_param_seq_plock_feedback_frame_t *frame_ctx)
{
    if (frame_ctx == 0)
    {
        return;
    }

    frame_ctx->seq_context_active = 0U;
    frame_ctx->has_ref_step = 0U;
    frame_ctx->ref_track = 0U;
    frame_ctx->ref_step = 0U;

    if (ui_hall_is_seq_context(ui_get_hall_mode()) == 0U)
    {
        return;
    }

    frame_ctx->seq_context_active = 1U;

    seq_track_id_t ref_track = 0U;
    seq_step_id_t ref_step = 0U;
    if (ui_param_seq_resolve_ref_step(&ref_track, &ref_step, 0U) == 0U)
    {
        return;
    }

    frame_ctx->has_ref_step = 1U;
    frame_ctx->ref_track = ref_track;
    frame_ctx->ref_step = ref_step;
}

static uint8_t ui_param_is_track_scoped(param_id_t param)
{
    if (ui_param_is_seq_runtime_track_param(param) != 0U)
    {
        return 1U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    return ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            && (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

static uint8_t ui_param_track_accepts_relative_param(uint8_t track, param_id_t param)
{
    if ((track >= SEQ_TRACK_COUNT) || (param >= PARAM_COUNT)
            || (track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_MASTER) != 0U)
            || (track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_FX) != 0U))
    {
        return 0U;
    }
    if (ui_param_is_seq_runtime_track_param(param) != 0U)
    {
        return ui_param_seq_runtime_track_is_valid(track);
    }
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    if ((rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE))
    {
        return 0U;
    }

    track_runtime_refresh_track(track);
    if (track_runtime_get_effective_param_status(track, param) != TRACK_RUNTIME_PARAM_ALLOWED)
    {
        return 0U;
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV))
    {
        uint8_t set_id = 0U;
        seq_param_slot_t param_slot = 0U;
        return ui_param_resolve_seq_slot(track, param, &set_id, &param_slot);
    }

    return 1U;
}

static uint32_t ui_param_make_gesture_key(uint8_t encoder, param_id_t param, uint8_t active_track)
{
    const uint32_t hall_mode = (uint32_t)ui_get_hall_mode();
    return 0x10000000UL
        | (hall_mode << 28)
        | ((uint32_t)active_track << 20)
        | ((uint32_t)param << 4)
        | (uint32_t)(encoder & 0x0FU);
}

static uint32_t ui_param_make_encoder_group_gesture_key(const ui_param_encoder_context_t *ctx)
{
    uint32_t key = 0x20000000UL | (((uint32_t)ui_get_hall_mode() & 0x0FU) << 24);

    if ((ctx == 0) || (ctx->valid == 0U))
    {
        return key;
    }

    key ^= ((uint32_t)ctx->active_track & 0x0FU) << 20;
    for (uint8_t encoder = 0U; encoder < 4U; ++encoder)
    {
        key ^= ((uint32_t)ctx->bank.params[encoder] & 0x03FFUL) << (encoder * 5U);
    }

    return key;
}

static uint32_t ui_param_get_active_undo_gesture_key(uint8_t encoder, param_id_t param, uint8_t active_track)
{
    if (g_ui_param_encoder_edit_group_active != 0U)
    {
        return g_ui_param_encoder_edit_group_key;
    }

    return ui_param_make_gesture_key(encoder, param, active_track);
}

static void ui_param_ensure_undo_transaction(uint8_t encoder, param_id_t param, uint8_t active_track)
{
    if ((undo_v2_is_transaction_open() != 0U) || (undo_v2_param_is_undoable(param) == 0U))
    {
        return;
    }

    (void)undo_v2_begin_transaction(UNDO_V2_TX_KIND_PARAM,
                                    UNDO_V2_SOURCE_ENCODER,
                                    ui_param_get_active_undo_gesture_key(encoder, param, active_track),
                                    UNDO_V2_TX_MODE_DELTA);
}

static uint8_t ui_param_begin_structural_undo(uint8_t encoder, param_id_t param, uint8_t track)
{
    if ((param != PARAM_CFG_TRACK) && (param != PARAM_CFG_TRACK_TYPE)
            && (param != PARAM_CFG_POLY_VOICES)
            && (param != PARAM_CFG_POLY_SPREAD)) return 0U;
    if (undo_v2_begin_snapshot_transaction(UNDO_V2_SOURCE_ENCODER,
            ui_param_make_gesture_key(encoder, param, track)) != UNDO_V2_STATUS_OK) return 0U;
    if (undo_v2_capture_snapshot_before() != UNDO_V2_STATUS_OK)
    { undo_v2_cancel_transaction(); return 0U; }
    return 1U;
}

static void ui_param_finish_structural_undo(uint8_t started, uint8_t applied)
{
    if (started == 0U) return;
    if ((applied == 0U) || (undo_v2_capture_snapshot_after() != UNDO_V2_STATUS_OK))
    { undo_v2_cancel_transaction(); return; }
    (void)undo_v2_commit_transaction();
}

static uint8_t ui_param_resolve_edit_bounds(param_id_t param, uint8_t track, float *out_min, float *out_max)
{
    if ((param >= PARAM_COUNT) || (out_min == 0) || (out_max == 0))
    {
        return 0U;
    }

    const param_desc_t *desc = &param_registry[param];
    *out_min = desc->min;
    *out_max = desc->max;

    if (param == PARAM_CFG_TRACK)
    {
        *out_max = (float)((uint8_t)UI_TRACK_FAMILY_COUNT - 1U);
    }
    else if (param == PARAM_CFG_TRACK_TYPE)
    {
        const ui_track_family_t active_family = ui_get_track_family(track);
        const uint8_t type_count = ui_get_track_type_count_for_family(active_family);
        *out_max = (type_count > 0U) ? (float)(type_count - 1U) : 0.0f;
    }
    else if (param == PARAM_CFG_POLY_VOICES)
    {
        *out_max = (float)synth_polyphony_get_available_for_track(track);
    }
    else if (param == PARAM_MOD_MATRIX_DEST)
    {
        const uint16_t count = mod_lfo_v1_dest_count(track);
        *out_max = (count > 0U) ? (float)(count - 1U) : 0.0f;
    }
    else if ((param == PARAM_SAMPLER_SAMPLE)
             && (ui_get_track_family(track) == UI_TRACK_FAMILY_SAMPLER)
             && (ui_get_track_type(track) == UI_TRACK_TYPE_MULTI))
    {
        *out_min = 0.0f;
        *out_max = (float)multi_sample_pool_get_instrument_count();
    }
    else if ((param == PARAM_SAMPLER_SAMPLE)
             && (ui_get_track_family(track) == UI_TRACK_FAMILY_SAMPLER))
    {
        const uint16_t active_slots = sample_global_pool_get_active_slot_capacity();
        *out_min = 0.0f;
        *out_max = (active_slots > 0U) ? (float)(active_slots - 1U) : 0.0f;
    }

    return 1U;
}

static uint8_t ui_param_resolve_effective_edit_track(param_id_t param, uint8_t active_track)
{
    ui_page_template_play_context_t play_context;
    if (ui_param_resolve_play_context(param, active_track, &play_context) != 0U)
    {
        return play_context.target_track;
    }
    return active_track;
}

static uint8_t ui_param_resolve_play_context(param_id_t param,
                                             uint8_t active_track,
                                             ui_page_template_play_context_t *out_context)
{
    return ui_page_template_play_resolve_context(param, active_track, out_context);
}

static float ui_param_get_active_track_value(param_id_t param, uint8_t active_track)
{
    active_track = ui_param_resolve_effective_edit_track(param, active_track);

    float value = 0.0f;
    if (ui_param_get_seq_runtime_track_value(param, active_track, &value) != 0U)
    {
        return value;
    }
    if (ui_param_is_track_scoped(param) == 0U)
    {
        return param_get(param);
    }

    if (ui_param_get_track_edit_value(param, active_track, &value) != 0U)
    {
        return value;
    }

    return param_store_get_active(param);
}

float ui_param_get_active_track_display_value(param_id_t param, uint8_t active_track)
{
    return ui_param_get_active_track_value(param, active_track);
}

static uint8_t ui_param_get_track_edit_value(param_id_t param, uint8_t track, float *out_value)
{
    if (ui_param_get_seq_runtime_track_value(param, track, out_value) != 0U)
    {
        return 1U;
    }
    if (ui_param_is_prism_tune(param, track) != 0U)
    {
        float coarse = 0.5f;
        float fine = 0.5f;
        if ((param_registry_get_track_value(param, track, &coarse) == 0U)
                || (param_registry_get_track_value(ui_param_prism_fine_for_tune(param), track, &fine) == 0U))
        {
            return 0U;
        }
        *out_value = ui_param_prism_tune_normalized_from_parts(coarse, fine);
        return 1U;
    }

    return param_registry_get_track_value(param, track, out_value);
}

static uint8_t ui_param_is_relative_multi_track_candidate(param_id_t param, uint8_t active_track)
{
    if ((param >= PARAM_COUNT)
            || (active_track >= SEQ_TRACK_COUNT)
            || (ui_param_is_track_scoped(param) == 0U)
            || (track_topology_is_role(active_track, TRACK_TOPOLOGY_ROLE_MASTER) != 0U)
            || (track_topology_is_role(active_track, TRACK_TOPOLOGY_ROLE_FX) != 0U))
    {
        return 0U;
    }

    if (ui_param_is_seq_runtime_track_param(param) != 0U)
    {
        return ui_param_track_accepts_relative_param(active_track, param);
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    if ((rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE))
    {
        return 0U;
    }

    return ui_param_track_accepts_relative_param(active_track, param);
}

static uint8_t ui_param_resolve_seq_slot(uint8_t track,
                                         param_id_t param,
                                         uint8_t *out_set_id,
                                         seq_param_slot_t *out_param_slot)
{
    if ((out_set_id == 0) || (out_param_slot == 0) || (track >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    uint8_t set_id = 0U;
    switch (rule.domain)
    {
        case TRACK_RUNTIME_PARAM_DOMAIN_CFG:
            return 0U;
        case TRACK_RUNTIME_PARAM_DOMAIN_ENV:
            set_id = (uint8_t)SEQ_PLOCK_SET_ENV;
            break;
        case TRACK_RUNTIME_PARAM_DOMAIN_TONE:
            set_id = (uint8_t)SEQ_PLOCK_SET_TONE;
            break;
        case TRACK_RUNTIME_PARAM_DOMAIN_PLAY:
            set_id = (uint8_t)SEQ_PLOCK_SET_PLAY;
            break;
        case TRACK_RUNTIME_PARAM_DOMAIN_MOD:
            set_id = (uint8_t)SEQ_PLOCK_SET_MOD;
            break;
        case TRACK_RUNTIME_PARAM_DOMAIN_MIDI_FX:
            set_id = (uint8_t)SEQ_PLOCK_SET_MIDI_FX;
            break;
        case TRACK_RUNTIME_PARAM_DOMAIN_MIX:
            set_id = (uint8_t)SEQ_PLOCK_SET_MIX;
            break;
        default:
            return 0U;
    }

    seq_param_slot_t param_slot = 0U;
    if (seq_param_iface_param_to_slot(track, set_id, param, &param_slot) == 0U)
    {
        return 0U;
    }

    *out_set_id = set_id;
    *out_param_slot = param_slot;
    return 1U;
}

static uint8_t ui_param_live_rec_resolve_context(param_id_t param,
                                                 uint8_t active_track,
                                                 ui_param_live_rec_ctx_t *out_ctx)
{
    if (out_ctx == 0)
    {
        return 0U;
    }

    const seq_track_id_t track = active_track;
    if (track >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    uint8_t set_id = 0U;
    seq_param_slot_t param_slot = 0U;
    if (ui_param_resolve_seq_slot(track, param, &set_id, &param_slot) == 0U)
    {
        return 0U;
    }

    if (seq_runtime_live_rec_param_can_write(track, set_id, param_slot) == 0U)
    {
        return 0U;
    }

    seq_step_id_t step = 0U;
    if (seq_runtime_get_playhead_step(track, &step) == 0U)
    {
        return 0U;
    }

    out_ctx->track = track;
    out_ctx->step = step;
    out_ctx->set_id = set_id;
    out_ctx->param_slot = param_slot;
    return 1U;
}

static uint8_t ui_param_set_track_value(uint8_t encoder,
                                        param_id_t param,
                                        float value,
                                        uint8_t track,
                                        uint8_t update_active_mirror)
{
    if (ui_param_is_seq_runtime_track_param(param) != 0U)
    {
        const param_desc_t *const desc = &param_registry[param];
        const float clamped = ui_param_clamp(value, desc->min, desc->max);
        float current_value = 0.0f;
        if (ui_param_get_seq_runtime_track_value(param, track, &current_value) == 0U)
        {
            return 0U;
        }

        if (ui_param_value_is_same(current_value, clamped) != 0U)
        {
            if (update_active_mirror != 0U)
            {
                param_store_set_active(param, clamped);
            }
            return 1U;
        }

        ui_param_ensure_undo_transaction(encoder, param, track);
        if (ui_param_apply_seq_runtime_track_value(param, track, clamped) == 0U)
        {
            return 0U;
        }

        if (update_active_mirror != 0U)
        {
            param_store_set_active(param, clamped);
        }
        if (undo_v2_is_transaction_open() != 0U)
        {
            (void)undo_v2_record_param_change(param, 1U, track, current_value, clamped);
        }
        return 1U;
    }


    if (ui_param_is_track_scoped(param) == 0U)
    {
        const param_desc_t *const desc = &param_registry[param];
        const float clamped = ui_param_clamp(value, desc->min, desc->max);
        const float before = param_get(param);
        if (ui_param_value_is_same(before, clamped) != 0U)
        {
            return 1U;
        }

        ui_param_ensure_undo_transaction(encoder, param, track);
        param_set(param, clamped);
        if (undo_v2_is_transaction_open() != 0U)
        {
            (void)undo_v2_record_param_change(param, 0U, 0U, before, clamped);
        }
        return 1U;
    }

    if (ui_param_is_prism_tune(param, track) != 0U)
    {
        const float clamped = ui_param_clamp(value, 0.0f, 1.0f);
        float current_value = 0.0f;
        if (ui_param_get_track_edit_value(param, track, &current_value) == 0U)
        {
            return 0U;
        }

        if (ui_param_value_is_same(current_value, clamped) != 0U)
        {
            if (update_active_mirror != 0U)
            {
                param_store_set_active(param, clamped);
                param_store_set_active(ui_param_prism_fine_for_tune(param), 0.5f);
            }
            return 1U;
        }

        ui_param_ensure_undo_transaction(encoder, param, track);

        const param_registry_track_edit_cmd_t fine_cmd = {
            .id = ui_param_prism_fine_for_tune(param),
            .track = track,
            .value = 0.5f
        };
        const param_registry_track_edit_cmd_t coarse_cmd = {
            .id = param,
            .track = track,
            .value = clamped
        };
        if ((param_registry_apply_track_edit(&fine_cmd) == 0U)
                || (param_registry_apply_track_edit(&coarse_cmd) == 0U))
        {
            return 0U;
        }

        uint8_t set_id = 0U;
        seq_param_slot_t param_slot = 0U;
        if (ui_param_resolve_seq_slot(track, param, &set_id, &param_slot) != 0U)
        {
            const seq_value16_t encoded = seq_param_iface_encode_param_value(param, clamped);
            const seq_param_iface_base_commit_cmd_t cmd = {
                .source = SEQ_PARAM_IFACE_COMMIT_SOURCE_UI_TRACK_EDIT,
                .authoritative_apply_done = 1U,
                .target_track = track,
                .set_id = set_id,
                .param_slot = param_slot,
                .value16 = encoded
            };
            (void)seq_param_iface_commit_base_after_authoritative_apply(&cmd);
        }

        if (update_active_mirror != 0U)
        {
            param_store_set_active(param, clamped);
            param_store_set_active(ui_param_prism_fine_for_tune(param), 0.5f);
        }
        if (undo_v2_is_transaction_open() != 0U)
        {
            (void)undo_v2_record_param_change(param, 1U, track, current_value, clamped);
        }
        return 1U;
    }

    const param_desc_t *const desc = &param_registry[param];
    const float clamped = ui_param_clamp(value, desc->min, desc->max);
    float current_value = 0.0f;
    uint8_t note_fx_slot = 0U;
    uint8_t note_fx_param = 0U;
    uint8_t displaced_arp_slot = NOTE_FX_SLOT_NONE;
    if ((note_fx_state_param_map(param, &note_fx_slot, &note_fx_param) != 0U)
            && (note_fx_param == 3U)
            && ((uint8_t)(clamped + 0.5f) == NOTE_FX_MODEL_ARP))
    {
        displaced_arp_slot = note_fx_state_find_arp_slot(track, note_fx_slot);
    }

    if ((param_registry_get_track_value(param, track, &current_value) != 0U)
            && (ui_param_value_is_same(current_value, clamped) != 0U))
    {
        /* Query check before command: avoid redundant apply when the effective value is unchanged. */
        if (update_active_mirror != 0U)
        {
            param_store_set_active(param, clamped);
        }
        return 1U;
    }

    ui_param_ensure_undo_transaction(encoder, param, track);

    const param_registry_track_edit_cmd_t edit_cmd = {
        .id = param,
        .track = track,
        .value = clamped
    };
    if (param_registry_apply_track_edit(&edit_cmd) == 0U)
    {
        return 0U;
    }

    uint8_t set_id = 0U;
    seq_param_slot_t param_slot = 0U;
    if (ui_param_resolve_seq_slot(track, param, &set_id, &param_slot) != 0U)
    {
        const seq_value16_t encoded = seq_param_iface_encode_param_value(param, clamped);
        const seq_param_iface_base_commit_cmd_t cmd = {
            .source = SEQ_PARAM_IFACE_COMMIT_SOURCE_UI_TRACK_EDIT,
            .authoritative_apply_done = 1U,
            .target_track = track,
            .set_id = set_id,
            .param_slot = param_slot,
            .value16 = encoded
        };
        (void)seq_param_iface_commit_base_after_authoritative_apply(&cmd);
    }

    /* Track-scoped contract: active[] mirrors the UI edit context; runtime authority is apply_track_value(track,...). */
    if (update_active_mirror != 0U)
    {
        param_store_set_active(param, clamped);
    }
    if (undo_v2_is_transaction_open() != 0U)
    {
        if (displaced_arp_slot < NOTE_FX_SLOT_COUNT)
        {
            const param_id_t displaced_model = (param_id_t)(PARAM_MIDI_FX_S1_MODEL
                + (displaced_arp_slot * NOTE_FX_PARAM_COUNT));
            (void)undo_v2_record_param_change(displaced_model,
                                              1U,
                                              track,
                                              (float)NOTE_FX_MODEL_ARP,
                                              (float)NOTE_FX_MODEL_OFF);
        }
        (void)undo_v2_record_param_change(param, 1U, track, current_value, clamped);
    }
    return 1U;
}

static uint8_t ui_param_set_active_track_value(uint8_t encoder, param_id_t param, float value, uint8_t active_track)
{
    return ui_param_set_track_value(encoder, param, value, active_track, 1U);
}

static float ui_param_encoder_edit_step(const param_desc_t *desc, const ui_param_encoder_context_t *ctx)
{
    if ((desc == 0) || (ctx == 0))
    {
        return 0.0f;
    }

    if (ctx->shift_down != 0U)
    {
        if (ui_param_is_prism_tune(desc->id, ctx->active_track) != 0U)
        {
            return 0.01f / 48.0f;
        }
        if ((desc->type == PARAM_TYPE_FLOAT)
                || ((desc->type == PARAM_TYPE_BIPOLAR)
                    && ((desc->display_type != PARAM_DISPLAY_INT) || (ui_param_is_stack_osc_tune(desc->id) != 0U))))
        {
            return 0.01f;
        }
    }

    if (ui_param_is_prism_tune(desc->id, ctx->active_track) != 0U)
    {
        return 1.0f / 48.0f;
    }

    if (desc->id == PARAM_FILTER_CUTOFF)
    {
        return 1.0f;
    }

    if ((desc->id == PARAM_MIX_DELAY_MOD_RATE) || (ui_param_is_stack_osc_tune(desc->id) != 0U))
    {
        return 1.0f;
    }

    if ((desc->display_type == PARAM_DISPLAY_PERCENT)
            && (desc->type == PARAM_TYPE_FLOAT)
            && (desc->max > desc->min))
    {
        const float range = desc->max - desc->min;
        return range / 127.0f;
    }

    if (desc->id == PARAM_CFG_TEMPO)
    {
        return 1.0f;
    }

    return desc->step;
}

static uint8_t ui_param_desc_value_count_is_short(const param_desc_t *desc)
{
    if ((desc == 0) || (desc->step <= 0.0f) || (desc->max < desc->min))
    {
        return 0U;
    }

    const float span = desc->max - desc->min;
    const uint32_t intervals = (uint32_t)((span / desc->step) + 0.5f);
    return ((intervals + 1UL) < 20UL) ? 1U : 0U;
}

static uint8_t ui_param_encoder_uses_stepped_accum(const param_desc_t *desc)
{
    if (desc == 0)
    {
        return 0U;
    }

    if ((desc->type != PARAM_TYPE_ENUM)
            && (desc->type != PARAM_TYPE_BOOL)
            && (desc->type != PARAM_TYPE_INT)
            && (desc->display_type != PARAM_DISPLAY_ENUM)
            && (desc->display_type != PARAM_DISPLAY_BOOL)
            && (desc->display_type != PARAM_DISPLAY_INT))
    {
        return 0U;
    }

    return ui_param_desc_value_count_is_short(desc);
}

static uint32_t ui_param_make_stepped_encoder_key(const ui_param_encoder_context_t *ctx,
                                                  uint8_t encoder,
                                                  param_id_t param)
{
    uint32_t key = ((uint32_t)(ctx->active_track & 0x0FU) << 24)
                 | ((uint32_t)(encoder & 0x03U) << 22)
                 | ((uint32_t)param & 0x03FFUL);

    for (uint8_t i = 0U; i < 4U; ++i)
    {
        key ^= ((uint32_t)ctx->bank.params[i] & 0x03FFUL) << (2U + (i * 5U));
    }
    return key;
}

static int16_t ui_param_filter_stepped_encoder_delta(const ui_param_encoder_context_t *ctx,
                                                     uint8_t encoder,
                                                     param_id_t param,
                                                     const param_desc_t *desc,
                                                     int16_t delta)
{
    if ((ctx == 0) || (encoder >= 4U) || (delta == 0) || (ui_param_encoder_uses_stepped_accum(desc) == 0U))
    {
        return delta;
    }

    const uint32_t key = ui_param_make_stepped_encoder_key(ctx, encoder, param);
    if (g_ui_param_stepped_encoder_key[encoder] != key)
    {
        g_ui_param_stepped_encoder_key[encoder] = key;
        g_ui_param_stepped_encoder_accum[encoder] = 0;
    }

    g_ui_param_stepped_encoder_accum[encoder] = (int16_t)(g_ui_param_stepped_encoder_accum[encoder] + delta);
    const int16_t stepped_delta = (int16_t)(g_ui_param_stepped_encoder_accum[encoder] / UI_PARAM_STEPPED_ENCODER_DIVIDER);
    g_ui_param_stepped_encoder_accum[encoder] =
        (int16_t)(g_ui_param_stepped_encoder_accum[encoder] - (stepped_delta * UI_PARAM_STEPPED_ENCODER_DIVIDER));
    return stepped_delta;
}

static uint8_t ui_param_is_lfo_rate(param_id_t param)
{
    return ((param == PARAM_LFO1_RATE) || (param == PARAM_LFO2_RATE) || (param == PARAM_LFO3_RATE)) ? 1U : 0U;
}

static float ui_param_step_lfo_rate(float current_value, int16_t delta, uint8_t shift_down)
{
    if (delta == 0)
    {
        return current_value;
    }

    const int8_t dir = ui_param_signum(delta);
    if (current_value > 0.0001f)
    {
        float next = current_value + (float)dir;
        if (next < 0.5f)
        {
            next = 0.0f;
        }
        return ui_param_clamp(next, 0.0f, (float)MOD_LFO_SYNC_RATE_COUNT);
    }

    if (current_value < -0.0001f)
    {
        const float step = (shift_down != 0U) ? 0.01f : 1.0f;
        float next = current_value + ((float)delta * step);
        if (next > -0.0001f)
        {
            next = 0.0f;
        }
        return ui_param_clamp(next, -LFO_FREE_MAX_HZ, 0.0f);
    }

    if (dir > 0)
    {
        return 1.0f;
    }
    return (shift_down != 0U) ? -0.01f : -1.0f;
}

static float ui_param_apply_delta_value(param_id_t param,
                                        float current_value,
                                        int16_t delta,
                                        float edit_step,
                                        float min_value,
                                        float max_value,
                                        uint8_t shift_down)
{
    if ((param == PARAM_WAVE_OSC1_TUNE) || (param == PARAM_WAVE_OSC2_TUNE))
    {
        const float step = (shift_down != 0U) ? 0.01f : 1.0f;
        return ui_param_clamp(current_value + ((float)delta * step), min_value, max_value);
    }
    if (ui_param_is_lfo_rate(param) != 0U)
    {
        return ui_param_step_lfo_rate(current_value, delta, shift_down);
    }
    return ui_param_clamp(current_value + ((float)delta * edit_step), min_value, max_value);
}

static uint8_t ui_param_apply_relative_delta_to_other_tracks(uint8_t encoder,
                                                             param_id_t param,
                                                             int16_t delta,
                                                             float edit_step,
                                                             uint8_t active_track)
{
    if ((delta == 0) || (ui_is_track_modifier_held() == 0U)
            || (ui_param_relative_multi_track_is_record_context_blocked() != 0U)
            || (ui_param_is_relative_multi_track_candidate(param, active_track) == 0U))
    {
        return 0U;
    }

    const float requested_delta = (float)delta * edit_step;
    uint8_t applied = 0U;

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if ((track == active_track)
                || (track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_MASTER) != 0U)
                || (track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_FX) != 0U))
        {
            continue;
        }

        if (ui_param_track_accepts_relative_param(track, param) == 0U)
        {
            continue;
        }

        float current_value = 0.0f;
        float min_value = 0.0f;
        float max_value = 0.0f;
        if ((ui_param_get_track_edit_value(param, track, &current_value) == 0U)
                || (ui_param_resolve_edit_bounds(param, track, &min_value, &max_value) == 0U))
        {
            continue;
        }

        float next_value = (ui_param_is_lfo_rate(param) != 0U)
            ? ui_param_apply_delta_value(param, current_value, delta, edit_step, min_value, max_value, 0U)
            : ui_param_clamp(current_value + requested_delta, min_value, max_value);
        if (ui_param_value_is_same(next_value, current_value) != 0U)
        {
            continue;
        }

        if (ui_param_set_track_value(encoder, param, next_value, track, 0U) != 0U)
        {
            applied = 1U;
        }
    }

    return applied;
}

static uint8_t ui_param_try_apply_seq_plock(uint8_t encoder,
                                            param_id_t param,
                                            const param_desc_t *desc,
                                            int16_t delta,
                                            float edit_step,
                                            float min_value,
                                            float max_value,
                                            uint8_t active_track)
{
    if ((ui_hall_is_seq_context(ui_get_hall_mode()) == 0U) || (desc == 0))
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

    const seq_track_id_t param_track = active_track;
    if ((seq_edit_track_sequence_is_locked(param_track) != 0U)
            || (seq_edit_track_sequence_is_locked(held_track) != 0U))
    {
        return 1U;
    }

    uint8_t set_id = 0U;
    seq_param_slot_t param_slot = 0U;
    if (ui_param_resolve_seq_slot(param_track, param, &set_id, &param_slot) == 0U)
    {
        return 1U;
    }

    const float delta_value = (float)delta * edit_step;
    const float base_track_value = ui_param_get_active_track_value(param, active_track);
    seq_plock_entry_t prior_entries[SEQ_STEPS_PER_PAGE];
    seq_value16_t target_values[SEQ_STEPS_PER_PAGE];
    uint8_t had_prior_entry[SEQ_STEPS_PER_PAGE];
    uint8_t prior_trig[SEQ_STEPS_PER_PAGE];
    const uint32_t gesture_key = ui_param_get_active_undo_gesture_key(encoder, param, active_track);

    if (undo_v2_begin_transaction(UNDO_V2_TX_KIND_PLOCK,
                                  UNDO_V2_SOURCE_ENCODER,
                                  gesture_key,
                                  UNDO_V2_TX_MODE_DELTA) != UNDO_V2_STATUS_OK)
    {
        return 1U;
    }

    for (uint8_t i = 0U; i < held_count; ++i)
    {
        const seq_step_id_t step = held_steps[i];

        float source_value = base_track_value;
        prior_trig[i] = seq_model_get_trig(param_track, step);
        had_prior_entry[i] = seq_edit_step_plock_find(param_track, step, set_id, param_slot, &prior_entries[i]);
        if (had_prior_entry[i] != 0U)
        {
            source_value = seq_param_iface_decode_param_value(param, prior_entries[i].value16);
        }

        float next_value = source_value + delta_value;
        if (ui_param_macro_fx_quantize_edit(param_track, param, source_value, delta, &next_value) == 0U)
        {
            next_value = ui_param_apply_delta_value(param, source_value, delta, edit_step, min_value, max_value, button_down(BTN_SHIFT) != 0U);
        }
        target_values[i] = seq_param_iface_encode_param_value(param, next_value);
    }

    uint8_t applied_count = 0U;
    for (; applied_count < held_count; ++applied_count)
    {
        const uint8_t before_present = had_prior_entry[applied_count];
        const uint16_t before_value16 = (before_present != 0U) ? prior_entries[applied_count].value16 : 0U;
        const uint8_t before_flags = (before_present != 0U) ? prior_entries[applied_count].flags : 0U;
        const seq_plock_op_status_t status = seq_edit_step_plock_upsert(param_track,
                                                                        held_steps[applied_count],
                                                                        set_id,
                                                                        param_slot,
                                                                        target_values[applied_count],
                                                                        0U);
        if ((status == SEQ_PLOCK_OP_CREATED) || (status == SEQ_PLOCK_OP_UPDATED))
        {
            seq_edit_step_plock_commit(param_track, held_steps[applied_count], set_id, param_slot);
            if ((param_track != held_track) && (seq_model_get_trig(held_track, held_steps[applied_count]) == 0U))
            {
                seq_model_set_trig(held_track, held_steps[applied_count], 1U);
            }
            const undo_v2_status_t undo_status =
                undo_v2_record_plock_change(param_track,
                                            held_steps[applied_count],
                                            set_id,
                                            param_slot,
                                            before_present,
                                            before_value16,
                                            before_flags,
                                            prior_trig[applied_count],
                                            1U,
                                            target_values[applied_count],
                                            0U,
                                            seq_model_get_trig(param_track, held_steps[applied_count]));
            if (undo_status != UNDO_V2_STATUS_OK)
            {
                uint8_t rollback_count = (uint8_t)(applied_count + 1U);
                while (rollback_count > 0U)
                {
                    rollback_count--;
                    if (had_prior_entry[rollback_count] != 0U)
                    {
                        (void)seq_model_step_plock_upsert(param_track,
                                                          held_steps[rollback_count],
                                                          set_id,
                                                          param_slot,
                                                          prior_entries[rollback_count].value16,
                                                          prior_entries[rollback_count].flags);
                    }
                    else
                    {
                        (void)seq_model_step_plock_delete(param_track,
                                                          held_steps[rollback_count],
                                                          set_id,
                                                          param_slot);
                    }
                }

                undo_v2_cancel_transaction();
                return 1U;
            }
            continue;
        }

        while (applied_count > 0U)
        {
            applied_count--;
            if (had_prior_entry[applied_count] != 0U)
            {
                (void)seq_model_step_plock_upsert(param_track,
                                                  held_steps[applied_count],
                                                  set_id,
                                                  param_slot,
                                                  prior_entries[applied_count].value16,
                                                  prior_entries[applied_count].flags);
            }
            else
            {
                (void)seq_model_step_plock_delete(param_track,
                                                  held_steps[applied_count],
                                                  set_id,
                                                  param_slot);
            }
        }

        undo_v2_cancel_transaction();
        return 1U;
    }

    (void)undo_v2_commit_transaction();
    ui_param_note_user_value_flash(encoder,
                                   param,
                                   param_track,
                                   seq_param_iface_decode_param_value(param, target_values[0]),
                                   UI_PARAM_VALUE_FLASH_PLOCK);

    return 1U;
}

static uint8_t ui_param_try_apply_live_rec_plock(uint8_t encoder,
                                                 param_id_t param,
                                                 const param_desc_t *desc,
                                                 int16_t delta,
                                                 float edit_step,
                                                 float min_value,
                                                 float max_value,
                                                 uint8_t active_track)
{
    if ((desc == 0) || (ui_param_is_track_scoped(param) == 0U))
    {
        return 0U;
    }

    ui_param_live_rec_ctx_t live_rec_ctx;
    if (ui_param_live_rec_resolve_context(param, active_track, &live_rec_ctx) == 0U)
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

    float source_value = ui_param_get_active_track_value(param, active_track);
    seq_plock_entry_t existing;
    const uint8_t before_present = seq_edit_step_plock_find(live_rec_ctx.track,
                                                            live_rec_ctx.step,
                                                            live_rec_ctx.set_id,
                                                            live_rec_ctx.param_slot,
                                                            &existing);
    if (before_present != 0U)
    {
        source_value = seq_param_iface_decode_param_value(param, existing.value16);
    }

    float next_value = ui_param_apply_delta_value(param,
                                                  source_value,
                                                  delta,
                                                  edit_step,
                                                  min_value,
                                                  max_value,
                                                  button_down(BTN_SHIFT) != 0U);
    if (ui_param_macro_fx_quantize_edit(active_track, param, source_value, delta, &next_value) == 0U)
    {
        next_value = ui_param_apply_delta_value(param,
                                                source_value,
                                                delta,
                                                edit_step,
                                                min_value,
                                                max_value,
                                                button_down(BTN_SHIFT) != 0U);
    }
    const seq_value16_t encoded = seq_param_iface_encode_param_value(param, next_value);

    if (seq_runtime_live_rec_param_write(live_rec_ctx.track,
                                         live_rec_ctx.set_id,
                                         live_rec_ctx.param_slot,
                                         encoded) == 0U)
    {
        return 0U;
    }

    param_store_set_active(param, next_value);
    ui_param_note_user_value_flash(encoder,
                                   param,
                                   active_track,
                                   next_value,
                                   UI_PARAM_VALUE_FLASH_LIVE_REC_PLOCK);
    return 1U;
}

uint8_t ui_param_try_get_seq_plock_feedback_with_frame(const ui_param_seq_plock_feedback_frame_t *frame_ctx,
                                                       param_id_t param,
                                                       float *out_value,
                                                       uint8_t *out_inverted)
{
    if (out_inverted != 0)
    {
        *out_inverted = 0U;
    }

    if (out_value == 0)
    {
        return 0U;
    }

    if ((frame_ctx == 0)
            || (frame_ctx->seq_context_active == 0U)
            || (frame_ctx->has_ref_step == 0U))
    {
        return 0U;
    }

    const uint8_t plock_track = ui_param_resolve_effective_edit_track(param, ui_get_active_track());
    uint8_t set_id = 0U;
    seq_param_slot_t param_slot = 0U;
    if (ui_param_resolve_seq_slot(plock_track, param, &set_id, &param_slot) == 0U)
    {
        return 0U;
    }

    seq_plock_entry_t existing;
    if (seq_edit_step_plock_find(plock_track,
                                 frame_ctx->ref_step,
                                 set_id,
                                 param_slot,
                                 &existing) == 0U)
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

uint8_t ui_param_try_get_seq_plock_feedback(param_id_t param, float *out_value, uint8_t *out_inverted)
{
    ui_param_seq_plock_feedback_frame_t frame_ctx;
    ui_param_seq_plock_feedback_frame_begin(&frame_ctx);
    return ui_param_try_get_seq_plock_feedback_with_frame(&frame_ctx, param, out_value, out_inverted);
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
    ui_param_encoder_context_t ctx;
    ui_param_capture_encoder_context(&ctx);
    (void)ui_param_handle_encoder_with_context(&ctx, encoder, delta);
}

uint8_t ui_param_handle_encoder_with_context(const ui_param_encoder_context_t *ctx,
                                             uint8_t encoder,
                                             int16_t delta)
{
    if ((ctx == 0) || (ctx->valid == 0U) || (delta == 0) || (encoder >= 4U))
    {
        return 0U;
    }

    const param_id_t param = ctx->bank.params[encoder];
    if (param >= PARAM_COUNT)
    {
        return 0U;
    }
    const uint8_t edit_track = ui_param_resolve_effective_edit_track(param, ctx->active_track);

    const param_desc_t *desc = &param_registry[param];
    const float edit_step = ui_param_encoder_edit_step(desc, ctx);
    float min_value = 0.0f;
    float max_value = 0.0f;
    (void)ui_param_resolve_edit_bounds(param, edit_track, &min_value, &max_value);

    if (ui_param_try_apply_seq_plock(encoder, param, desc, delta, edit_step, min_value, max_value, edit_track) != 0U)
    {
        return 1U;
    }

    if (ui_param_try_apply_live_rec_plock(encoder, param, desc, delta, edit_step, min_value, max_value, edit_track) != 0U)
    {
        return 1U;
    }

    delta = ui_param_filter_stepped_encoder_delta(ctx, encoder, param, desc, delta);
    if (delta == 0)
    {
        return 0U;
    }

    float value = ui_param_get_active_track_value(param, ctx->active_track);

    if (param == PARAM_CFG_TRACK)
    {
        value = ui_param_step_cfg_track(value, ui_param_signum(delta), edit_track);
        if (ui_param_value_is_same(value, ui_param_get_active_track_value(param, ctx->active_track)) != 0U)
        {
            return 0U;
        }
        const uint8_t structural_undo = ui_param_begin_structural_undo(encoder, param, edit_track);
        const uint8_t structural_applied = ui_param_set_active_track_value(encoder, param, value, edit_track);
        if (structural_applied != 0U)
        {
            ui_param_note_user_value_flash(encoder,
                                           param,
                                           edit_track,
                                           value,
                                           UI_PARAM_VALUE_FLASH_DIRECT);
        }
        ui_param_finish_structural_undo(structural_undo, structural_applied);
        if ((g_ui_param_encoder_edit_group_active == 0U) && (undo_v2_is_transaction_open() != 0U))
        {
            (void)undo_v2_commit_transaction();
        }
        return 1U;
    }

    if (param == PARAM_CFG_TRACK_TYPE)
    {
        value = ui_param_step_cfg_track_type(value, ui_param_signum(delta), edit_track);
        if (ui_param_value_is_same(value, ui_param_get_active_track_value(param, ctx->active_track)) != 0U)
        {
            return 0U;
        }
        const uint8_t structural_undo = ui_param_begin_structural_undo(encoder, param, edit_track);
        const uint8_t structural_applied = ui_param_set_active_track_value(encoder, param, value, edit_track);
        if (structural_applied != 0U)
        {
            ui_param_note_user_value_flash(encoder,
                                           param,
                                           edit_track,
                                           value,
                                           UI_PARAM_VALUE_FLASH_DIRECT);
        }
        ui_param_finish_structural_undo(structural_undo, structural_applied);
        if ((g_ui_param_encoder_edit_group_active == 0U) && (undo_v2_is_transaction_open() != 0U))
        {
            (void)undo_v2_commit_transaction();
        }
        return 1U;
    }

    const float source_current_value = value;
    {
        if (ui_param_macro_fx_step_type(edit_track, param, source_current_value, delta, &value) != 0U)
        {
            /* MacroFX TYPE has slot-level availability constraints for unique FX resources. */
        }
        else if (ui_param_macro_fx_quantize_edit(edit_track, param, source_current_value, delta, &value) == 0U)
        {
            value = ui_param_apply_delta_value(param,
                                               source_current_value,
                                               delta,
                                               edit_step,
                                               min_value,
                                               max_value,
                                               ctx->shift_down);
        }

        if (ui_param_value_is_same(value, source_current_value) != 0U)
        {
            if ((param == PARAM_CFG_POLY_VOICES) && (delta > 0))
                ui_core_feedback_set("VOICE MAX", HAL_GetTick());
            const uint8_t applied = ui_param_apply_relative_delta_to_other_tracks(encoder, param, delta, edit_step, edit_track);
            if ((g_ui_param_encoder_edit_group_active == 0U) && (undo_v2_is_transaction_open() != 0U))
            {
                (void)undo_v2_commit_transaction();
            }
            return applied;
        }
    }

    const uint8_t structural_undo = ui_param_begin_structural_undo(encoder, param, edit_track);
    uint8_t source_applied = ui_param_set_active_track_value(encoder, param, value, edit_track);
    ui_param_finish_structural_undo(structural_undo, source_applied);
    if (source_applied != 0U)
    {
        ui_param_note_user_value_flash(encoder,
                                       param,
                                       edit_track,
                                       value,
                                       UI_PARAM_VALUE_FLASH_DIRECT);
    }
    (void)ui_param_apply_relative_delta_to_other_tracks(encoder, param, delta, edit_step, edit_track);
    if ((g_ui_param_encoder_edit_group_active == 0U) && (undo_v2_is_transaction_open() != 0U))
    {
        (void)undo_v2_commit_transaction();
    }
    return 1U;
}
