#include "ui_core_clipboard.h"
#include "App/Hall/hall_surface.h"
#include "Board/board_product.h"

#include <string.h>

#include "buttons.h"
#include "ui_core.h"
#include "ui_page_manager.h"
#include "ui_template_page.h"
#include "ui_edit_context_sync.h"
#include "ui_macro_interaction.h"
#include "Core/project_control.h"
#include "Storage/memory_layout.h"
#include "Core/engine_tasklet.h"
#include "Core/track_snapshot.h"
#include "Core/live_clock.h"
#include "Core/live_parameter_audio_publication.h"
#include "Core/live_parameter_migration.h"
#include "NoteFx/note_fx_state.h"
#include "param_registry.h"
#include "Core/track_runtime.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_model.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_clipboard.h"

typedef enum
{
    UI_CLIPBOARD_SCOPE_NONE = 0,
    UI_CLIPBOARD_SCOPE_TRACK,
    UI_CLIPBOARD_SCOPE_ENSEMBLE,
    UI_CLIPBOARD_SCOPE_PAGE
} ui_clipboard_scope_t;

typedef struct
{
    uint8_t valid;
    ui_clipboard_scope_t scope;
    ui_track_family_t source_family;
    ui_track_type_t source_type;
    uint8_t param_count;
    param_id_t params[PARAM_COUNT];
    float values[PARAM_COUNT];
} ui_param_clipboard_t;

typedef struct
{
    uint8_t valid;
    uint8_t source_track;
    uint8_t snapshot_count;
    track_snapshot_t snapshot[1U + BRICK_ENTITY_GROUP_CHILD_COUNT];
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
    ui_param_clipboard_t ensemble;
    ui_param_clipboard_t page;
} ui_clipboard_state_t;

UI_SDRAM static ui_clipboard_state_t g_ui_clipboard;

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
    for (uint8_t order = 0U; order < 14U; ++order)
    {
        if (param_registry_get_audio_fx_param(order) == id)
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
        if ((id >= PARAM_COUNT) || (id == PARAM_CFG_TRACK) || (id == PARAM_CFG_TRACK_TYPE)
                || (id == PARAM_CFG_MIDI_CH) || (id == PARAM_CFG_MIDI_SRC)
                )
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

        if (count >= PARAM_COUNT)
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
    const board_product_capabilities_t *const caps = board_product_capabilities();
    const uint8_t hall = ((caps != 0) && (caps->has_separate_hall_keyboard != 0U)) ? 6U : 9U;
    return hall_surface_is_pressed(hall);
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
            if (ui_core_clipboard_collect_params_from_subpage(&family->subpages[sp], out_ids, &count) == 0U)
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
    if (ui_core_clipboard_collect_params_from_subpage(subpage, out_ids, &count) == 0U)
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
                                          float value)
{
    if ((bulk == 0) || (bulk->count >= LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS)
            || (id >= PARAM_COUNT))
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    const uint8_t scope = (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
        ? LIVE_PARAMETER_EVENT_SCOPE_GLOBAL : LIVE_PARAMETER_EVENT_SCOPE_TRACK;
    const uint8_t event_track = (scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK) ? track : 0U;
    float command_value = value;
    if ((scope == LIVE_PARAMETER_EVENT_SCOPE_GLOBAL)
            && (param_registry_prepare_global_audio_command(
                id, value, &command_value) == 0U))
    {
        return 0U;
    }
    live_parameter_audio_bulk_item_t *const item = &bulk->item[bulk->count++];
    item->parameter_id = (uint16_t)id;
    item->scope = scope;
    item->track = event_track;
    item->slot = LIVE_PARAMETER_EVENT_INVALID_INDEX;
    item->flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                             | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS);
    item->value = live_parameter_event_encode_float(command_value);
    return 1U;
}

