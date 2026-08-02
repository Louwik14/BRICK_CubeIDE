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
#include "Storage/project_v1.h"
#include "Storage/memory_layout.h"
#include "Core/engine_tasklet.h"
#include "Core/track_snapshot.h"
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
    track_snapshot_t snapshot;
} ui_track_clipboard_t;

typedef struct
{
    uint8_t valid;
    project_v1_macro_lock_t lock;
} ui_macro_lock_clipboard_t;

typedef struct
{
    ui_macro_lock_clipboard_t macro_lock;
    ui_track_clipboard_t track;
    ui_param_clipboard_t ensemble;
    ui_param_clipboard_t page;
} ui_clipboard_state_t;

UI_SDRAM static ui_clipboard_state_t g_ui_clipboard;

static void ui_core_clipboard_feedback(ui_core_clipboard_feedback_fn feedback, const char *message)
{
    if (feedback != 0)
    {
        feedback(message);
    }
}

static uint8_t ui_core_clipboard_macro_make_empty_lock(project_v1_macro_lock_t *out_lock)
{
    if (out_lock == 0)
    {
        return 0U;
    }

    out_lock->track = PROJECT_V1_MACRO_LOCK_TRACK_NONE;
    out_lock->param = PROJECT_V1_MACRO_LOCK_PARAM_NONE;
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
    project_v1_macro_lock_t current;

    if (project_v1_macro_get_scene_lock(scene, lock, &current) == 0U)
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

    return project_v1_macro_set_scene_lock(scene, lock, &g_ui_clipboard.macro_lock.lock);
}

static uint8_t ui_core_clipboard_clear_macro_lock(uint8_t scene, uint8_t lock)
{
    project_v1_macro_lock_t empty_lock;
    if (ui_core_clipboard_macro_make_empty_lock(&empty_lock) == 0U)
    {
        return 0U;
    }

    return project_v1_macro_set_scene_lock(scene, lock, &empty_lock);
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
    const uint8_t active_track = ui_get_active_track();
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

static void ui_core_clipboard_clear_param_list_to_min(uint8_t track,
                                                      const param_id_t *params,
                                                      uint8_t count)
{
    if (params == 0)
    {
        return;
    }

    /* Consumer-edge refresh: clear-to-min applies on a refreshed projection. */
    track_runtime_refresh_track(track);
    param_registry_batch_begin();
    for (uint8_t i = 0U; i < count; ++i)
    {
        const param_id_t id = params[i];
        if (id < PARAM_COUNT)
        {
            const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
            if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
            {
                param_set(id, param_registry[id].min);
            }
            else
            {
                (void)param_registry_apply_track_value(id, track, param_registry[id].min);
            }
        }
    }
    param_registry_batch_end();
}

static uint8_t ui_core_clipboard_copy_track(uint8_t track)
{
    ui_track_clipboard_t *const cb = &g_ui_clipboard.track;
    memset(cb, 0, sizeof(*cb));

    cb->source_track = track;
    track_runtime_refresh_track(track);
    if (track_snapshot_capture(track, &cb->snapshot) == 0U)
    {
        return 0U;
    }

    cb->valid = 1U;
    return 1U;
}

static uint8_t ui_core_clipboard_clear_track(uint8_t track)
{
    track_snapshot_t snapshot;
    if (track_snapshot_make_default(track, &snapshot) == 0U)
    {
        return 0U;
    }
    return track_snapshot_apply(track, &snapshot);
}

static uint8_t ui_core_clipboard_track_is_input_exclusive(const ui_track_clipboard_t *cb)
{
    if (cb == 0)
    {
        return 0U;
    }

    return (uint8_t)ui_track_family_is_input(cb->snapshot.config.family);
}

static ui_track_family_t ui_core_clipboard_find_free_input_family(void)
{
    for (ui_track_family_t family = UI_TRACK_FAMILY_INPUT1; family <= UI_TRACK_FAMILY_INPUT3; ++family)
    {
        if (ui_track_family_is_input(family)
                && (ui_count_tracks_with_family(family) == 0U))
        {
            return family;
        }
    }

    return UI_TRACK_FAMILY_COUNT;
}

static uint8_t ui_core_clipboard_paste_track(uint8_t track)
{
    ui_track_clipboard_t *const cb = &g_ui_clipboard.track;
    if (cb->valid == 0U)
    {
        return 0U;
    }

    const uint8_t source_track = cb->source_track;
    const uint8_t source_track_valid = (source_track < UI_TRACK_COUNT) ? 1U : 0U;
    const uint8_t source_equals_target = (uint8_t)((source_track_valid != 0U) && (source_track == track));

    ui_track_family_t target_family = cb->snapshot.config.family;
    uint8_t clear_source_after_success = 0U;

    if ((source_equals_target == 0U)
            && (source_track_valid != 0U)
            && (ui_core_clipboard_track_is_input_exclusive(cb) != 0U))
    {
        const ui_track_family_t free_input = ui_core_clipboard_find_free_input_family();
        if (free_input != UI_TRACK_FAMILY_COUNT)
        {
            target_family = free_input;
        }
        else
        {
            clear_source_after_success = 1U;
        }
    }

    const track_snapshot_apply_options_t options = {
        .has_family_override = (target_family != cb->snapshot.config.family) ? 1U : 0U,
        .family_override = target_family,
        .clear_source_track = clear_source_after_success,
        .source_track = source_track
    };
    if (track_snapshot_apply_ex(track, &cb->snapshot, &options) == 0U)
    {
        return 0U;
    }

    if ((clear_source_after_success != 0U) && (source_equals_target == 0U))
    {
        if (ui_core_clipboard_clear_track(source_track) == 0U)
        {
            return 0U;
        }

        cb->source_track = track;
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
    /* Consumer-edge refresh: intersection apply uses a refreshed projection. */
    track_runtime_refresh_track(track);
    param_registry_batch_begin();

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

        if (found == 0U)
        {
            continue;
        }

        ++common;
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

    const uint8_t track = ui_get_active_track();
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
        family_id = UI_TEMPLATE_FAMILY_MIDI_FX;
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

    const uint8_t track = ui_get_active_track();
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
        ui_core_clipboard_clear_param_list_to_min(track, params, count);
        ui_edit_context_sync_active_track(0U);
        ui_core_clipboard_feedback(feedback, "ENS CLEARED");
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

    const uint8_t track = ui_get_active_track();
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
        ui_core_clipboard_clear_param_list_to_min(track, params, count);
        ui_edit_context_sync_active_track(0U);
        ui_core_clipboard_feedback(feedback, "PAGE CLEARED");
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

    seq_track_id_t track = (seq_track_id_t)ui_get_active_track();
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
