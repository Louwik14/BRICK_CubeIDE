#include "ui_core_clipboard.h"
#include "App/control_clipboard.h"
#include "App/Hall/hall_engine.h"

#include <string.h>
#include <math.h>

#include "buttons.h"
#include "ui_core.h"
#include "ui_page_manager.h"
#include "ui_template_page.h"
#include "ui_macro_interaction.h"
#include "Storage/project_control.h"
#include "Platform/memory_layout.h"
#include "App/engine_tasklet.h"
#include "Track/track_state.h"
#include "Track/track_input_ownership.h"
#include "Track/track_mute.h"
#include "Track/control_routing.h"
#include "Track/tone_program_control.h"
#include "Track/fm_control_state.h"
#include "Track/audio_fx_control_state.h"
#include "Track/polyphony_control.h"
#include "Track/synth_polyphony.h"
#include "Track/mixer_control_state.h"
#include "Track/track_sound_state.h"
#include "Param/param_filter.h"
#include "Mod/mod_env3_control.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Storage/asset_ref.h"
#include "IPC/live_clock_control.h"
#include "App/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"
#include "Param/live_parameter_migration.h"
#include "NoteFx/note_fx_state.h"
#include "NoteFx/note_fx_pipeline.h"
#include "param_registry.h"
#include "Track/track_runtime.h"
#include "Track/entity_topology.h"
#include "Track/track_catalog.h"
#include "Mod/mod_matrix_control.h"
#include "Mod/mod_destination_control.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_model.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_clipboard.h"
#include "Seq/seq_step_snapshot.h"
#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"

#define UI_PAGE_CLIPBOARD_CAPACITY 4U
#define UI_ENSEMBLE_CLIPBOARD_CAPACITY 39U
#define UI_TRACK_CLIPBOARD_ENTITY_CAPACITY (1U + BRICK_ENTITY_GROUP_CHILD_COUNT)
#define UI_TRACK_CLIPBOARD_ASSET_CAPACITY PROJECT_CONTROL_ASSET_ROLE_COUNT

typedef struct
{
    param_id_t id;
    float value;
} ui_param_clipboard_entry_t;

typedef struct
{
    uint8_t scope;
    param_registry_prepared_track_target_t track;
    param_id_t global_id;
    float global_value;
} ui_clipboard_prepared_control_t;

typedef struct
{
    uint8_t valid;
    uint8_t count;
    uint8_t target_count;
    param_id_t target_params[UI_PAGE_CLIPBOARD_CAPACITY];
    ui_param_clipboard_entry_t entry[UI_PAGE_CLIPBOARD_CAPACITY];
} ui_page_clipboard_t;

typedef struct
{
    uint8_t valid;
    uint8_t count;
    uint8_t target_count;
    param_id_t target_params[UI_ENSEMBLE_CLIPBOARD_CAPACITY];
    ui_param_clipboard_entry_t entry[UI_ENSEMBLE_CLIPBOARD_CAPACITY];
} ui_ensemble_clipboard_t;

typedef struct
{
    uint8_t valid;
    uint8_t track;
    uint8_t count;
    seq_step_id_t steps[SEQ_MAX_STEPS];
} ui_seq_clipboard_target_t;

typedef struct
{
    uint8_t length;
    uint8_t division;
    uint8_t quantization;
    uint8_t swing;
    seq_play_snapshot_t play_base;
    seq_step_snapshot_t step[SEQ_MAX_STEPS];
} ui_track_clipboard_sequence_t;

typedef struct
{
    track_mod_matrix_slot_t slot[MOD_MATRIX_SLOT_COUNT];
    uint8_t multi_source[2U][2U];
    uint8_t slew_source[2U];
    float slew_amount[2U];
} ui_track_clipboard_matrix_t;

typedef struct
{
    uint8_t valid;
    track_config_t config;
    uint8_t external_input;
    uint8_t midi_channel;
    track_midi_source_t midi_source;
    uint8_t fm_present;
    tone_program_control_t tone;
    fm_control_state_t fm;
    param_filter_control_state_t filter;
    vca_control_state_t vca;
    mod_env3_control_state_t env3;
    mod_lfo_control_bank_t lfo;
    ui_track_clipboard_matrix_t matrix;
    uint8_t modulation_present;
    audio_fx_control_state_t audio_fx;
    polyphony_control_state_t polyphony;
    mixer_control_state_t mixer;
    note_fx_track_state_t note_fx;
    ui_track_clipboard_sequence_t sequence;
    uint8_t asset_count;
    persist_control_asset_ref_t assets[UI_TRACK_CLIPBOARD_ASSET_CAPACITY];
} ui_track_clipboard_payload_t;

typedef struct
{
    uint8_t valid;
    uint8_t source_track;
    uint8_t payload_count;
    ui_track_clipboard_payload_t payload[UI_TRACK_CLIPBOARD_ENTITY_CAPACITY];
} ui_track_clipboard_t;

typedef struct
{
    uint8_t valid;
    project_control_macro_lock_t lock;
} ui_macro_lock_clipboard_t;

typedef struct
{
    ui_macro_lock_clipboard_t macro_lock;
    ui_track_clipboard_t track;
    ui_ensemble_clipboard_t ensemble;
    ui_page_clipboard_t page;
    ui_seq_clipboard_target_t sequence_target;
} ui_clipboard_state_t;

UI_SDRAM static ui_clipboard_state_t g_ui_clipboard;
static volatile uint8_t g_clipboard_control_owned;

static uint8_t ui_core_clipboard_note_fx_param_kind(param_id_t id, uint8_t *out_param)
{
    uint8_t slot = 0U;
    uint8_t param = 0U;
    if ((out_param == 0) || (note_fx_state_param_map(id, &slot, &param) == 0U))
    {
        return 0U;
    }

    *out_param = param;
    return 1U;
}

static uint8_t ui_core_clipboard_param_phase(param_id_t id)
{
    for (uint8_t order = 0U; ; ++order)
    {
        const param_id_t audio_fx_id = param_registry_get_audio_fx_param(order);
        if (audio_fx_id == PARAM_COUNT)
            break;
        if (audio_fx_id == id)
        {
            return order;
        }
    }

    uint8_t note_fx_param = 0U;
    if ((ui_core_clipboard_note_fx_param_kind(id, &note_fx_param) != 0U)
            && (note_fx_param == 3U))
    {
        return 5U;
    }
    return 4U;
}
static void ui_core_clipboard_feedback(ui_core_clipboard_feedback_fn feedback, const char *message)
{
    if (feedback != 0)
    {
        feedback(message);
    }
}

uint8_t control_clipboard_ui_available(void)
{
    return (g_clipboard_control_owned == 0U) ? 1U : 0U;
}

uint8_t control_clipboard_request_apply(control_clipboard_operation_t operation,
                                        uint8_t target,
                                        uint8_t arg0,
                                        uint8_t arg1)
{
    if (g_clipboard_control_owned != 0U) return 0U;
    g_clipboard_control_owned = 1U;
    const control_clipboard_intent_t intent = {
        .operation = (uint8_t)operation,
        .target = target,
        .arg0 = arg0,
        .arg1 = arg1
    };
    if (control_domain_request_clipboard(&intent) == 0U)
    {
        g_clipboard_control_owned = 0U;
        return 0U;
    }
    return 1U;
}

uint8_t control_clipboard_request_sequence_apply(uint8_t track,
                                                 const seq_step_id_t *steps,
                                                 uint8_t count,
                                                 uint8_t clear)
{
    if ((steps == 0) || (count == 0U) || (count > SEQ_MAX_STEPS)
        || (control_clipboard_ui_available() == 0U))
        return 0U;
    g_ui_clipboard.sequence_target.valid = 1U;
    g_ui_clipboard.sequence_target.track = track;
    g_ui_clipboard.sequence_target.count = count;
    memcpy(g_ui_clipboard.sequence_target.steps, steps,
           (size_t)count * sizeof(steps[0]));
    return control_clipboard_request_apply(
        (clear != 0U) ? CONTROL_CLIPBOARD_CLEAR_SEQUENCE
                      : CONTROL_CLIPBOARD_APPLY_SEQUENCE,
        track, 0U, 0U);
}

static uint8_t ui_core_clipboard_macro_make_empty_lock(project_control_macro_lock_t *out_lock)
{
    if (out_lock == 0)
    {
        return 0U;
    }

    out_lock->track = 0xFFU;
    out_lock->param = PARAM_COUNT;
    out_lock->scene_value = 0.0f;
    return 1U;
}

static uint8_t ui_core_clipboard_resolve_active_macro_lock_target(uint8_t *out_scene,
                                                                  uint8_t *out_lock)
{
    return ui_macro_interaction_get_active_lock_target(out_scene, out_lock);
}

static uint8_t ui_core_clipboard_copy_macro_lock(uint8_t scene, uint8_t lock)
{
    project_control_macro_lock_t current;

    if (project_control_get_scene_lock(scene, lock, &current) == 0U)
    {
        return 0U;
    }

    g_ui_clipboard.macro_lock.lock = current;
    g_ui_clipboard.macro_lock.valid = 1U;
    return 1U;
}

static uint8_t ui_core_clipboard_paste_macro_lock(uint8_t scene, uint8_t lock)
{
    if (g_ui_clipboard.macro_lock.valid == 0U)
    {
        return 0U;
    }

    return project_control_set_scene_lock(scene, lock, &g_ui_clipboard.macro_lock.lock);
}

static uint8_t ui_core_clipboard_clear_macro_lock(uint8_t scene, uint8_t lock)
{
    project_control_macro_lock_t empty_lock;
    if (ui_core_clipboard_macro_make_empty_lock(&empty_lock) == 0U)
    {
        return 0U;
    }

    return project_control_set_scene_lock(scene, lock, &empty_lock);
}