static void ui_core_clipboard_bulk_accept_control_values(const live_parameter_audio_bulk_t *bulk)
{
    if (bulk == 0)
    {
        return;
    }

    for (uint8_t i = 0U; i < bulk->count; ++i)
    {
        const live_parameter_audio_bulk_item_t *const item = &bulk->item[i];
        const param_id_t id = (param_id_t)item->parameter_id;
        const float value = live_parameter_event_decode_float(item->value);
        (void)ui_param_accept_audio_owned_command(id,
                                                  item->scope,
                                                  item->track,
                                                  value);
    }
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
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
        .count = 0U
    };
    uint8_t direct_applied = 0U;
    param_registry_batch_begin();
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
                        param_set(id, param_registry[id].min);
                        direct_applied = 1U;
                    }
                    else if (ui_core_clipboard_bulk_add(&bulk, id, track,
                                                        param_registry[id].min) == 0U)
                    {
                        param_registry_batch_end();
                        return 0U;
                    }
                }
                else if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
                {
                    param_set(id, param_registry[id].min);
                    direct_applied = 1U;
                }
                else
                {
                    direct_applied = (uint8_t)(direct_applied
                        | (param_registry_apply_track_value(id, track,
                                                             param_registry[id].min) != 0U));
                }
            }
        }
    }
    param_registry_batch_end();
    if ((bulk.count != 0U)
            && (live_parameter_audio_publication_submit_bulk(&bulk) == false))
    {
        return direct_applied;
    }
    ui_core_clipboard_bulk_accept_control_values(&bulk);
    return (uint8_t)((direct_applied != 0U) || (bulk.count != 0U));
}

static uint8_t ui_core_clipboard_copy_track(uint8_t track)
{
    ui_track_clipboard_t *const cb = &g_ui_clipboard.track;
    memset(cb, 0, sizeof(*cb));

    cb->source_track = track;
    entity_topology_descriptor_t topology;
    if ((entity_topology_get(track, &topology) == 0U)
            || (track_snapshot_capture(track, &cb->snapshot[0]) == 0U))
    {
        return 0U;
    }

    cb->snapshot_count = 1U;
    if (topology.role == ENTITY_ROLE_GROUP_MASTER)
    {
        for (uint8_t member = 0U; member < BRICK_ENTITY_GROUP_CHILD_COUNT; ++member)
        {
            brick_entity_id_t child = BRICK_ENTITY_INVALID_ID;
            if ((entity_topology_group_child(topology.entity_id, member, &child) == 0U)
                    || (track_snapshot_capture(child, &cb->snapshot[1U + member]) == 0U))
            {
                memset(cb, 0, sizeof(*cb));
                return 0U;
            }
        }
        cb->snapshot_count = 1U + BRICK_ENTITY_GROUP_CHILD_COUNT;
    }

    cb->valid = 1U;
    return 1U;
}

static uint8_t ui_core_clipboard_clear_track(uint8_t track)
{
    entity_topology_descriptor_t topology;
    if (entity_topology_get(track, &topology) == 0U)
    {
        return 0U;
    }
    track_snapshot_t snapshot;
    if (track_snapshot_make_default(track, &snapshot) == 0U)
    {
        return 0U;
    }
    if (topology.role != ENTITY_ROLE_GROUP_MASTER)
    {
        return track_snapshot_apply(track, &snapshot);
    }

    for (uint8_t member = 0U; member < BRICK_ENTITY_GROUP_CHILD_COUNT; ++member)
    {
        brick_entity_id_t child = BRICK_ENTITY_INVALID_ID;
        if ((entity_topology_group_child(topology.entity_id, member, &child) == 0U)
                || (track_snapshot_make_default(child, &snapshot) == 0U)
                || (track_snapshot_apply(child, &snapshot) == 0U))
        {
            return 0U;
        }
    }
    return track_snapshot_make_default(track, &snapshot)
            && track_snapshot_apply(track, &snapshot);
}

static uint8_t ui_core_clipboard_paste_track(uint8_t track)
{
    ui_track_clipboard_t *const cb = &g_ui_clipboard.track;
    if (cb->valid == 0U)
    {
        return 0U;
    }

    const uint8_t source_track = cb->source_track;

    const track_snapshot_apply_options_t options = {
        .has_family_override = 0U,
        .family_override = cb->snapshot[0].config.family,
        .clear_source_track = 0U,
        .source_track = source_track
    };
    if (track_snapshot_apply_ex(track, &cb->snapshot[0], &options) == 0U)
    {
        return 0U;
    }

    if (cb->snapshot_count > 1U)
    {
        entity_topology_descriptor_t topology;
        if ((entity_topology_get(track, &topology) == 0U)
                || (topology.role != ENTITY_ROLE_GROUP_MASTER))
        {
            return 0U;
        }
        for (uint8_t member = 0U; member < BRICK_ENTITY_GROUP_CHILD_COUNT; ++member)
        {
            brick_entity_id_t child = BRICK_ENTITY_INVALID_ID;
            if ((entity_topology_group_child(topology.entity_id, member, &child) == 0U)
                    || (track_snapshot_apply(child, &cb->snapshot[1U + member]) == 0U))
            {
                return 0U;
            }
        }
    }

    ui_edit_context_sync_active_track(0U);
    return 1U;
}

