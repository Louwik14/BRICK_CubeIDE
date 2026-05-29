#include "ui_hall_mode_flow.h"

#include <string.h>

#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Param/param_registry.h"
#include "Storage/kit_v1.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/patch_v1.h"
#include "pages/ui_page_kit_assign.h"
#include "pages/ui_page_patch_assign.h"
#include "ui_edit_context_sync.h"
#include "ui_core_feedback.h"
#include "ui_core_navigation_bridge.h"
#include "ui_hall_mode_contract.h"
#include "ui_hall_mode_projection.h"

#define UI_HALL_MODE_DOUBLE_TAP_MS 400U
#define UI_HALL_PATCH_SAVE_ARM_MS 80U
#define UI_HALL_KIT_SAVE_ARM_MS 80U

typedef struct
{
    ui_track_config_t config;
    uint8_t midi_channel;
    ui_track_midi_source_t midi_source;
    param_id_t params[PARAM_COUNT];
    float values[PARAM_COUNT];
    uint16_t param_count;
} ui_hall_mode_flow_track_sound_copy_t;

typedef struct
{
    uint8_t active;
    uint32_t tap_ms;
    uint8_t target_track;
    ui_hall_mode_t previous_mode;
    uint8_t save_pending;
    uint32_t save_due_ms;
    uint8_t save_track;
} ui_hall_mode_flow_patch_pending_t;

typedef struct
{
    uint8_t active;
    uint32_t tap_ms;
    uint8_t save_pending;
    uint32_t save_due_ms;
} ui_hall_mode_flow_kit_pending_t;

static ui_hall_mode_flow_patch_pending_t g_patch_pending;
static ui_hall_mode_flow_kit_pending_t g_kit_pending;

static uint8_t ui_hall_mode_flow_capture_track_sound_copy(uint8_t source_track,
                                                          ui_hall_mode_flow_track_sound_copy_t *out_copy)
{
    if ((source_track >= UI_TRACK_COUNT) || (out_copy == 0))
    {
        return 0U;
    }

    memset(out_copy, 0, sizeof(*out_copy));
    out_copy->config = ui_get_track_config(source_track);
    out_copy->midi_channel = ui_get_track_midi_channel(source_track);
    out_copy->midi_source = ui_get_track_midi_source(source_track);

    track_runtime_refresh_track(source_track);
    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        const param_id_t id = (param_id_t)raw_id;
        if ((id == PARAM_CFG_TRACK)
                || (id == PARAM_CFG_TRACK_TYPE)
                || (id == PARAM_CFG_MIDI_CH)
                || (id == PARAM_CFG_MIDI_SRC))
        {
            continue;
        }

        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
        {
            continue;
        }

        if (track_runtime_get_effective_param_status(source_track, id) != TRACK_RUNTIME_PARAM_ALLOWED)
        {
            continue;
        }

        float value = 0.0f;
        if (param_registry_get_track_value(id, source_track, &value) == 0U)
        {
            continue;
        }

        out_copy->params[out_copy->param_count] = id;
        out_copy->values[out_copy->param_count] = value;
        ++out_copy->param_count;
    }

    return 1U;
}

static uint8_t ui_hall_mode_flow_apply_track_sound_copy(uint8_t target_track,
                                                        const ui_hall_mode_flow_track_sound_copy_t *copy)
{
    if ((target_track >= UI_TRACK_COUNT) || (copy == 0))
    {
        return 0U;
    }

    if (ui_set_track_family(target_track, copy->config.family) == false)
    {
        return 0U;
    }

    if (ui_set_track_type(target_track, copy->config.type) == false)
    {
        return 0U;
    }

    if (ui_set_track_midi_channel(target_track, copy->midi_channel) == false)
    {
        return 0U;
    }

    if (ui_set_track_midi_source(target_track, copy->midi_source) == false)
    {
        return 0U;
    }

    track_runtime_refresh_track(target_track);
    for (uint16_t i = 0U; i < copy->param_count; ++i)
    {
        if (track_runtime_get_effective_param_status(target_track, copy->params[i]) != TRACK_RUNTIME_PARAM_ALLOWED)
        {
            continue;
        }

        if (param_registry_apply_track_value(copy->params[i], target_track, copy->values[i]) == 0U)
        {
            continue;
        }
    }

    if (ui_get_active_track() == target_track)
    {
        ui_edit_context_sync_active_track(0U);
    }

    return 1U;
}