static uint8_t ui_core_clipboard_collect_params_from_subpage(const ui_template_subpage_t *subpage,
                                                             param_id_t *out_ids,
                                                             uint8_t capacity,
                                                             uint8_t *inout_count)
{
    if ((subpage == 0) || (out_ids == 0) || (inout_count == 0))
    {
        return 0U;
    }

    uint8_t count = *inout_count;
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        const param_id_t id = subpage->param_bank.params[i];
        if (id >= PARAM_COUNT)
        {
            continue;
        }

        uint8_t already_present = 0U;
        for (uint8_t j = 0U; j < count; ++j)
        {
            if (out_ids[j] == id)
            {
                already_present = 1U;
                break;
            }
        }

        if (already_present != 0U)
        {
            continue;
        }

        if (count >= capacity)
        {
            return 0U;
        }

        out_ids[count++] = id;
    }

    *inout_count = count;
    return 1U;
}

static uint8_t ui_core_clipboard_get_held_param_button(button_id_t *out_button)
{
    if (out_button == 0)
    {
        return 0U;
    }

    static const button_id_t k_param_buttons[] = {
        BTN_PARAM_1, BTN_PARAM_2, BTN_PARAM_3, BTN_PARAM_4, BTN_PARAM_5, BTN_PARAM_6
    };

    for (uint8_t i = 0U; i < (uint8_t)(sizeof(k_param_buttons) / sizeof(k_param_buttons[0])); ++i)
    {
        if (button_down(k_param_buttons[i]) != 0U)
        {
            *out_button = k_param_buttons[i];
            return 1U;
        }
    }

    return 0U;
}

static uint8_t ui_core_clipboard_midi_fx_shortcut_is_held(void)
{
    if (ui_page_get_id() != UI_PAGE_MIDI_FX)
    {
        return 0U;
    }
    const uint8_t hall = 6U;
    return hall_engine_is_pressed(hall);
}

static uint8_t ui_core_clipboard_resolve_template_family_from_button(button_id_t button,
                                                                     ui_template_family_id_t *out_family)
{
    if (out_family == 0)
    {
        return 0U;
    }

    switch (button)
    {
        case BTN_PARAM_1: *out_family = UI_TEMPLATE_FAMILY_ENV; return 1U;
        case BTN_PARAM_2: *out_family = UI_TEMPLATE_FAMILY_TONE; return 1U;
        case BTN_PARAM_3: *out_family = UI_TEMPLATE_FAMILY_MOD; return 1U;
        case BTN_PARAM_4: *out_family = UI_TEMPLATE_FAMILY_MIX; return 1U;
        case BTN_PARAM_5: *out_family = UI_TEMPLATE_FAMILY_PLAY; return 1U;
        case BTN_PARAM_6: *out_family = UI_TEMPLATE_FAMILY_ENV; return 1U;
        default: return 0U;
    }
}

static uint8_t ui_core_clipboard_collect_ensemble_params(ui_template_family_id_t family_id,
                                                         param_id_t *out_ids,
                                                         uint8_t *out_count)
{
    if ((out_ids == 0) || (out_count == 0) || ((uint8_t)family_id >= (uint8_t)UI_TEMPLATE_FAMILY_COUNT))
    {
        return 0U;
    }

    uint8_t count = 0U;
    const uint8_t active_track = ui_get_active_lane();
    const uint8_t scope_count = ui_template_family_get_effective_scope_count(family_id, active_track);
    if (scope_count == 0U)
    {
        return 0U;
    }

    for (uint8_t scope = 0U; scope < scope_count; ++scope)
    {
        const uint8_t scope_index = (scope_count > 1U) ? scope : UI_TEMPLATE_EFFECTIVE_SCOPE_CURRENT;
        const ui_template_family_t *const family =
                ui_template_family_resolve_effective_for_track(family_id, active_track, scope_index);
        if (family == 0)
        {
            return 0U;
        }

        for (uint8_t sp = 0U; sp < 4U; ++sp)
        {
            if (ui_core_clipboard_collect_params_from_subpage(&family->subpages[sp], out_ids,
                    UI_ENSEMBLE_CLIPBOARD_CAPACITY, &count) == 0U)
            {
                return 0U;
            }
        }
    }

    *out_count = count;
    return (count > 0U) ? 1U : 0U;
}

static uint8_t ui_core_clipboard_collect_active_page_params(param_id_t *out_ids, uint8_t *out_count)
{
    if ((out_ids == 0) || (out_count == 0))
    {
        return 0U;
    }

    const ui_page_t *const page = ui_page_get();
    if ((page == 0) || (page->context == 0))
    {
        return 0U;
    }

    const ui_template_page_state_t *const state = (const ui_template_page_state_t *)page->context;
    const ui_template_subpage_t *subpage = ui_template_page_get_active_subpage(state);
    if (subpage == 0)
    {
        return 0U;
    }

    uint8_t count = 0U;
    if (ui_core_clipboard_collect_params_from_subpage(subpage, out_ids,
            UI_PAGE_CLIPBOARD_CAPACITY, &count) == 0U)
    {
        return 0U;
    }

    *out_count = count;
    return (count > 0U) ? 1U : 0U;
}

static uint8_t ui_core_clipboard_get_active_page_button(button_id_t *out_button)
{
    if (out_button == 0)
    {
        return 0U;
    }

    const ui_page_t *const page = ui_page_get();
    if ((page == 0) || (page->context == 0))
    {
        return 0U;
    }

    const ui_template_page_state_t *const state = (const ui_template_page_state_t *)page->context;
    if (state->active_subpage >= 4U)
    {
        return 0U;
    }

    *out_button = (button_id_t)((uint8_t)BTN_PAGE_1 + state->active_subpage);
    return 1U;
}

static uint8_t ui_core_clipboard_is_active_page_button_held(void)
{
    button_id_t active_page_button = BTN_COUNT;
    if (ui_core_clipboard_get_active_page_button(&active_page_button) == 0U)
    {
        return 0U;
    }

    return (button_down(active_page_button) != 0U) ? 1U : 0U;
}

