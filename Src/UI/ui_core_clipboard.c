#include "ui_core_clipboard.h"

#include <string.h>

#include "buttons.h"
#include "ui_core.h"
#include "ui_page_manager.h"
#include "ui_template_page.h"
#include "ui_edit_context_sync.h"
#include "Storage/undo_v1.h"
#include "Core/engine_tasklet.h"
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
    ui_track_config_t config;
    uint8_t midi_channel;
    ui_track_midi_source_t midi_source;
    uint8_t seq_page;
    uint8_t seq_length;
    uint8_t seq_div;
    uint8_t seq_quant;
    uint8_t seq_swing;
    uint8_t param_count;
    param_id_t params[PARAM_COUNT];
    float values[PARAM_COUNT];
} ui_track_clipboard_t;

typedef struct
{
    ui_track_clipboard_t track;
    ui_param_clipboard_t ensemble;
    ui_param_clipboard_t page;
} ui_clipboard_state_t;

static ui_clipboard_state_t g_ui_clipboard;

static uint32_t ui_core_clipboard_make_undo_gesture_key(uint8_t op, uint8_t track, uint8_t extra)
{
    return (0x30000000UL
        | ((uint32_t)op << 24)
        | ((uint32_t)track << 8)
        | (uint32_t)extra);
}

static void ui_core_clipboard_feedback(ui_core_clipboard_feedback_fn feedback, const char *message)
{
    if (feedback != 0)
    {
        feedback(message);
    }
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
                || (id == PARAM_CFG_MIDI_CH) || (id == PARAM_CFG_MIDI_SRC))
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