static uint8_t ui_core_clipboard_copy_param_scope(ui_param_clipboard_t *clipboard,
                                                  ui_clipboard_scope_t scope,
                                                  const param_id_t *params,
                                                  uint8_t count,
                                                  uint8_t track)
{
    if ((clipboard == 0) || (params == 0) || (count == 0U))
    {
        return 0U;
    }

    memset(clipboard, 0, sizeof(*clipboard));
    clipboard->scope = scope;
    clipboard->source_family = ui_get_track_family(track);
    clipboard->source_type = ui_get_track_type(track);

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
            value = param_get(id);
        }
        else if (param_registry_get_track_value(id, track, &value) == 0U)
        {
            return 0U;
        }

        clipboard->params[i] = id;
        clipboard->values[i] = value;
    }

    clipboard->param_count = count;
    clipboard->valid = 1U;
    return 1U;
}

static uint8_t ui_core_clipboard_apply_intersection(uint8_t track,
                                                    const ui_param_clipboard_t *clipboard,
                                                    const param_id_t *target_params,
                                                    uint8_t target_count,
                                                    uint8_t *out_common_count)
{
    if ((clipboard == 0) || (target_params == 0) || (clipboard->valid == 0U) || (out_common_count == 0))
    {
        return 0U;
    }

    uint8_t applied = 0U;
    uint8_t common = 0U;
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
        .count = 0U
    };

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
            for (uint8_t src = 0U; src < clipboard->param_count; ++src)
            {
                if (clipboard->params[src] == target)
                {
                    value = clipboard->values[src];
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
                    param_set(target, value);
                }
                else if (ui_core_clipboard_bulk_add(&bulk, target, track, value) == 0U)
                {
                    *out_common_count = common;
                    return 0U;
                }
            }
        }
    }
    const uint8_t bulk_accepted = (bulk.count == 0U)
        ? 1U : (live_parameter_audio_publication_submit_bulk(&bulk) != false);
    if (bulk_accepted != 0U)
    {
        ui_core_clipboard_bulk_accept_control_values(&bulk);
        applied = (uint8_t)(applied + bulk.count);
    }

    /* Structural/non-audio values keep their existing transition contract and
     * are deliberately applied outside the continuous audio transaction. */
    param_registry_batch_begin();
    for (uint8_t pass = 0U; pass < 6U; ++pass)
    {
        for (uint8_t i = 0U; i < target_count; ++i)
        {
            const param_id_t target = target_params[i];
            uint8_t found = 0U;
            float value = 0.0f;
            for (uint8_t src = 0U; src < clipboard->param_count; ++src)
            {
                if (clipboard->params[src] == target)
                {
                    value = clipboard->values[src];
                    found = 1U;
                    break;
                }
            }
            if ((found == 0U) || (live_parameter_is_audio_owned(target) != 0U))
                continue;
            if (ui_core_clipboard_param_phase(target) != pass)
                continue;
            const track_runtime_param_rule_t rule = track_runtime_get_param_rule(target);
            if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
            {
                param_set(target, value);
                ++applied;
            }
            else if (param_registry_apply_track_value(target, track, value) != 0U)
            {
                ++applied;
            }
        }
    }
    param_registry_batch_end();
    *out_common_count = common;

    return applied;
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