static uint8_t ui_core_clipboard_bulk_add(live_parameter_audio_bulk_t *bulk,
                                          param_id_t id,
                                          uint8_t track,
                                          float value,
                                          audio_fx_control_prepare_context_t *audio_fx_context,
                                          ui_clipboard_prepared_control_t *prepared,
                                          uint8_t *prepared_count)
{
    if ((bulk == 0) || (bulk->count >= LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS)
            || (id >= PARAM_COUNT) || (prepared == NULL)
            || (prepared_count == NULL)
            || (*prepared_count >= UI_ENSEMBLE_CLIPBOARD_CAPACITY))
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    const uint8_t scope = (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
        ? LIVE_PARAMETER_EVENT_SCOPE_GLOBAL : LIVE_PARAMETER_EVENT_SCOPE_TRACK;
    const uint8_t event_track = (scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK) ? track : 0U;
    float command_value = value;
    ui_clipboard_prepared_control_t next = { .scope = scope };
    if (scope == LIVE_PARAMETER_EVENT_SCOPE_GLOBAL)
    {
        param_registry_prepared_value_t global_prepared;
        float model_value = 0.0f;
        if (!isfinite(value)
                || (param_registry_prepare_value(
                    id, value, &global_prepared) == 0U)
                || (param_registry_query_global(
                    PARAM_MODFX_MODEL, &model_value) == 0U)
                || (param_registry_prepare_global_audio_command(
                    id, global_prepared.value,
                    (uint8_t)(((id == PARAM_MODFX_MODEL)
                        ? global_prepared.value : model_value) + 0.5f),
                    &command_value) == 0U)) return 0U;
        next.global_id = id;
        next.global_value = global_prepared.value;
    }
    else if (param_registry_prepare_track_control_target(id, track, value,
                audio_fx_context, &next.track) == 0U)
        return 0U;
    else
        command_value = next.track.canonical_value;
    live_parameter_audio_bulk_item_t *const item = &bulk->item[bulk->count++];
    item->parameter_id = (uint16_t)id;
    item->scope = scope;
    item->track = event_track;
    item->slot = LIVE_PARAMETER_EVENT_INVALID_INDEX;
    item->flags = LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS;
    item->value = live_parameter_event_encode_float(command_value);
    prepared[(*prepared_count)++] = next;
    return 1U;
}

static uint8_t ui_core_clipboard_bulk_accept_control_values(
    const ui_clipboard_prepared_control_t *prepared, uint8_t prepared_count)
{
    if (prepared == NULL) return 0U;
    for (uint8_t i = 0U; i < prepared_count; ++i)
    {
        const uint8_t ok = (prepared[i].scope == LIVE_PARAMETER_EVENT_SCOPE_GLOBAL)
            ? param_registry_install_prepared_global_control_target(
                prepared[i].global_id, prepared[i].global_value)
            : param_registry_install_prepared_track_control_target(&prepared[i].track);
        if (ok == 0U) return 0U;
    }
    return 1U;
}

static uint8_t ui_core_clipboard_audio_fx_project(
    uint8_t track, const param_id_t *ids, const float *values, uint8_t count,
    audio_fx_control_prepare_context_t *context)
{
    if ((ids == NULL) || (values == NULL) || (context == NULL)
            || (audio_fx_control_prepare_context_init(track, context) == 0U))
        return 0U;
    for (uint8_t i = 0U; i < count; ++i)
        if (((ids[i] == PARAM_AUDIO_FX_MODEL)
                || (ids[i] == PARAM_AUDIO_FX_B_MODEL))
                && (audio_fx_control_prepare_project_model(
                    track, ids[i], values[i], context) == 0U))
            return 0U;
    return audio_fx_control_prepare_finalize(track, context);
}

static uint8_t ui_core_clipboard_clear_param_list_to_min(uint8_t track,
                                                         const param_id_t *params,
                                                         uint8_t count)
{
    if (params == 0)
    {
        return 0U;
    }

    live_parameter_audio_bulk_t bulk = {
        .capture_tick = 0U,
        .count = 0U
    };
    ui_clipboard_prepared_control_t prepared[UI_ENSEMBLE_CLIPBOARD_CAPACITY];
    uint8_t prepared_count = 0U;
    audio_fx_control_prepare_context_t audio_fx_context = {0};
    uint8_t direct_count = 0U;
    param_id_t direct_ids[UI_ENSEMBLE_CLIPBOARD_CAPACITY];
    float projected_values[UI_ENSEMBLE_CLIPBOARD_CAPACITY];
    for (uint8_t i = 0U; i < count; ++i)
        projected_values[i] = (params[i] < PARAM_COUNT)
            ? param_registry[params[i]].min : 0.0f;
    if (ui_core_clipboard_audio_fx_project(track, params, projected_values,
            count, &audio_fx_context) == 0U) return 0U;
    for (uint8_t pass = 0U; pass < 6U; ++pass)
    {
        for (uint8_t i = 0U; i < count; ++i)
        {
            const param_id_t id = params[i];
            if (ui_core_clipboard_param_phase(id) != pass)
            {
                continue;
            }

            if (id < PARAM_COUNT)
            {
                const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
                if (live_parameter_is_audio_owned(id) != 0U)
                {
                    if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
                    {
                        direct_ids[direct_count++] = id;
                    }
                    else if (ui_core_clipboard_bulk_add(&bulk, id, track,
                                param_registry[id].min, &audio_fx_context,
                                prepared, &prepared_count) == 0U)
                    {
                        return 0U;
                    }
                }
                else
                {
                    direct_ids[direct_count++] = id;
                }
            }
        }
    }
    if ((bulk.count != 0U)
            && (live_parameter_audio_publication_submit_bulk_now(&bulk) == false))
    {
        return 0U;
    }
    if (ui_core_clipboard_bulk_accept_control_values(
            prepared, prepared_count) == 0U) return 0U;
    for (uint8_t i = 0U; i < direct_count; ++i)
    {
        const param_id_t id = direct_ids[i];
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        const uint8_t ok = (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
            ? param_registry_commit_global(id, param_registry[id].min)
            : param_registry_apply_track_value(id, track, param_registry[id].min);
        if (ok == 0U)
        {
            return 0U;
        }
    }
    return (uint8_t)((direct_count != 0U) || (bulk.count != 0U));
}

static uint8_t ui_track_clipboard_capture_sequence(
    uint8_t track, ui_track_clipboard_sequence_t *out)
{
    if ((track >= SEQ_LANE_CAPACITY) || (out == NULL)) return 0U;
    memset(out, 0, sizeof(*out));
    out->length = seq_model_get_track_length(track);
    if ((seq_runtime_get_track_div(track, &out->division) == 0U)
            || (seq_runtime_get_track_quant(track, &out->quantization) == 0U)
            || (seq_runtime_get_track_swing(track, &out->swing) == 0U)
            || (seq_model_play_base_capture(track, &out->play_base) == 0U))
        return 0U;
    for (seq_step_id_t step = 0U; step < SEQ_MAX_STEPS; ++step)
        if (seq_step_snapshot_capture(track, step, &out->step[step]) == 0U)
            return 0U;
    return 1U;
}

static uint8_t ui_track_clipboard_capture_lfo(
    uint8_t track, mod_lfo_control_bank_t *out)
{
    if (out == NULL) return 0U;
    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        if ((mod_lfo_v1_get_track_param(track, lfo, MOD_LFO_PARAM_RATE,
                                         &out->lfo[lfo].rate) == 0U)
                || (mod_lfo_v1_get_track_param(track, lfo, MOD_LFO_PARAM_SHAPE,
                                                &out->lfo[lfo].shape) == 0U)
                || (mod_lfo_v1_get_track_param(track, lfo, MOD_LFO_PARAM_TRIG,
                                                &out->lfo[lfo].trigger) == 0U)
                || (mod_lfo_v1_get_track_param(track, lfo, MOD_LFO_PARAM_PHASE,
                                                &out->lfo[lfo].phase) == 0U))
            return 0U;
    }
    return 1U;
}

static uint8_t ui_track_clipboard_capture_assets(
    uint8_t track, ui_track_clipboard_payload_t *out)
{
    if (out == NULL) return 0U;
    const track_family_t family = out->config.family;
    const track_type_t type = out->config.type;
    if ((family == TRACK_FAMILY_SAMPLER)
            && ((type == TRACK_TYPE_STREAM) || (type == TRACK_TYPE_RAM)
                || (type == TRACK_TYPE_MULTI)))
    {
        persist_control_asset_ref_t asset;
        if (project_control_track_asset_get(
                track, PROJECT_CONTROL_ASSET_SAMPLER, &asset) != 0U)
            out->assets[out->asset_count++] = asset;
    }
    else if ((family == TRACK_FAMILY_SYNTH) && (type == TRACK_TYPE_WAVE))
    {
        for (uint8_t osc = 0U; osc < 2U; ++osc)
        {
            persist_control_asset_ref_t asset;
            if (project_control_track_asset_get(track,
                    (project_control_asset_role_t)(PROJECT_CONTROL_ASSET_WAVE_OSC1 + osc),
                    &asset) != 0U)
                out->assets[out->asset_count++] = asset;
        }
    }
    return 1U;
}

static uint8_t ui_track_clipboard_capture_payload(
    uint8_t track, ui_track_clipboard_payload_t *out)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (out == NULL)) return 0U;
    memset(out, 0, sizeof(*out));
    out->config = track_state_get_config(track);
    out->external_input = (track < TRACK_COUNT)
        ? track_state_get_external_input(track) : 0U;
    out->midi_channel = track_state_get_midi_channel(track);
    out->midi_source = track_state_get_midi_source(track);
    out->fm_present = (uint8_t)((out->config.family == TRACK_FAMILY_SYNTH)
        && (out->config.type == TRACK_TYPE_FM));
    if (((out->fm_present != 0U)
            ? fm_control_state_get(track, &out->fm)
            : tone_program_control_capture(track, &out->tone)) == 0U)
        return 0U;
    if ((param_filter_control_capture(track, &out->filter) == 0U)
            || (vca_control_state_capture(track, &out->vca) == 0U)
            || (audio_fx_control_state_capture(track, &out->audio_fx) == 0U)
            || (polyphony_control_capture(track, &out->polyphony) == 0U)
            || (mixer_control_state_capture(track, &out->mixer) == 0U)
            || (note_fx_state_capture_track(track, &out->note_fx) == 0U)
            || (ui_track_clipboard_capture_sequence(track, &out->sequence) == 0U)
            || (ui_track_clipboard_capture_assets(track, out) == 0U))
        return 0U;

    brick_entity_id_t mod_owner = track;
    if ((entity_topology_mod_owner(track, &mod_owner) == 0U)) return 0U;
    if (mod_owner == track)
    {
        const track_sound_state_t *const matrix = track_sound_state_get_const(track);
        if ((matrix == NULL)
                || (mod_env3_control_capture(track, &out->env3) == 0U)
                || (ui_track_clipboard_capture_lfo(track, &out->lfo) == 0U))
            return 0U;
        memcpy(out->matrix.slot, matrix->mod_matrix, sizeof(out->matrix.slot));
        memcpy(out->matrix.multi_source, matrix->mod_multi_source,
               sizeof(out->matrix.multi_source));
        memcpy(out->matrix.slew_source, matrix->mod_slew_source,
               sizeof(out->matrix.slew_source));
        memcpy(out->matrix.slew_amount, matrix->mod_slew_amount,
               sizeof(out->matrix.slew_amount));
        out->modulation_present = 1U;
    }
    out->valid = 1U;
    return 1U;
}

static uint8_t ui_core_clipboard_copy_track(uint8_t track)
{
    ui_track_clipboard_t *const cb = &g_ui_clipboard.track;
    entity_topology_descriptor_t topology;
    memset(cb, 0, sizeof(*cb));
    cb->source_track = track;
    if ((entity_topology_get(track, &topology) == 0U)
            || (ui_track_clipboard_capture_payload(track, &cb->payload[0]) == 0U))
        return 0U;
    cb->payload_count = 1U;
    if (topology.role == ENTITY_ROLE_GROUP_MASTER)
    {
        for (uint8_t member = 0U; member < BRICK_ENTITY_GROUP_CHILD_COUNT; ++member)
        {
            brick_entity_id_t child = BRICK_ENTITY_INVALID_ID;
            if ((entity_topology_group_child(topology.entity_id, member, &child) == 0U)
                    || (ui_track_clipboard_capture_payload(
                            child, &cb->payload[1U + member]) == 0U))
            {
                memset(cb, 0, sizeof(*cb));
                return 0U;
            }
        }
        cb->payload_count = UI_TRACK_CLIPBOARD_ENTITY_CAPACITY;
    }
    cb->valid = 1U;
    return 1U;
}