static uint8_t ui_hall_mode_flow_prepare_off_target_for_group_add(uint8_t anchor_track,
                                                                  uint8_t target_track,
                                                                  uint8_t needs_track_sound_copy,
                                                                  ui_hall_mode_flow_track_sound_copy_t *copy)
{
    if (needs_track_sound_copy == 0U)
    {
        return 1U;
    }

    if ((copy == 0)
            || (ui_hall_mode_flow_capture_track_sound_copy(anchor_track, copy) == 0U)
            || (ui_hall_mode_flow_apply_track_sound_copy(target_track, copy) == 0U))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t ui_hall_mode_flow_apply_group_role_add(uint8_t anchor_track,
                                                      track_voice_group_role_t anchor_role,
                                                      uint8_t target_track)
{
    if ((anchor_track >= UI_TRACK_COUNT) || (target_track >= UI_TRACK_COUNT))
    {
        return 0U;
    }

    uint8_t next_roles[UI_TRACK_COUNT];
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        next_roles[track] = (uint8_t)track_state_get_voice_group_role(track);
    }

    if (anchor_role == TRACK_VOICE_GROUP_ROLE_SOLO)
    {
        next_roles[anchor_track] = (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER;
    }
    next_roles[target_track] = (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE;

    return (track_state_apply_voice_group_roles_bulk(next_roles) != 0U) ? 1U : 0U;
}

static uint8_t ui_hall_mode_flow_apply_group_role_remove(uint8_t target_track)
{
    if (target_track >= UI_TRACK_COUNT)
    {
        return 0U;
    }

    uint8_t next_roles[UI_TRACK_COUNT];
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        next_roles[track] = (uint8_t)track_state_get_voice_group_role(track);
    }
    next_roles[target_track] = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;

    return (track_state_apply_voice_group_roles_bulk(next_roles) != 0U) ? 1U : 0U;
}

ui_hall_direct_action_t ui_hall_mode_flow_resolve_direct_action(uint8_t shift_down,
                                                                uint8_t track_select_armed,
                                                                uint8_t was_pressed,
                                                                uint8_t pressed)
{
    if ((was_pressed == 0U) && (pressed != 0U))
    {
        if ((shift_down != 0U) && (track_select_armed == 0U))
        {
            return UI_HALL_DIRECT_ACTION_SHIFT_MODE;
        }

        if (track_select_armed != 0U)
        {
            return UI_HALL_DIRECT_ACTION_TRACK_SELECT;
        }
    }

    return UI_HALL_DIRECT_ACTION_NONE;
}

void ui_hall_mode_flow_handle_shift_hall_action(uint8_t hall,
                                                uint32_t now_ms,
                                                uint32_t mode_tap_ms[UI_HALL_MODE_COUNT],
                                                uint8_t hall_note_suppressed[HALL_KEY_COUNT])
{
    if (hall >= HALL_KEY_COUNT)
    {
        return;
    }

    if (hall == 0U)
    {
        hall_note_suppressed[hall] = 1U;
        g_kit_pending.active = 0U;
        if ((g_patch_pending.active != 0U)
                && ((now_ms - g_patch_pending.tap_ms) <= UI_HALL_MODE_DOUBLE_TAP_MS))
        {
            ui_hall_patch_feedback_begin(now_ms);
            ui_core_feedback_set("PATCH SAVE", now_ms);
            g_patch_pending.save_pending = 1U;
            g_patch_pending.save_due_ms = now_ms + UI_HALL_PATCH_SAVE_ARM_MS;
            g_patch_pending.save_track = ui_get_active_track();
            g_patch_pending.active = 0U;
            return;
        }

        g_patch_pending.active = 1U;
        g_patch_pending.tap_ms = now_ms;
        g_patch_pending.target_track = ui_get_active_track();
        g_patch_pending.previous_mode = ui_get_hall_mode();
        return;
    }

    if (hall == 1U)
    {
        hall_note_suppressed[hall] = 1U;
        g_patch_pending.active = 0U;
        if ((g_kit_pending.active != 0U)
                && ((now_ms - g_kit_pending.tap_ms) <= UI_HALL_MODE_DOUBLE_TAP_MS))
        {
            ui_hall_kit_feedback_begin(now_ms);
            ui_core_feedback_set("KIT SAVE", now_ms);
            g_kit_pending.save_pending = 1U;
            g_kit_pending.save_due_ms = now_ms + UI_HALL_KIT_SAVE_ARM_MS;
            g_kit_pending.active = 0U;
            return;
        }

        g_kit_pending.active = 1U;
        g_kit_pending.tap_ms = now_ms;
        return;
    }

    g_patch_pending.active = 0U;
    g_kit_pending.active = 0U;

    ui_hall_mode_t target_mode = UI_HALL_MODE_SEQ;
    uint8_t target_page = UI_HALL_MODE_TARGET_PAGE_NONE;
    for (uint8_t mode = 0U; mode < (uint8_t)UI_HALL_MODE_COUNT; ++mode)
    {
        uint8_t trigger_hall = 0U;
        uint8_t resolved_page = 0U;
        if ((ui_hall_mode_get_trigger_hall((ui_hall_mode_t)mode, &trigger_hall) != 0U)
                && (trigger_hall == hall)
                && (ui_hall_mode_get_target_page((ui_hall_mode_t)mode, &resolved_page) != 0U))
        {
            target_mode = (ui_hall_mode_t)mode;
            target_page = resolved_page;
            break;
        }
    }

    if (target_page == UI_HALL_MODE_TARGET_PAGE_NONE)
    {
        return;
    }

    hall_note_suppressed[hall] = 1U;
    const uint32_t last_tap = mode_tap_ms[target_mode];
    const uint8_t is_double_tap = ((last_tap != 0U)
                                   && ((now_ms - last_tap) <= UI_HALL_MODE_DOUBLE_TAP_MS)) ? 1U : 0U;
    mode_tap_ms[target_mode] = now_ms;
    if (ui_macro_overlay_is_active() != 0U)
    {
        ui_macro_overlay_on_hall_mode_changed();
    }
    ui_set_hall_mode(target_mode);
    ui_core_navigation_bridge_request_hall_mode_page(target_mode, target_page, is_double_tap);
}

void ui_hall_mode_flow_service_pending(uint32_t now_ms)
{
    if (g_patch_pending.save_pending != 0U)
    {
        if ((int32_t)(now_ms - g_patch_pending.save_due_ms) < 0)
        {
            return;
        }

        uint16_t saved_slot = PATCH_V1_INVALID_SLOT;
        const patch_v1_result_t result =
            patch_v1_save_track_direct(g_patch_pending.save_track, &saved_slot);
        (void)saved_slot;
        const uint32_t done_ms = HAL_GetTick();
        ui_hall_patch_feedback_end(done_ms);
        g_patch_pending.save_pending = 0U;
        ui_core_feedback_set(patch_v1_result_label(result), done_ms);
        return;
    }

    if (g_kit_pending.save_pending != 0U)
    {
        if ((int32_t)(now_ms - g_kit_pending.save_due_ms) < 0)
        {
            return;
        }

        uint16_t saved_slot = KIT_V1_INVALID_SLOT;
        const kit_v1_result_t result = kit_v1_save_direct(&saved_slot);
        if ((result == KIT_V1_RESULT_OK) && (saved_slot < KIT_V1_SLOT_COUNT))
        {
            (void)pattern_live_link_active_kit(saved_slot);
        }
        const uint32_t done_ms = HAL_GetTick();
        ui_hall_kit_feedback_end(done_ms);
        g_kit_pending.save_pending = 0U;
        ui_core_feedback_set(kit_v1_result_label(result), done_ms);
        return;
    }

    if (g_patch_pending.active != 0U)
    {
        if ((now_ms - g_patch_pending.tap_ms) <= UI_HALL_MODE_DOUBLE_TAP_MS)
        {
            return;
        }

        const uint8_t target_track = g_patch_pending.target_track;
        const ui_hall_mode_t previous_mode = g_patch_pending.previous_mode;
        g_patch_pending.active = 0U;
        if (ui_macro_overlay_is_active() != 0U)
        {
            ui_macro_overlay_on_hall_mode_changed();
        }
        ui_set_hall_mode(UI_HALL_MODE_PATCH);
        ui_page_patch_assign_open(target_track, previous_mode);
        return;
    }

    if (g_kit_pending.active == 0U)
    {
        return;
    }

    if ((now_ms - g_kit_pending.tap_ms) <= UI_HALL_MODE_DOUBLE_TAP_MS)
    {
        return;
    }

    g_kit_pending.active = 0U;
    if (ui_macro_overlay_is_active() != 0U)
    {
        ui_macro_overlay_on_hall_mode_changed();
    }
    ui_page_kit_assign_open();
}

void ui_hall_mode_flow_handle_track_hall_action(uint8_t hall,
                                                uint32_t now_ms,
                                                uint8_t held_master_candidate,
                                                uint8_t has_held_master_candidate,
                                                uint32_t cfg_tap_ms[UI_TRACK_COUNT],
                                                uint8_t hall_note_suppressed[HALL_KEY_COUNT],
                                                ui_hall_mode_flow_set_active_track_fn set_active_track,
                                                ui_hall_mode_flow_feedback_fn feedback)
{
    if ((hall >= HALL_KEY_COUNT) || (hall >= UI_TRACK_COUNT))
    {
        return;
    }

    const uint8_t active_track_before_press = ui_get_active_track();
    hall_note_suppressed[hall] = 1U;

    const uint32_t last_tap = cfg_tap_ms[hall];
    const uint8_t is_double_tap = ((active_track_before_press == hall)
                                   && (last_tap != 0U)
                                   && ((now_ms - last_tap) <= UI_HALL_MODE_DOUBLE_TAP_MS)) ? 1U : 0U;
    cfg_tap_ms[hall] = now_ms;

    if (has_held_master_candidate == 0U)
    {
        if (set_active_track != 0)
        {
            set_active_track(hall);
        }
        if (is_double_tap != 0U)
        {
            ui_core_navigation_bridge_request_cfg_page();
        }
        return;
    }

    const uint8_t anchor_track = held_master_candidate;
    uint8_t master_track = anchor_track;
    if (track_runtime_get_voice_group_effective_master(anchor_track, &master_track) == 0U)
    {
        master_track = anchor_track;
    }

    uint8_t members[UI_TRACK_COUNT];
    uint8_t member_count = 0U;
    const uint8_t has_group_members = track_runtime_collect_voice_group_members(master_track,
                                                                                 members,
                                                                                 (uint8_t)UI_TRACK_COUNT,
                                                                                 &member_count);
    const uint8_t group_end = (has_group_members != 0U)
            ? members[(uint8_t)(member_count - 1U)]
            : master_track;

    uint8_t mutation_attempted = 0U;
    uint8_t mutation_applied = 0U;
    uint8_t invalid_action = 0U;
    const track_voice_group_role_t anchor_role = track_state_get_voice_group_role(anchor_track);
    const uint8_t needs_track_sound_copy =
        ((hall != anchor_track) && (ui_get_track_family(hall) == UI_TRACK_FAMILY_OFF)) ? 1U : 0U;
    ui_hall_mode_flow_track_sound_copy_t track_sound_copy;
    if (hall != anchor_track)
    {
        /* Add: only immediate right of current group end, never with holes. */
        if (hall == (uint8_t)(group_end + 1U))
        {
            mutation_attempted = 1U;
            if (ui_hall_mode_flow_prepare_off_target_for_group_add(anchor_track,
                                                                   hall,
                                                                   needs_track_sound_copy,
                                                                   &track_sound_copy) == 0U)
            {
                invalid_action = 1U;
            }

            if ((invalid_action == 0U) && (anchor_role == TRACK_VOICE_GROUP_ROLE_SOLO))
            {
                if (hall == (uint8_t)(anchor_track + 1U))
                {
                    if (ui_hall_mode_flow_apply_group_role_add(anchor_track, anchor_role, hall) != 0U)
                    {
                        mutation_applied = 1U;
                    }
                }
            }
            else if (invalid_action == 0U)
            {
                if (ui_hall_mode_flow_apply_group_role_add(anchor_track, anchor_role, hall) != 0U)
                {
                    mutation_applied = 1U;
                }
            }
        }
        /* Remove: only rightmost slave of an existing group. */
        else if ((hall == group_end) && (track_state_get_voice_group_role(hall) == TRACK_VOICE_GROUP_ROLE_SLAVE))
        {
            mutation_attempted = 1U;
            mutation_applied = ui_hall_mode_flow_apply_group_role_remove(hall);
        }
        else
        {
            invalid_action = 1U;
        }
    }

    if ((((mutation_attempted != 0U) && (mutation_applied == 0U)) || (invalid_action != 0U)) && (feedback != 0))
    {
        feedback("CHAIN ERR");
    }
}