void ui_core_clipboard_init(void)
{
    memset(&g_ui_clipboard, 0, sizeof(g_ui_clipboard));
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
        if (ui_core_clipboard_clear_macro_lock(scene, lock) != 0U)
        {
            ui_core_clipboard_feedback(feedback, "MACRO CLEARED");
        }
        return 1U;
    }

    if (ui_core_clipboard_paste_macro_lock(scene, lock) != 0U)
    {
        ui_core_clipboard_feedback(feedback, "MACRO PASTED");
    }
    else
    {
        ui_core_clipboard_feedback(feedback, "MACRO INCOMP");
    }
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
        if (ui_core_clipboard_clear_track(track) != 0U)
        {
            ui_core_clipboard_feedback(feedback, "TRACK CLEARED");
        }
        return 1U;
    }

    if (ui_core_clipboard_paste_track(track) != 0U)
    {
        ui_core_clipboard_feedback(feedback,
            (track_snapshot_last_voice_limited() != 0U) ? "VOICE LIMITED" : "TRACK PASTED");
    }
    else
    {
        ui_core_clipboard_feedback(feedback,
            (track_snapshot_last_voice_max() != 0U) ? "VOICE MAX" : "TRACK INCOMP");
    }
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

    param_id_t params[PARAM_COUNT];
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
    if (ev->id == (uint8_t)BTN_COPY)
    {
        if (ui_core_clipboard_copy_param_scope(&g_ui_clipboard.ensemble,
                                               UI_CLIPBOARD_SCOPE_ENSEMBLE,
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
        const uint8_t cleared = ui_core_clipboard_clear_param_list_to_min(track, params, count);
        ui_edit_context_sync_active_track(0U);
        ui_core_clipboard_feedback(feedback, (cleared != 0U) ? "ENS CLEARED" : "ENS INCOMP");
        return 1U;
    }

    uint8_t common_count = 0U;
    const uint8_t applied = ui_core_clipboard_apply_intersection(track,
                                                                 &g_ui_clipboard.ensemble,
                                                                 params,
                                                                 count,
                                                                 &common_count);
    if ((common_count == 0U) || (applied == 0U))
    {
        ui_core_clipboard_feedback(feedback, "ENS INCOMP");
        return 1U;
    }

    ui_edit_context_sync_active_track(0U);
    if ((applied < common_count) || (common_count < g_ui_clipboard.ensemble.param_count))
    {
        ui_core_clipboard_feedback(feedback, "ENS PARTIAL");
    }
    else
    {
        ui_core_clipboard_feedback(feedback, "ENS PASTED");
    }
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

    param_id_t params[PARAM_COUNT];
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
    if (ev->id == (uint8_t)BTN_COPY)
    {
        if (ui_core_clipboard_copy_param_scope(&g_ui_clipboard.page,
                                               UI_CLIPBOARD_SCOPE_PAGE,
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
        const uint8_t cleared = ui_core_clipboard_clear_param_list_to_min(track, params, count);
        ui_edit_context_sync_active_track(0U);
        ui_core_clipboard_feedback(feedback, (cleared != 0U) ? "PAGE CLEARED" : "PAGE INCOMP");
        return 1U;
    }

    uint8_t common_count = 0U;
    const uint8_t applied = ui_core_clipboard_apply_intersection(track,
                                                                 &g_ui_clipboard.page,
                                                                 params,
                                                                 count,
                                                                 &common_count);
    if ((common_count == 0U) || (applied == 0U))
    {
        ui_core_clipboard_feedback(feedback, "PAGE INCOMP");
        return 1U;
    }

    ui_edit_context_sync_active_track(0U);
    if ((applied < common_count) || (common_count < g_ui_clipboard.page.param_count))
    {
        ui_core_clipboard_feedback(feedback, "PAGE PARTIAL");
    }
    else
    {
        ui_core_clipboard_feedback(feedback, "PAGE PASTED");
    }
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

    if (ev->id == (uint8_t)BTN_COPY)
    {
        if (shift_down != 0U)
        {
            seq_edit_clear_steps(track, steps, step_count);
            ui_core_clipboard_feedback(feedback, (step_scope != 0U) ? "STEP CLEARED" : "SEQ CLEARED");
            return 1U;
        }

        if (seq_edit_copy_steps(track, steps, step_count) != 0U)
        {
            ui_core_clipboard_feedback(feedback, (step_scope != 0U) ? "STEP COPIED" : "SEQ COPIED");
        }
        return 1U;
    }

    seq_clipboard_paste_result_t paste_result = { 0U, 0U, 0U };
    if (seq_edit_paste_steps(track, steps, step_count, &paste_result) == 0U)
    {
        ui_core_clipboard_feedback(feedback, (step_scope != 0U) ? "STEP INCOMP" : "SEQ INCOMP");
        return 1U;
    }

    if (paste_result.trunc != 0U)
    {
        ui_core_clipboard_feedback(feedback, (step_scope != 0U) ? "STEP TRUNC" : "SEQ TRUNC");
    }
    else if (paste_result.partial != 0U)
    {
        ui_core_clipboard_feedback(feedback, (step_scope != 0U) ? "STEP PARTIAL" : "SEQ PARTIAL");
    }
    else
    {
        ui_core_clipboard_feedback(feedback, (step_scope != 0U) ? "STEP PASTED" : "SEQ PASTED");
    }

    return 1U;
}