static uint8_t ui_track_clipboard_validate_sequence(
    uint8_t target, track_type_t type, const ui_track_clipboard_sequence_t *seq)
{
    if ((seq == NULL) || (seq->length == 0U) || (seq->length > SEQ_MAX_STEPS)
            || ((seq->division != 1U) && (seq->division != 2U)
                && (seq->division != 4U) && (seq->division != 8U))
            || (seq->quantization > 100U) || (seq->swing > 100U)
            || (seq_edit_track_sequence_is_locked(target) != 0U))
        return 0U;
    uint16_t lock_count = 0U;
    for (uint8_t step = 0U; step < SEQ_MAX_STEPS; ++step)
    {
        if (seq_step_snapshot_validate_for_target(
                1U, 1U, (uint8_t)type, &seq->step[step]) == 0U)
            return 0U;
        lock_count = (uint16_t)(lock_count + seq->step[step].lock_count);
    }
    return (lock_count <= seq_model_get_track_plock_capacity(target)) ? 1U : 0U;
}

static uint8_t ui_track_clipboard_validate_assets(
    const ui_track_clipboard_payload_t *payload)
{
    if ((payload == NULL) || (payload->asset_count > UI_TRACK_CLIPBOARD_ASSET_CAPACITY))
        return 0U;
    const uint8_t sampler = (uint8_t)((payload->config.family == TRACK_FAMILY_SAMPLER)
        && ((payload->config.type == TRACK_TYPE_STREAM)
            || (payload->config.type == TRACK_TYPE_RAM)
            || (payload->config.type == TRACK_TYPE_MULTI)));
    const uint8_t wave = (uint8_t)((payload->config.family == TRACK_FAMILY_SYNTH)
        && (payload->config.type == TRACK_TYPE_WAVE));
    if (((sampler != 0U) && (payload->asset_count > 1U))
            || ((wave != 0U) && (payload->asset_count > 2U))
            || ((sampler == 0U) && (wave == 0U) && (payload->asset_count != 0U)))
        return 0U;
    for (uint8_t i = 0U; i < payload->asset_count; ++i)
        if (project_control_validate_asset(&payload->assets[i]) == 0U)
            return 0U;
    return 1U;
}

static uint8_t ui_track_clipboard_target_at(
    uint8_t root, uint8_t index, uint8_t payload_count, uint8_t *out)
{
    if ((out == NULL) || (index >= payload_count)) return 0U;
    if (index == 0U) { *out = root; return 1U; }
    if ((payload_count != UI_TRACK_CLIPBOARD_ENTITY_CAPACITY)
            || (root != BRICK_ENTITY_GROUP_MASTER_ID)) return 0U;
    *out = (uint8_t)(BRICK_ENTITY_FIRST_GROUP_CHILD_ID + index - 1U);
    return 1U;
}

static uint8_t ui_track_clipboard_validate_matrix(
    uint8_t source, uint8_t target, const ui_track_clipboard_matrix_t *matrix,
    const track_config_t configs[BRICK_ENTITY_CAPACITY])
{
    const uint8_t group_active = (uint8_t)(
        configs[BRICK_ENTITY_GROUP_MASTER_ID].type == TRACK_TYPE_GROUP);
    entity_topology_descriptor_t topology;
    if ((matrix == NULL) || (configs == NULL)
            || (entity_topology_resolve(group_active, target, &topology) == 0U)
            || (topology.active == 0U))
        return 0U;
    const uint8_t owner = (topology.role == ENTITY_ROLE_GROUP_CHILD)
        ? topology.parent_entity_id : target;
    for (uint8_t op = 0U; op < 2U; ++op)
    {
        if ((matrix->slew_source[op] >= MOD_MATRIX_SOURCE_COUNT)
                || !isfinite(matrix->slew_amount[op])) return 0U;
        for (uint8_t input = 0U; input < 2U; ++input)
            if (matrix->multi_source[op][input] >= MOD_MATRIX_SOURCE_COUNT)
                return 0U;
    }
    for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
    {
        const track_mod_matrix_slot_t *const state = &matrix->slot[slot];
        mod_destination_address_t destination = state->destination;
        uint8_t destination_entity;
        param_id_t destination_param;
        if ((mod_destination_address_resolve(destination, &destination_entity,
                &destination_param) != 0U) && (destination_entity == source))
            destination = mod_destination_address_make(target, destination_param);
        if ((state->source >= MOD_MATRIX_SOURCE_COUNT) || (state->enabled > 1U)
                || !isfinite(state->depth)
                || (mod_destination_catalog_address_is_supported_projected(
                        owner, destination, configs) == 0U))
            return 0U;
    }
    return 1U;
}

static uint8_t ui_track_clipboard_validate_payload_owners(
    uint8_t source, uint8_t target,
    const track_config_t configs[BRICK_ENTITY_CAPACITY],
    const ui_track_clipboard_payload_t *payload)
{
    mod_env3_control_state_t env3;
    mod_lfo_control_bank_t lfo;
    const uint8_t group_active = (uint8_t)(
        configs[BRICK_ENTITY_GROUP_MASTER_ID].type == TRACK_TYPE_GROUP);
    entity_topology_descriptor_t target_topology;
    const uint8_t expect_fm = (uint8_t)((payload->config.family == TRACK_FAMILY_SYNTH)
        && (payload->config.type == TRACK_TYPE_FM));
    if ((payload->fm_present != expect_fm)
            || ((expect_fm != 0U)
                ? (fm_control_state_validate(&payload->fm) == 0U)
                : (tone_program_control_validate(&payload->tone,
                    track_runtime_type_from_ui(payload->config.type)) == 0U))
            || (entity_topology_resolve(group_active, target,
                    &target_topology) == 0U)
            || (target_topology.active == 0U)
            || (param_filter_control_validate(&payload->filter) == 0U)
            || (vca_control_state_validate(&payload->vca) == 0U)
            || (audio_fx_control_state_validate(&payload->audio_fx) == 0U)
            || (mixer_control_state_validate(&payload->mixer) == 0U))
        return 0U;
    if (payload->modulation_present == 0U)
        return (target_topology.role == ENTITY_ROLE_GROUP_CHILD) ? 1U : 0U;
    if (target_topology.role == ENTITY_ROLE_GROUP_CHILD) return 0U;
    return (uint8_t)((mod_env3_control_prepare(&payload->env3, &env3) != 0U)
        && (mod_lfo_v1_prepare_bank(&payload->lfo, &lfo) != 0U)
        && (ui_track_clipboard_validate_matrix(
                source, target, &payload->matrix, configs) != 0U));
}

static uint8_t ui_track_clipboard_prevalidate(
    uint8_t root, const ui_track_clipboard_t *cb,
    uint8_t families[BRICK_ENTITY_CAPACITY],
    uint8_t types[BRICK_ENTITY_CAPACITY],
    uint8_t midi_channels[BRICK_ENTITY_CAPACITY],
    uint8_t midi_sources[BRICK_ENTITY_CAPACITY],
    uint8_t inputs[TRACK_COUNT])
{
    if ((cb == NULL) || (cb->valid == 0U)
            || ((cb->payload_count != 1U)
                && (cb->payload_count != UI_TRACK_CLIPBOARD_ENTITY_CAPACITY)))
        return 0U;
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        const track_config_t config = track_state_get_config(entity);
        families[entity] = (uint8_t)config.family;
        types[entity] = (uint8_t)config.type;
        midi_channels[entity] = track_state_get_midi_channel(entity);
        midi_sources[entity] = (uint8_t)track_state_get_midi_source(entity);
        if (entity < TRACK_COUNT) inputs[entity] = track_state_get_external_input(entity);
    }
    for (uint8_t index = 0U; index < cb->payload_count; ++index)
    {
        uint8_t target;
        const ui_track_clipboard_payload_t *const payload = &cb->payload[index];
        note_fx_track_state_t note_fx = payload->note_fx;
        if ((ui_track_clipboard_target_at(root, index, cb->payload_count, &target) == 0U)
                || (payload->valid == 0U)
                || (payload->config.family >= TRACK_FAMILY_COUNT)
                || (payload->config.type >= TRACK_TYPE_COUNT)
                || ((payload->config.family == TRACK_FAMILY_OFF)
                    != (payload->config.type == TRACK_TYPE_NONE))
                || ((payload->config.family != TRACK_FAMILY_OFF)
                    && (track_catalog_type_is_valid_for_family(
                            payload->config.family, payload->config.type) == false))
                || (payload->midi_channel < 1U) || (payload->midi_channel > 16U)
                || (payload->midi_source >= TRACK_MIDI_SOURCE_COUNT)
                || (payload->polyphony.voice_count < 1U)
                || (payload->polyphony.voice_count > SYNTH_POLYPHONY_MAX_VOICES)
                || !isfinite(payload->polyphony.spread)
                || (payload->audio_fx.config.filter_position >= AUDIO_FX_FILTER_POS_COUNT)
                || (payload->audio_fx.config.order >= AUDIO_FX_ORDER_COUNT)
                || (payload->audio_fx.config.spatial_mode[0] >= 4U)
                || (payload->audio_fx.config.spatial_mode[1] >= 4U)
                || (note_fx_state_normalize_track(&note_fx) == 0U)
                || (memcmp(&note_fx, &payload->note_fx, sizeof(note_fx)) != 0)
                || (ui_track_clipboard_validate_assets(payload) == 0U)
                || (ui_track_clipboard_validate_sequence(
                        target, payload->config.type, &payload->sequence) == 0U))
            return 0U;
        families[target] = (uint8_t)payload->config.family;
        types[target] = (uint8_t)payload->config.type;
        midi_channels[target] = payload->midi_channel;
        midi_sources[target] = (uint8_t)payload->midi_source;
        if (target < TRACK_COUNT) inputs[target] = payload->external_input;
    }
    track_config_t configs[BRICK_ENTITY_CAPACITY];
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        configs[entity] = (track_config_t){
            .family = (track_family_t)families[entity],
            .type = (track_type_t)types[entity]
        };
    const uint8_t group_active = (uint8_t)(
        configs[BRICK_ENTITY_GROUP_MASTER_ID].type == TRACK_TYPE_GROUP);
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        entity_topology_descriptor_t topology;
        if (entity_topology_resolve(group_active, entity, &topology) == 0U) return 0U;
        if ((topology.active != 0U) && (configs[entity].family != TRACK_FAMILY_OFF)
                && (track_catalog_type_is_available(entity,
                        configs[entity].family, configs[entity].type, configs) == false))
            return 0U;
    }
    for (uint8_t index = 0U; index < cb->payload_count; ++index)
    {
        uint8_t source, target;
        if ((ui_track_clipboard_target_at(cb->source_track, index,
                cb->payload_count, &source) == 0U)
                || (ui_track_clipboard_target_at(root, index,
                    cb->payload_count, &target) == 0U)
                || (ui_track_clipboard_validate_payload_owners(source, target,
                    configs, &cb->payload[index]) == 0U))
            return 0U;
    }
    return track_input_ownership_validate_bulk(configs, inputs);
}