static uint8_t ui_core_clipboard_resolve_template_family_from_button(button_id_t button,
                                                                     ui_template_family_id_t *out_family)
{
    if (out_family == 0)
    {
        return 0U;
    }

    switch (button)
    {
        case BTN_PARAM_1: *out_family = UI_TEMPLATE_FAMILY_COLORS; return 1U;
        case BTN_PARAM_2: *out_family = UI_TEMPLATE_FAMILY_TONE; return 1U;
        case BTN_PARAM_3: *out_family = UI_TEMPLATE_FAMILY_MOD; return 1U;
        case BTN_PARAM_4: *out_family = UI_TEMPLATE_FAMILY_MIX; return 1U;
        case BTN_PARAM_5: *out_family = UI_TEMPLATE_FAMILY_PLAY; return 1U;
        case BTN_PARAM_6: *out_family = UI_TEMPLATE_FAMILY_VCA; return 1U;
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
    const ui_track_config_t config = ui_get_track_config(active_track);
    const ui_template_family_t *family = ui_template_family_resolve(family_id, active_track, config.family, config.type);

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

static uint8_t ui_core_clipboard_apply_param_list(uint8_t track,
                                                  const param_id_t *params,
                                                  const float *values,
                                                  uint8_t count)
{
    if ((params == 0) || (values == 0))
    {
        return 0U;
    }

    track_runtime_refresh_track(track);
    param_registry_batch_begin();
    uint8_t applied = 0U;
    for (uint8_t i = 0U; i < count; ++i)
    {
        if (param_registry_apply_track_value(params[i], track, values[i]) != 0U)
        {
            ++applied;
        }
    }
    param_registry_batch_end();
    return applied;
}

static void ui_core_clipboard_clear_param_list_to_min(uint8_t track,
                                                      const param_id_t *params,
                                                      uint8_t count)
{
    if (params == 0)
    {
        return;
    }

    track_runtime_refresh_track(track);
    param_registry_batch_begin();
    for (uint8_t i = 0U; i < count; ++i)
    {
        const param_id_t id = params[i];
        if (id < PARAM_COUNT)
        {
            (void)param_registry_apply_track_value(id, track, param_registry[id].min);
        }
    }
    param_registry_batch_end();
}

static uint8_t ui_core_clipboard_copy_track(uint8_t track)
{
    ui_track_clipboard_t *const cb = &g_ui_clipboard.track;
    memset(cb, 0, sizeof(*cb));

    cb->source_track = track;
    cb->config = ui_get_track_config(track);
    cb->midi_channel = ui_get_track_midi_channel(track);
    cb->midi_source = ui_get_track_midi_source(track);
    cb->seq_page = seq_model_get_track_page(track);
    cb->seq_length = seq_model_get_track_length(track);
    (void)seq_runtime_get_track_div(track, &cb->seq_div);
    (void)seq_runtime_get_track_quant(track, &cb->seq_quant);
    (void)seq_runtime_get_track_swing(track, &cb->seq_swing);

    uint8_t count = 0U;
    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        const param_id_t id = (param_id_t)raw_id;
        const track_runtime_param_status_t status = track_runtime_get_effective_param_status(track, id);
        if ((status != TRACK_RUNTIME_PARAM_ALLOWED) && (status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
        {
            continue;
        }

        float value = 0.0f;
        if (param_registry_get_track_value(id, track, &value) == 0U)
        {
            continue;
        }

        cb->params[count] = id;
        cb->values[count] = value;
        ++count;
    }

    cb->param_count = count;
    cb->valid = 1U;
    return 1U;
}

static uint8_t ui_core_clipboard_clear_track(uint8_t track)
{
    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        const param_id_t id = (param_id_t)raw_id;
        (void)param_registry_apply_track_value(id, track, param_registry[id].default_value);
    }

    for (seq_step_id_t step = 0U; step < (seq_step_id_t)SEQ_MAX_STEPS; ++step)
    {
        seq_model_set_trig(track, step, 0U);
        seq_model_step_plock_clear(track, step);
    }

    seq_model_set_track_page(track, 0U);
    seq_model_set_track_length(track, (uint8_t)SEQ_MAX_STEPS);
    seq_runtime_set_track_div(track, 1U);
    seq_runtime_set_track_quant(track, 0U);
    seq_runtime_set_track_swing(track, 0U);
    (void)seq_runtime_set_playhead_step(track, 0U);

    (void)ui_set_track_midi_channel(track, (uint8_t)((track < 16U) ? (track + 1U) : 16U));
    (void)ui_set_track_midi_source(track, UI_TRACK_MIDI_SRC_ALL);
    (void)ui_set_track_family(track, UI_TRACK_FAMILY_OFF);
    (void)ui_set_track_type(track, UI_TRACK_TYPE_AUDIO);
    return 1U;
}

static uint8_t ui_core_clipboard_track_is_simple_exclusive(const ui_track_clipboard_t *cb)
{
    if (cb == 0)
    {
        return 0U;
    }

    return (uint8_t)((cb->config.family == UI_TRACK_FAMILY_MASTER)
                         && (cb->config.type == UI_TRACK_TYPE_BUFFER));
}

static uint8_t ui_core_clipboard_track_is_input_exclusive(const ui_track_clipboard_t *cb)
{
    if (cb == 0)
    {
        return 0U;
    }

    return (uint8_t)ui_track_family_is_input(cb->config.family);
}

static ui_track_family_t ui_core_clipboard_find_free_input_family(void)
{
    for (ui_track_family_t family = UI_TRACK_FAMILY_INPUT1; family <= UI_TRACK_FAMILY_INPUT4; ++family)
    {
        if (ui_count_tracks_with_family(family) == 0U)
        {
            return family;
        }
    }

    return UI_TRACK_FAMILY_COUNT;
}

static uint8_t ui_core_clipboard_move_exclusive_track_config(uint8_t source_track,
                                                             uint8_t target_track,
                                                             ui_track_family_t target_family,
                                                             ui_track_type_t target_type)
{
    if ((source_track >= UI_TRACK_COUNT) || (target_track >= UI_TRACK_COUNT))
    {
        return 0U;
    }

    uint8_t family[UI_TRACK_COUNT];
    uint8_t type[UI_TRACK_COUNT];
    uint8_t midi_channel[UI_TRACK_COUNT];
    uint8_t midi_source[UI_TRACK_COUNT];

    for (uint8_t i = 0U; i < UI_TRACK_COUNT; ++i)
    {
        const ui_track_config_t cfg = ui_get_track_config(i);
        family[i] = (uint8_t)cfg.family;
        type[i] = (uint8_t)cfg.type;
        midi_channel[i] = ui_get_track_midi_channel(i);
        midi_source[i] = (uint8_t)ui_get_track_midi_source(i);
    }

    family[source_track] = (uint8_t)UI_TRACK_FAMILY_OFF;
    type[source_track] = (uint8_t)UI_TRACK_TYPE_AUDIO;
    family[target_track] = (uint8_t)target_family;
    type[target_track] = (uint8_t)target_type;

    return (uint8_t)(ui_restore_track_config_bulk(family, type, midi_channel, midi_source) ? 1U : 0U);
}

static uint8_t ui_core_clipboard_paste_track(uint8_t track)
{
    ui_track_clipboard_t *const cb_mut = &g_ui_clipboard.track;
    const ui_track_clipboard_t *const cb = cb_mut;
    if (cb->valid == 0U)
    {
        return 0U;
    }

    const uint8_t source_track = cb->source_track;
    const uint8_t source_track_valid = (source_track < UI_TRACK_COUNT) ? 1U : 0U;
    const uint8_t source_equals_target = (uint8_t)((source_track_valid != 0U) && (source_track == track));

    ui_track_family_t target_family = cb->config.family;
    uint8_t clear_source_after_success = 0U;

    if ((source_equals_target == 0U) && (source_track_valid != 0U))
    {
        if (ui_core_clipboard_track_is_simple_exclusive(cb) != 0U)
        {
            clear_source_after_success = 1U;
        }
        else if (ui_core_clipboard_track_is_input_exclusive(cb) != 0U)
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
    }

    if ((clear_source_after_success != 0U) && (source_equals_target == 0U))
    {
        if (ui_core_clipboard_move_exclusive_track_config(source_track, track, target_family, cb->config.type) == 0U)
        {
            return 0U;
        }
    }
    else
    {
        if (ui_set_track_family(track, target_family) == false)
        {
            return 0U;
        }
        if (ui_set_track_type(track, cb->config.type) == false)
        {
            return 0U;
        }
    }

    (void)ui_set_track_midi_channel(track, cb->midi_channel);
    (void)ui_set_track_midi_source(track, cb->midi_source);
    seq_model_set_track_page(track, cb->seq_page);
    seq_model_set_track_length(track, cb->seq_length);
    seq_runtime_set_track_div(track, cb->seq_div);
    seq_runtime_set_track_quant(track, cb->seq_quant);
    seq_runtime_set_track_swing(track, cb->seq_swing);

    const uint8_t applied = ui_core_clipboard_apply_param_list(track, cb->params, cb->values, cb->param_count);
    if ((cb->param_count > 0U) && (applied == 0U))
    {
        return 0U;
    }

    if ((clear_source_after_success != 0U) && (source_equals_target == 0U))
    {
        if (ui_core_clipboard_clear_track(source_track) == 0U)
        {
            return 0U;
        }

        cb_mut->source_track = track;
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
        if ((id >= PARAM_COUNT) || (param_registry_get_track_value(id, track, &value) == 0U))
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
        if (param_registry_apply_track_value(target, track, value) != 0U)
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
        undo_v1_begin_gesture(ui_core_clipboard_make_undo_gesture_key(1U, track, 0U));
        (void)undo_v1_capture_before_edit(0U);
        if (ui_core_clipboard_clear_track(track) != 0U)
        {
            ui_core_clipboard_feedback(feedback, "TRACK CLEARED");
        }
        return 1U;
    }

    undo_v1_begin_gesture(ui_core_clipboard_make_undo_gesture_key(2U, track, 0U));
    (void)undo_v1_capture_before_edit(0U);
    if (ui_core_clipboard_paste_track(track) != 0U)
    {
        ui_core_clipboard_feedback(feedback, "TRACK PASTED");
    }
    else
    {
        ui_core_clipboard_feedback(feedback, "TRACK INCOMP");
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

    button_id_t held_button = BTN_COUNT;
    if (ui_core_clipboard_get_held_param_button(&held_button) == 0U)
    {
        return 0U;
    }

    ui_template_family_id_t family_id = UI_TEMPLATE_FAMILY_COUNT;
    if (ui_core_clipboard_resolve_template_family_from_button(held_button, &family_id) == 0U)
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
        undo_v1_begin_gesture(ui_core_clipboard_make_undo_gesture_key(3U, track, (uint8_t)count));
        (void)undo_v1_capture_before_edit(0U);
        ui_core_clipboard_clear_param_list_to_min(track, params, count);
        ui_edit_context_sync_active_track(0U);
        ui_core_clipboard_feedback(feedback, "ENS CLEARED");
        return 1U;
    }

    undo_v1_begin_gesture(ui_core_clipboard_make_undo_gesture_key(4U, track, (uint8_t)count));
    (void)undo_v1_capture_before_edit(0U);
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

    if (ui_core_clipboard_is_active_page_button_held() == 0U)
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
        undo_v1_begin_gesture(ui_core_clipboard_make_undo_gesture_key(5U, track, (uint8_t)count));
        (void)undo_v1_capture_before_edit(0U);
        ui_core_clipboard_clear_param_list_to_min(track, params, count);
        ui_edit_context_sync_active_track(0U);
        ui_core_clipboard_feedback(feedback, "PAGE CLEARED");
        return 1U;
    }

    undo_v1_begin_gesture(ui_core_clipboard_make_undo_gesture_key(6U, track, (uint8_t)count));
    (void)undo_v1_capture_before_edit(0U);
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
        if (seq_edit_copy_steps(track, steps, step_count) != 0U)
        {
            ui_core_clipboard_feedback(feedback, (step_scope != 0U) ? "STEP COPIED" : "SEQ COPIED");
        }
        return 1U;
    }

    if (shift_down != 0U)
    {
        seq_edit_clear_steps(track, steps, step_count);
        ui_core_clipboard_feedback(feedback, (step_scope != 0U) ? "STEP CLEARED" : "SEQ CLEARED");
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