static uint8_t ui_track_clipboard_restore_assets(
    uint8_t target, const ui_track_clipboard_payload_t *payload)
{
    if (project_control_track_assets_clear(target) == 0U) return 0U;
    if ((payload->config.family == TRACK_FAMILY_SAMPLER)
            && (payload->asset_count != 0U))
        return project_control_track_asset_restore(
            target, PROJECT_CONTROL_ASSET_SAMPLER, &payload->assets[0]);
    if ((payload->config.family == TRACK_FAMILY_SYNTH)
            && (payload->config.type == TRACK_TYPE_WAVE))
    {
        for (uint8_t osc = 0U; osc < payload->asset_count; ++osc)
            if (project_control_track_asset_restore(target,
                    (project_control_asset_role_t)(PROJECT_CONTROL_ASSET_WAVE_OSC1 + osc),
                    &payload->assets[osc]) == 0U)
                return 0U;
    }
    return 1U;
}

static uint8_t ui_track_clipboard_restore_modulation(
    uint8_t source, uint8_t target, const ui_track_clipboard_payload_t *payload)
{
    if (payload->modulation_present == 0U) return 1U;
    if ((mod_env3_control_restore(target, &payload->env3) == 0U)
            || (mod_lfo_v1_restore_track(target, &payload->lfo) == 0U))
        return 0U;
    for (uint8_t op = 0U; op < 2U; ++op)
    {
        for (uint8_t input = 0U; input < 2U; ++input)
            if (mod_matrix_set_multi_source(target, op, input,
                    (float)payload->matrix.multi_source[op][input]) == 0U)
                return 0U;
        if ((mod_matrix_set_slew_source(target, op,
                    (float)payload->matrix.slew_source[op]) == 0U)
                || (mod_matrix_set_slew_amount(target, op,
                    payload->matrix.slew_amount[op]) == 0U))
            return 0U;
    }
    for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
    {
        const track_mod_matrix_slot_t *const state = &payload->matrix.slot[slot];
        mod_destination_address_t destination = state->destination;
        uint8_t destination_entity;
        param_id_t destination_param;
        if ((mod_destination_address_resolve(destination, &destination_entity,
                &destination_param) != 0U) && (destination_entity == source))
            destination = mod_destination_address_make(target, destination_param);
        if (mod_matrix_set_slot_state(target, slot, state->source,
                destination, state->depth, state->enabled) == 0U)
            return 0U;
    }
    return 1U;
}

static uint8_t ui_track_clipboard_restore_sequence(
    uint8_t target, const ui_track_clipboard_sequence_t *seq)
{
    seq_runtime_begin_track_restore(&target, 1U);
    seq_play_scheduler_notify_track_pattern_change(target);
    for (uint8_t step = 0U; step < SEQ_MAX_STEPS; ++step)
    {
        seq_model_set_trig(target, step, 0U);
        seq_model_step_plock_clear(target, step);
        seq_model_play_clear_step(target, step);
    }
    uint8_t ok = 1U;
    for (uint8_t step = 0U; (step < SEQ_MAX_STEPS) && (ok != 0U); ++step)
        ok = seq_step_snapshot_apply(target, step, &seq->step[step]);
    if (ok != 0U)
    {
        seq_model_set_track_length(target, seq->length);
        seq_runtime_restore_track_div(target, seq->division);
        seq_runtime_set_track_quant(target, seq->quantization);
        seq_runtime_set_track_swing(target, seq->swing);
        ok = seq_model_play_base_restore(target, &seq->play_base);
    }
    seq_runtime_end_track_restore(&target, 1U);
    return ok;
}

static uint8_t ui_track_clipboard_restore_payload(
    uint8_t source, uint8_t target, const ui_track_clipboard_payload_t *payload)
{
    polyphony_control_state_t prepared_polyphony;
    audio_fx_control_state_t prepared_audio_fx;
    live_parameter_audio_bulk_t owner_bulk={.capture_tick=0U,
        .count=0U};
    track_runtime_resolved_track_t resolved;
    if ((track_runtime_resolve_track(target, &resolved) == 0U)
            || !polyphony_control_prepare(&payload->polyphony,&prepared_polyphony)
            || !audio_fx_control_state_prepare_for_polyphony(target,
                &payload->audio_fx,prepared_polyphony.voice_count,
                &prepared_audio_fx)
            || !polyphony_control_bulk_add(target,&prepared_polyphony,&owner_bulk)
            || !audio_fx_control_state_bulk_add_prepared(target,
                &prepared_audio_fx,&owner_bulk)) return 0U;
    if ((ui_track_clipboard_restore_assets(target, payload) == 0U)
            || (((payload->fm_present != 0U)
                ? fm_control_state_restore(target, &payload->fm)
                : tone_program_control_restore(target, &payload->tone)) == 0U)
            || ((resolved.has_filter_target != 0U)
                && (param_filter_control_restore(target, &payload->filter) == 0U))
            || (vca_control_state_restore(target, &payload->vca) == 0U)
            || !live_parameter_audio_publication_submit_bulk_now(&owner_bulk)
            || !polyphony_control_install_prepared(target,&prepared_polyphony)
            || !audio_fx_control_state_install_prepared(target,&prepared_audio_fx)
            || (mixer_control_state_restore(target, &payload->mixer) == 0U)
            || (note_fx_state_restore_track(target, &payload->note_fx) == 0U)
            || (note_fx_pipeline_configure_track(target) == 0U)
            || (ui_track_clipboard_restore_modulation(source, target, payload) == 0U)
            || (ui_track_clipboard_restore_sequence(target, &payload->sequence) == 0U))
        return 0U;
    return 1U;
}

static uint8_t ui_core_clipboard_paste_track(uint8_t track)
{
    ui_track_clipboard_t *const cb = &g_ui_clipboard.track;
    uint8_t families[BRICK_ENTITY_CAPACITY], types[BRICK_ENTITY_CAPACITY];
    uint8_t midi_channels[BRICK_ENTITY_CAPACITY], midi_sources[BRICK_ENTITY_CAPACITY];
    uint8_t inputs[TRACK_COUNT];
    if ((ui_track_clipboard_prevalidate(track, cb, families, types,
            midi_channels, midi_sources, inputs) == 0U)
            || (track_structure_apply_entity_bulk_with_inputs(families, types,
                midi_channels, midi_sources, inputs) == false))
        return 0U;
    for (uint8_t index = 0U; index < cb->payload_count; ++index)
    {
        uint8_t target, source;
        if ((ui_track_clipboard_target_at(track, index, cb->payload_count, &target) == 0U)
                || (ui_track_clipboard_target_at(cb->source_track, index,
                        cb->payload_count, &source) == 0U)
                || (ui_track_clipboard_restore_payload(
                        source, target, &cb->payload[index]) == 0U))
            return 0U;
    }
    return 1U;
}

static uint8_t ui_track_clipboard_clear_sequence(uint8_t track)
{
    seq_runtime_begin_track_restore(&track, 1U);
    seq_play_scheduler_notify_track_pattern_change(track);
    for (uint8_t step = 0U; step < SEQ_MAX_STEPS; ++step)
    {
        seq_model_set_trig(track, step, 0U);
        seq_model_set_step_roll(track, step, SEQ_STEP_ROLL_OFF);
        seq_model_step_plock_clear(track, step);
        seq_model_play_clear_step(track, step);
    }
    seq_model_set_track_length(track, SEQ_DEFAULT_LENGTH_STEPS);
    seq_runtime_restore_track_div(track, 1U);
    seq_runtime_set_track_quant(track, 0U);
    seq_runtime_set_track_swing(track, 0U);
    seq_play_snapshot_t base;
    seq_play_snapshot_init(&base);
    for (uint8_t voice = 0U; voice < SEQ_PLAY_MAX_CAPACITY; ++voice)
    {
        if ((seq_play_snapshot_set(&base, voice, SEQ_STEP_PLAY_FIELD_NOTE, 60) == 0U)
                || (seq_play_snapshot_set(&base, voice, SEQ_STEP_PLAY_FIELD_VELOCITY,
                        (voice == 0U) ? 100 : 0) == 0U)
                || (seq_play_snapshot_set(&base, voice, SEQ_STEP_PLAY_FIELD_LENGTH, 1) == 0U)
                || (seq_play_snapshot_set(&base, voice, SEQ_STEP_PLAY_FIELD_MICROTIMING, 0) == 0U))
        {
            seq_runtime_end_track_restore(&track, 1U);
            return 0U;
        }
    }
    const uint8_t ok = seq_model_play_base_restore(track, &base);
    seq_runtime_end_track_restore(&track, 1U);
    return ok;
}

static uint8_t ui_track_clipboard_clear_active_entity(uint8_t track)
{
    const track_config_t config = track_state_get_config(track);
    track_runtime_resolved_track_t resolved;
    brick_entity_id_t mod_owner = track;
    note_fx_track_state_t note_fx;
    memset(&note_fx, 0, sizeof(note_fx));
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        note_fx.value[slot][0] = 2U;
        note_fx.value[slot][2] = 1U;
        note_fx.value[slot][3] = NOTE_FX_MODEL_OFF;
    }
    const uint8_t owns_modulation = (uint8_t)((entity_topology_mod_owner(track, &mod_owner) != 0U)
        && (mod_owner == track));
    if ((track_runtime_resolve_track(track, &resolved) == 0U)
            || (project_control_track_assets_clear(track) == 0U)
            || (control_routing_clear_entity(track) == 0U)
            || (track_mute_set(track, 0U) == 0U)
            || (tone_program_control_activate(track, TRACK_RUNTIME_TYPE_NONE) == 0U)
            || ((config.family == TRACK_FAMILY_SYNTH) && (config.type == TRACK_TYPE_FM)
                && (fm_control_state_reset(track) == 0U))
            || ((resolved.has_filter_target != 0U)
                && (param_filter_control_reset(track) == 0U))
            || (vca_control_state_reset(track) == 0U)
            || ((owns_modulation != 0U) && (mod_env3_control_reset(track) == 0U))
            || ((owns_modulation != 0U) && (mod_lfo_v1_reset_track(track) == 0U))
            || (audio_fx_control_state_reset(track) == 0U)
            || (polyphony_control_reset(track) == 0U)
            || (mixer_control_state_reset(track) == 0U)
            || (note_fx_state_restore_track(track, &note_fx) == 0U)
            || (note_fx_pipeline_configure_track(track) == 0U)
            || (ui_track_clipboard_clear_sequence(track) == 0U))
        return 0U;
    if (owns_modulation != 0U)
    {
        track_sound_state_t matrix;
        track_sound_state_make_default(&matrix);
        for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
            if (mod_matrix_set_slot_state(track, slot,
                    matrix.mod_matrix[slot].source,
                    matrix.mod_matrix[slot].destination,
                    matrix.mod_matrix[slot].depth,
                    matrix.mod_matrix[slot].enabled) == 0U)
                return 0U;
        for (uint8_t op = 0U; op < 2U; ++op)
        {
            for (uint8_t input = 0U; input < 2U; ++input)
                if (mod_matrix_set_multi_source(track, op, input,
                        (float)matrix.mod_multi_source[op][input]) == 0U)
                    return 0U;
            if ((mod_matrix_set_slew_source(track, op,
                        (float)matrix.mod_slew_source[op]) == 0U)
                    || (mod_matrix_set_slew_amount(track, op,
                        matrix.mod_slew_amount[op]) == 0U))
                return 0U;
        }
        if (mod_matrix_set_selected_slot(track,
                (float)matrix.mod_matrix_selected_slot) == 0U)
            return 0U;
    }
    return 1U;
}

static uint8_t ui_core_clipboard_clear_track(uint8_t track)
{
    entity_topology_descriptor_t topology;
    uint8_t clear[UI_TRACK_CLIPBOARD_ENTITY_CAPACITY];
    uint8_t count = 1U;
    clear[0] = track;
    if (entity_topology_get(track, &topology) == 0U) return 0U;
    if (topology.role == ENTITY_ROLE_GROUP_MASTER)
    {
        count = UI_TRACK_CLIPBOARD_ENTITY_CAPACITY;
        for (uint8_t member = 0U; member < BRICK_ENTITY_GROUP_CHILD_COUNT; ++member)
            if (entity_topology_group_child(track, member, &clear[member]) == 0U)
                return 0U;
        clear[BRICK_ENTITY_GROUP_CHILD_COUNT] = track;
    }
    for (uint8_t index = 0U; index < count; ++index)
        if (ui_track_clipboard_clear_active_entity(clear[index]) == 0U)
            return 0U;

    uint8_t families[BRICK_ENTITY_CAPACITY], types[BRICK_ENTITY_CAPACITY];
    uint8_t midi_channels[BRICK_ENTITY_CAPACITY], midi_sources[BRICK_ENTITY_CAPACITY];
    uint8_t inputs[TRACK_COUNT];
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        const track_config_t config = track_state_get_config(entity);
        families[entity] = (uint8_t)config.family;
        types[entity] = (uint8_t)config.type;
        midi_channels[entity] = track_state_get_midi_channel(entity);
        midi_sources[entity] = (uint8_t)track_state_get_midi_source(entity);
        if (entity < TRACK_COUNT) inputs[entity] = track_state_get_external_input(entity);
    }
    for (uint8_t index = 0U; index < count; ++index)
    {
        const uint8_t entity = clear[index];
        families[entity] = TRACK_FAMILY_OFF;
        types[entity] = TRACK_TYPE_NONE;
        midi_channels[entity] = (uint8_t)((entity < 16U) ? entity + 1U : 16U);
        midi_sources[entity] = TRACK_MIDI_SOURCE_ALL;
        if (entity < TRACK_COUNT)
            inputs[entity] = (uint8_t)(entity % ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT);
    }
    if (track_structure_apply_entity_bulk_with_inputs(families, types,
            midi_channels, midi_sources, inputs) == false)
        return 0U;
    return 1U;
}

static uint8_t ui_core_clipboard_copy_param_scope(ui_param_clipboard_entry_t *entries,
                                                  uint8_t capacity,
                                                  uint8_t *out_valid,
                                                  uint8_t *out_count,
                                                  const param_id_t *params,
                                                  uint8_t count,
                                                  uint8_t track)
{
    if ((entries == 0) || (out_valid == 0) || (out_count == 0)
            || (params == 0) || (count == 0U) || (count > capacity))
    {
        return 0U;
    }

    *out_valid = 0U;
    *out_count = 0U;

    for (uint8_t i = 0U; i < count; ++i)
    {
        const param_id_t id = params[i];
        float value = 0.0f;
        if (id >= PARAM_COUNT)
        {
            return 0U;
        }
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
        {
            if (param_registry_query_global(id, &value) == 0U) return 0U;
        }
        else if (param_registry_get_track_value(id, track, &value) == 0U)
        {
            return 0U;
        }

        entries[i].id = id;
        entries[i].value = value;
    }

    *out_count = count;
    *out_valid = 1U;
    return 1U;
}

static uint8_t ui_core_clipboard_apply_intersection(uint8_t track,
                                                    const ui_param_clipboard_entry_t *entries,
                                                    uint8_t entry_count,
                                                    uint8_t valid,
                                                    const param_id_t *target_params,
                                                    uint8_t target_count,
                                                    uint8_t *out_common_count,
                                                    uint8_t *out_applied_count)
{
    if ((entries == 0) || (target_params == 0) || (valid == 0U)
            || (out_common_count == 0) || (out_applied_count == 0))
    {
        return 0U;
    }

    uint8_t applied = 0U;
    uint8_t common = 0U;
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = 0U,
        .count = 0U
    };
    ui_clipboard_prepared_control_t prepared[UI_ENSEMBLE_CLIPBOARD_CAPACITY];
    uint8_t prepared_count = 0U;
    audio_fx_control_prepare_context_t audio_fx_context = {0};

    param_id_t projected_ids[UI_ENSEMBLE_CLIPBOARD_CAPACITY];
    float projected_values[UI_ENSEMBLE_CLIPBOARD_CAPACITY];
    uint8_t projected_count = 0U;
    for (uint8_t i = 0U; i < target_count; ++i)
        for (uint8_t src = 0U; src < entry_count; ++src)
            if ((target_params[i] == entries[src].id)
                    && (projected_count < UI_ENSEMBLE_CLIPBOARD_CAPACITY))
            {
                projected_ids[projected_count] = target_params[i];
                projected_values[projected_count++] = entries[src].value;
                break;
            }
    if (ui_core_clipboard_audio_fx_project(track, projected_ids,
            projected_values, projected_count, &audio_fx_context) == 0U)
        return 0U;

    /* MODEL is applied before the three model-dependent values so the target
     * slot is normalized once, then receives the copied values. */
    for (uint8_t pass = 0U; pass < 6U; ++pass)
    {
        for (uint8_t i = 0U; i < target_count; ++i)
        {
            const param_id_t target = target_params[i];
            if (ui_core_clipboard_param_phase(target) != pass)
            {
                continue;
            }

            uint8_t found = 0U;
            float value = 0.0f;
            for (uint8_t src = 0U; src < entry_count; ++src)
            {
                if (entries[src].id == target)
                {
                    value = entries[src].value;
                    found = 1U;
                    break;
                }
            }

            if (found == 0U)
            {
                continue;
            }

            ++common;
            if (live_parameter_is_audio_owned(target) != 0U)
            {
                const track_runtime_param_rule_t rule = track_runtime_get_param_rule(target);
                if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
                {
                    continue;
                }
                else if (ui_core_clipboard_bulk_add(&bulk, target, track, value,
                            &audio_fx_context, prepared, &prepared_count) == 0U)
                {
                    *out_common_count = common;
                    return 0U;
                }
            }
        }
    }
    const uint8_t bulk_accepted = (bulk.count == 0U)
        ? 1U : (live_parameter_audio_publication_submit_bulk_now(&bulk) != false);
    if (bulk_accepted == 0U) return 0U;
    if (ui_core_clipboard_bulk_accept_control_values(
            prepared, prepared_count) == 0U) return 0U;
    applied = bulk.count;

    /* Structural/non-audio values are applied outside the continuous audio
     * transaction. */
    for (uint8_t pass = 0U; pass < 6U; ++pass)
    {
        for (uint8_t i = 0U; i < target_count; ++i)
        {
            const param_id_t target = target_params[i];
            uint8_t found = 0U;
            float value = 0.0f;
            for (uint8_t src = 0U; src < entry_count; ++src)
            {
                if (entries[src].id == target)
                {
                    value = entries[src].value;
                    found = 1U;
                    break;
                }
            }
            const track_runtime_param_rule_t rule = track_runtime_get_param_rule(target);
            if ((found == 0U) || ((live_parameter_is_audio_owned(target) != 0U)
                    && (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)))
                continue;
            if (ui_core_clipboard_param_phase(target) != pass)
                continue;
            if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
            {
                if (param_registry_commit_global(target, value) == 0U)
                {
                    return 0U;
                }
            }
            else if (param_registry_apply_track_value(target, track, value) == 0U)
            {
                return 0U;
            }
            ++applied;
        }
    }
    *out_common_count = common;
    *out_applied_count = applied;
    return 1U;
}

static uint8_t ui_core_clipboard_collect_track_sequence_steps(seq_track_id_t track,
                                                              seq_step_id_t *out_steps,
                                                              uint8_t max_steps,
                                                              uint8_t *out_count)
{
    if ((out_steps == 0) || (out_count == 0) || (track >= (seq_track_id_t)SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    const uint8_t capacity = seq_model_get_editable_step_capacity();
    const uint8_t limit = (capacity < max_steps) ? capacity : max_steps;

    for (uint8_t i = 0U; i < limit; ++i)
    {
        out_steps[i] = (seq_step_id_t)i;
    }

    *out_count = limit;
    return (limit > 0U) ? 1U : 0U;
}

typedef enum
{
    UI_SEQ_CLIPBOARD_SCOPE_NONE = 0,
    UI_SEQ_CLIPBOARD_SCOPE_STEP,
    UI_SEQ_CLIPBOARD_SCOPE_SEQ
} ui_seq_clipboard_scope_t;

static uint8_t ui_core_clipboard_collect_held_seq_steps(seq_track_id_t *out_track,
                                                        seq_step_id_t *out_steps,
                                                        uint8_t max_steps)
{
    return seq_edit_collect_held_steps(out_track, out_steps, max_steps, 0U);
}

static uint8_t ui_core_clipboard_resolve_seq_steps(seq_track_id_t *io_track,
                                                   seq_step_id_t *out_steps,
                                                   uint8_t max_steps,
                                                   uint8_t *out_count,
                                                   ui_seq_clipboard_scope_t *out_scope)
{
    if ((io_track == 0) || (out_steps == 0) || (out_count == 0) || (out_scope == 0))
    {
        return 0U;
    }

    *out_scope = UI_SEQ_CLIPBOARD_SCOPE_NONE;

    seq_track_id_t held_track = 0U;
    const uint8_t held_count = ui_core_clipboard_collect_held_seq_steps(&held_track, out_steps, max_steps);
    if (held_count != 0U)
    {
        *io_track = held_track;
        *out_count = held_count;
        *out_scope = UI_SEQ_CLIPBOARD_SCOPE_STEP;
        return 1U;
    }

    if (ui_core_clipboard_collect_track_sequence_steps(*io_track, out_steps, max_steps, out_count) == 0U)
    {
        return 0U;
    }

    *out_scope = UI_SEQ_CLIPBOARD_SCOPE_SEQ;
    return 1U;
}

void control_clipboard_process(const control_clipboard_intent_t *intent)
{
    if (intent == 0) return;

    switch ((control_clipboard_operation_t)intent->operation)
    {
        case CONTROL_CLIPBOARD_APPLY_MACRO_LOCK:
            (void)ui_core_clipboard_paste_macro_lock(intent->arg0, intent->arg1);
            break;
        case CONTROL_CLIPBOARD_CLEAR_MACRO_LOCK:
            (void)ui_core_clipboard_clear_macro_lock(intent->arg0, intent->arg1);
            break;
        case CONTROL_CLIPBOARD_APPLY_TRACK:
            (void)ui_core_clipboard_paste_track(intent->target);
            break;
        case CONTROL_CLIPBOARD_CLEAR_TRACK:
            (void)ui_core_clipboard_clear_track(intent->target);
            break;
        case CONTROL_CLIPBOARD_APPLY_ENSEMBLE:
        {
            uint8_t common = 0U;
            uint8_t applied = 0U;
            (void)ui_core_clipboard_apply_intersection(
                intent->target, g_ui_clipboard.ensemble.entry,
                g_ui_clipboard.ensemble.count, g_ui_clipboard.ensemble.valid,
                g_ui_clipboard.ensemble.target_params,
                g_ui_clipboard.ensemble.target_count, &common, &applied);
            break;
        }
        case CONTROL_CLIPBOARD_CLEAR_ENSEMBLE:
            (void)ui_core_clipboard_clear_param_list_to_min(
                intent->target, g_ui_clipboard.ensemble.target_params,
                g_ui_clipboard.ensemble.target_count);
            break;
        case CONTROL_CLIPBOARD_APPLY_PAGE:
        {
            uint8_t common = 0U;
            uint8_t applied = 0U;
            (void)ui_core_clipboard_apply_intersection(
                intent->target, g_ui_clipboard.page.entry,
                g_ui_clipboard.page.count, g_ui_clipboard.page.valid,
                g_ui_clipboard.page.target_params,
                g_ui_clipboard.page.target_count, &common, &applied);
            break;
        }
        case CONTROL_CLIPBOARD_CLEAR_PAGE:
            (void)ui_core_clipboard_clear_param_list_to_min(
                intent->target, g_ui_clipboard.page.target_params,
                g_ui_clipboard.page.target_count);
            break;
        case CONTROL_CLIPBOARD_APPLY_SEQUENCE:
        {
            seq_clipboard_paste_result_t result = { 0U, 0U, 0U };
            if (g_ui_clipboard.sequence_target.valid != 0U)
                (void)seq_edit_paste_steps(
                    g_ui_clipboard.sequence_target.track,
                    g_ui_clipboard.sequence_target.steps,
                    g_ui_clipboard.sequence_target.count, &result);
            break;
        }
        case CONTROL_CLIPBOARD_CLEAR_SEQUENCE:
            if (g_ui_clipboard.sequence_target.valid != 0U)
                seq_edit_clear_steps(g_ui_clipboard.sequence_target.track,
                                     g_ui_clipboard.sequence_target.steps,
                                     g_ui_clipboard.sequence_target.count);
            break;
        default:
            break;
    }
    g_clipboard_control_owned = 0U;
}

void ui_core_clipboard_init(void)
{
    memset(&g_ui_clipboard, 0, sizeof(g_ui_clipboard));
    g_clipboard_control_owned = 0U;
}

uint8_t ui_core_clipboard_handle_macro_lock_event(const ui_event_t *ev,
                                                  uint8_t shift_down,
                                                  ui_core_clipboard_feedback_fn feedback)
{
    uint8_t scene = 0U;
    uint8_t lock = 0U;

    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        return 0U;
    }

    if ((ev->id != (uint8_t)BTN_COPY) && (ev->id != (uint8_t)BTN_PASTE))
    {
        return 0U;
    }

    if (ui_core_clipboard_resolve_active_macro_lock_target(&scene, &lock) == 0U)
    {
        return 0U;
    }

    if (control_clipboard_ui_available() == 0U)
    {
        ui_core_clipboard_feedback(feedback, "CLIP BUSY");
        return 1U;
    }

    if (ev->id == (uint8_t)BTN_COPY)
    {
        if (ui_core_clipboard_copy_macro_lock(scene, lock) != 0U)
        {
            ui_core_clipboard_feedback(feedback, "MACRO COPIED");
        }
        return 1U;
    }

    if (shift_down != 0U)
    {
        if (control_clipboard_request_apply(CONTROL_CLIPBOARD_CLEAR_MACRO_LOCK,
                                            0U, scene, lock) != 0U)
            ui_core_clipboard_feedback(feedback, "MACRO QUEUED");
        else ui_core_clipboard_feedback(feedback, "CLIP BUSY");
        return 1U;
    }

    if (control_clipboard_request_apply(CONTROL_CLIPBOARD_APPLY_MACRO_LOCK,
                                        0U, scene, lock) != 0U)
        ui_core_clipboard_feedback(feedback, "MACRO QUEUED");
    else ui_core_clipboard_feedback(feedback, "CLIP BUSY");
    return 1U;
}

uint8_t ui_core_clipboard_handle_track_event(const ui_event_t *ev,
                                             uint8_t track_select_armed,
                                             uint8_t shift_down,
                                             ui_core_clipboard_feedback_fn feedback)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        return 0U;
    }

    if ((track_select_armed == 0U)
        || ((ev->id != (uint8_t)BTN_COPY) && (ev->id != (uint8_t)BTN_PASTE)))
    {
        return 0U;
    }

    const uint8_t track = ui_get_active_lane();
    if (control_clipboard_ui_available() == 0U)
    {
        ui_core_clipboard_feedback(feedback, "CLIP BUSY");
        return 1U;
    }
    if (ev->id == (uint8_t)BTN_COPY)
    {
        if (ui_core_clipboard_copy_track(track) != 0U)
        {
            ui_core_clipboard_feedback(feedback, "TRACK COPIED");
        }
        return 1U;
    }

    if (shift_down != 0U)
    {
        if (control_clipboard_request_apply(CONTROL_CLIPBOARD_CLEAR_TRACK,
                                            track, 0U, 0U) != 0U)
            ui_core_clipboard_feedback(feedback, "TRACK QUEUED");
        else ui_core_clipboard_feedback(feedback, "CLIP BUSY");
        return 1U;
    }

    if (control_clipboard_request_apply(CONTROL_CLIPBOARD_APPLY_TRACK,
                                        track, 0U, 0U) != 0U)
        ui_core_clipboard_feedback(feedback, "TRACK QUEUED");
    else ui_core_clipboard_feedback(feedback, "CLIP BUSY");
    return 1U;
}

uint8_t ui_core_clipboard_handle_ensemble_event(const ui_event_t *ev,
                                                uint8_t shift_down,
                                                ui_core_clipboard_feedback_fn feedback)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        return 0U;
    }

    if ((ev->id != (uint8_t)BTN_COPY) && (ev->id != (uint8_t)BTN_PASTE))
    {
        return 0U;
    }

    ui_template_family_id_t family_id = UI_TEMPLATE_FAMILY_COUNT;
    button_id_t held_button = BTN_COUNT;
    if (ui_core_clipboard_midi_fx_shortcut_is_held() != 0U)
    {
        family_id = UI_TEMPLATE_FAMILY_FX;
    }
    else if ((ui_core_clipboard_get_held_param_button(&held_button) == 0U)
            || (ui_core_clipboard_resolve_template_family_from_button(held_button, &family_id) == 0U))
    {
        return 0U;
    }

    param_id_t params[UI_ENSEMBLE_CLIPBOARD_CAPACITY];
    uint8_t count = 0U;
    if (ui_core_clipboard_collect_ensemble_params(family_id, params, &count) == 0U)
    {
        ui_core_clipboard_feedback(feedback, "ENS N/A");
        return 1U;
    }

    uint8_t track = ui_get_active_lane();
    if (ui_template_family_resolve_owner_track(family_id, track, &track) == 0U)
    {
        ui_core_clipboard_feedback(feedback, "ENS N/A");
        return 1U;
    }
    if (control_clipboard_ui_available() == 0U)
    {
        ui_core_clipboard_feedback(feedback, "CLIP BUSY");
        return 1U;
    }
    if (ev->id == (uint8_t)BTN_COPY)
    {
        if (ui_core_clipboard_copy_param_scope(g_ui_clipboard.ensemble.entry,
                                               UI_ENSEMBLE_CLIPBOARD_CAPACITY,
                                               &g_ui_clipboard.ensemble.valid,
                                               &g_ui_clipboard.ensemble.count,
                                               params,
                                               count,
                                               track) != 0U)
        {
            ui_core_clipboard_feedback(feedback, "ENS COPIED");
        }
        return 1U;
    }

    if (shift_down != 0U)
    {
        g_ui_clipboard.ensemble.target_count = count;
        memcpy(g_ui_clipboard.ensemble.target_params, params,
               (size_t)count * sizeof(params[0]));
        if (control_clipboard_request_apply(CONTROL_CLIPBOARD_CLEAR_ENSEMBLE,
                                            track, 0U, 0U) != 0U)
            ui_core_clipboard_feedback(feedback, "ENS QUEUED");
        else ui_core_clipboard_feedback(feedback, "CLIP BUSY");
        return 1U;
    }

    g_ui_clipboard.ensemble.target_count = count;
    memcpy(g_ui_clipboard.ensemble.target_params, params,
           (size_t)count * sizeof(params[0]));
    if (control_clipboard_request_apply(CONTROL_CLIPBOARD_APPLY_ENSEMBLE,
                                        track, 0U, 0U) != 0U)
        ui_core_clipboard_feedback(feedback, "ENS QUEUED");
    else ui_core_clipboard_feedback(feedback, "CLIP BUSY");
    return 1U;
}

uint8_t ui_core_clipboard_handle_page_event(const ui_event_t *ev,
                                            uint8_t shift_down,
                                            ui_core_clipboard_feedback_fn feedback)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        return 0U;
    }

    if ((ev->id != (uint8_t)BTN_COPY) && (ev->id != (uint8_t)BTN_PASTE))
    {
        return 0U;
    }

    if ((ui_page_get_id() != UI_PAGE_MIDI_FX)
            && (ui_core_clipboard_is_active_page_button_held() == 0U))
    {
        return 0U;
    }

    param_id_t params[UI_PAGE_CLIPBOARD_CAPACITY];
    uint8_t count = 0U;
    if (ui_core_clipboard_collect_active_page_params(params, &count) == 0U)
    {
        return 0U;
    }

    ui_template_edit_context_t edit_context;
    if (ui_template_edit_context_resolve_active(&edit_context) == 0U)
    {
        return 0U;
    }
    const uint8_t track = edit_context.owner_entity;
    if (control_clipboard_ui_available() == 0U)
    {
        ui_core_clipboard_feedback(feedback, "CLIP BUSY");
        return 1U;
    }
    if (ev->id == (uint8_t)BTN_COPY)
    {
        if (ui_core_clipboard_copy_param_scope(g_ui_clipboard.page.entry,
                                               UI_PAGE_CLIPBOARD_CAPACITY,
                                               &g_ui_clipboard.page.valid,
                                               &g_ui_clipboard.page.count,
                                               params,
                                               count,
                                               track) != 0U)
        {
            ui_core_clipboard_feedback(feedback, "PAGE COPIED");
        }
        return 1U;
    }

    if (shift_down != 0U)
    {
        g_ui_clipboard.page.target_count = count;
        memcpy(g_ui_clipboard.page.target_params, params,
               (size_t)count * sizeof(params[0]));
        if (control_clipboard_request_apply(CONTROL_CLIPBOARD_CLEAR_PAGE,
                                            track, 0U, 0U) != 0U)
            ui_core_clipboard_feedback(feedback, "PAGE QUEUED");
        else ui_core_clipboard_feedback(feedback, "CLIP BUSY");
        return 1U;
    }

    g_ui_clipboard.page.target_count = count;
    memcpy(g_ui_clipboard.page.target_params, params,
           (size_t)count * sizeof(params[0]));
    if (control_clipboard_request_apply(CONTROL_CLIPBOARD_APPLY_PAGE,
                                        track, 0U, 0U) != 0U)
        ui_core_clipboard_feedback(feedback, "PAGE QUEUED");
    else ui_core_clipboard_feedback(feedback, "CLIP BUSY");
    return 1U;
}

uint8_t ui_core_clipboard_handle_seq_track_event(const ui_event_t *ev,
                                                 uint8_t track_select_armed,
                                                 uint8_t shift_down,
                                                 ui_core_clipboard_feedback_fn feedback)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        return 0U;
    }

    if ((ev->id != (uint8_t)BTN_COPY) && (ev->id != (uint8_t)BTN_PASTE))
    {
        return 0U;
    }

    if (ui_hall_is_seq_context(ui_get_hall_mode()) == 0U)
    {
        return 0U;
    }

    if (track_select_armed != 0U)
    {
        return 0U;
    }

    button_id_t held_param_button = BTN_COUNT;
    if (ui_core_clipboard_get_held_param_button(&held_param_button) != 0U)
    {
        return 0U;
    }

    if (ui_core_clipboard_is_active_page_button_held() != 0U)
    {
        return 0U;
    }

    seq_track_id_t track = (seq_track_id_t)ui_get_active_lane();
    seq_step_id_t steps[SEQ_MAX_STEPS];
    uint8_t step_count = 0U;
    ui_seq_clipboard_scope_t scope = UI_SEQ_CLIPBOARD_SCOPE_NONE;
    if (ui_core_clipboard_resolve_seq_steps(&track, steps, (uint8_t)SEQ_MAX_STEPS, &step_count, &scope) == 0U)
    {
        return 1U;
    }

    const uint8_t step_scope = (scope == UI_SEQ_CLIPBOARD_SCOPE_STEP) ? 1U : 0U;

    if (control_clipboard_ui_available() == 0U)
    {
        ui_core_clipboard_feedback(feedback, "CLIP BUSY");
        return 1U;
    }

    if (ev->id == (uint8_t)BTN_COPY)
    {
        if (shift_down != 0U)
        {
            g_ui_clipboard.sequence_target.valid = 1U;
            g_ui_clipboard.sequence_target.track = track;
            g_ui_clipboard.sequence_target.count = step_count;
            memcpy(g_ui_clipboard.sequence_target.steps, steps,
                   (size_t)step_count * sizeof(steps[0]));
            if (control_clipboard_request_apply(CONTROL_CLIPBOARD_CLEAR_SEQUENCE,
                                                (uint8_t)track, 0U, 0U) != 0U)
                ui_core_clipboard_feedback(feedback,
                                           (step_scope != 0U)
                                               ? "STEP QUEUED" : "SEQ QUEUED");
            else
                ui_core_clipboard_feedback(feedback, "CLIP BUSY");
            return 1U;
        }

        if (seq_edit_copy_steps(track, steps, step_count) != 0U)
        {
            ui_core_clipboard_feedback(feedback, (step_scope != 0U) ? "STEP COPIED" : "SEQ COPIED");
        }
        return 1U;
    }

    g_ui_clipboard.sequence_target.valid = 1U;
    g_ui_clipboard.sequence_target.track = track;
    g_ui_clipboard.sequence_target.count = step_count;
    memcpy(g_ui_clipboard.sequence_target.steps, steps,
           (size_t)step_count * sizeof(steps[0]));
    const control_clipboard_operation_t operation =
        (shift_down != 0U) ? CONTROL_CLIPBOARD_CLEAR_SEQUENCE
                           : CONTROL_CLIPBOARD_APPLY_SEQUENCE;
    if (control_clipboard_request_apply(operation, (uint8_t)track, 0U, 0U) != 0U)
        ui_core_clipboard_feedback(feedback,
                                   (step_scope != 0U)
                                       ? "STEP QUEUED" : "SEQ QUEUED");
    else
        ui_core_clipboard_feedback(feedback, "CLIP BUSY");

    return 1U;
}
