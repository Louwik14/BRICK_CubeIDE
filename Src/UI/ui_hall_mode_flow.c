#include "ui_hall_mode_flow.h"

#include "Board/board_product.h"
#include "Storage/patch_v1.h"
#include "pages/ui_page_audio_rec.h"
#include "pages/ui_page_patch_assign.h"
#include "pages/ui_page_settings.h"
#include "pages/ui_page_template_tone.h"
#include "ui_core_feedback.h"
#include "ui_core_navigation_bridge.h"
#include "ui_hall_mode_contract.h"
#include "ui_hall_mode_projection.h"
#include "ui_navigation.h"

#define UI_HALL_MODE_DOUBLE_TAP_MS 400U
#define UI_HALL_PATCH_SAVE_ARM_MS 80U

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

static ui_hall_mode_flow_patch_pending_t g_patch_pending;
static uint8_t g_lowcost_rec_return_page = UI_PAGE_TEMPLATE_CFG;
static ui_hall_mode_t g_lowcost_rec_return_mode = UI_HALL_MODE_SEQ;
static uint8_t g_lowcost_rec_return_valid;

static void ui_hall_mode_flow_leave_lowcost_modal_page(void);

static uint8_t ui_hall_mode_flow_has_lowcost_step_modes(void)
{
    const board_product_capabilities_t *caps = board_product_capabilities();
    return ((caps != 0)
            && (caps->has_step_binary_lanes != 0U)
            && (caps->has_separate_hall_keyboard != 0U)) ? 1U : 0U;
}

static void ui_hall_mode_flow_activate_mode(ui_hall_mode_t target_mode,
                                            uint8_t target_page,
                                            uint8_t is_double_tap)
{
    if (ui_macro_overlay_is_active() != 0U)
    {
        ui_macro_overlay_on_hall_mode_changed();
    }
    ui_set_hall_mode(target_mode);
    ui_core_navigation_bridge_request_hall_mode_page(target_mode, target_page, is_double_tap);
}

static void ui_hall_mode_flow_open_midi_fx(void)
{
    ui_hall_mode_flow_leave_lowcost_modal_page();
    ui_navigation_request_page_with_availability(UI_PAGE_MIDI_FX);
}

static uint8_t ui_hall_mode_flow_open_looper_rout(void)
{
    if (ui_hall_mode_resolve_rout_context(ui_get_active_track(), ui_get_hall_mode())
            == UI_HALL_ROUT_CONTEXT_NONE)
    {
        return 0U;
    }

    ui_hall_mode_flow_open_midi_fx();
    return 1U;
}

static void ui_hall_mode_flow_close_lowcost_rec(void)
{
    if (ui_page_audio_rec_is_open() == 0U)
    {
        return;
    }

    const uint8_t return_page = (g_lowcost_rec_return_valid != 0U)
        ? g_lowcost_rec_return_page
        : UI_PAGE_TEMPLATE_CFG;
    const ui_hall_mode_t return_mode = (g_lowcost_rec_return_valid != 0U)
        ? g_lowcost_rec_return_mode
        : UI_HALL_MODE_SEQ;
    g_lowcost_rec_return_valid = 0U;
    ui_set_hall_mode(return_mode);
    ui_navigation_request_page_with_availability(return_page);
}

static void ui_hall_mode_flow_leave_lowcost_modal_page(void)
{
    if (ui_page_patch_assign_is_open() != 0U)
    {
        ui_page_patch_assign_close();
        return;
    }
    if (ui_page_settings_is_open() != 0U)
    {
        ui_page_settings_close_to_return_page();
        return;
    }
    ui_hall_mode_flow_close_lowcost_rec();
}

static void ui_hall_mode_flow_handle_lowcost_nav_button(button_id_t button)
{
    ui_hall_mode_flow_leave_lowcost_modal_page();
    const ui_event_t event = {
        .type = UI_EVENT_BUTTON_PRESS,
        .id = (uint8_t)button,
        .value = 0,
    };
    ui_navigation_handle_event(&event);
}

static uint8_t ui_hall_mode_flow_handle_lowcost_shift_step(uint8_t hall,
                                                           uint32_t now_ms,
                                                           uint32_t mode_tap_ms[UI_HALL_MODE_COUNT],
                                                           uint8_t hall_note_suppressed[HALL_UI_LANE_COUNT])
{
    if (ui_hall_mode_flow_has_lowcost_step_modes() == 0U)
    {
        return 0U;
    }

    hall_note_suppressed[hall] = 1U;
    g_patch_pending.active = 0U;

    ui_hall_mode_t target_mode = UI_HALL_MODE_SEQ;
    uint8_t target_page = UI_HALL_MODE_TARGET_PAGE_NONE;
    switch (hall)
    {
        case 0U:
            target_mode = UI_HALL_MODE_KEYBOARD;
            target_page = UI_PAGE_TEMPLATE_KEYBOARD;
            break;

        case 1U:
            target_mode = UI_HALL_MODE_SEQ;
            target_page = UI_PAGE_TEMPLATE_SEQ;
            break;

        case 2U:
            return 1U;

        case 3U:
            if (ui_page_patch_assign_is_open() != 0U)
            {
                ui_page_patch_assign_close();
                return 1U;
            }
            if (track_topology_is_active(ui_get_active_track()) == 0U)
            {
                ui_core_feedback_set("TRACK ONLY", now_ms);
                return 1U;
            }
            ui_hall_mode_flow_leave_lowcost_modal_page();
            if (ui_macro_overlay_is_active() != 0U)
            {
                ui_macro_overlay_on_hall_mode_changed();
            }
            const ui_hall_mode_t previous_mode = ui_get_hall_mode();
            ui_set_hall_mode(UI_HALL_MODE_PATCH);
            ui_page_patch_assign_open(ui_get_active_track(), previous_mode);
            return 1U;

        case 4U:
            if (ui_page_settings_is_open() != 0U)
            {
                ui_page_settings_close_to_return_page();
                return 1U;
            }
            ui_hall_mode_flow_leave_lowcost_modal_page();
            ui_page_settings_open_sample_browser(ui_page_get_id());
            return 1U;

        case 5U:
            if (ui_page_audio_rec_is_open() != 0U)
            {
                ui_hall_mode_flow_close_lowcost_rec();
                return 1U;
            }
            ui_hall_mode_flow_leave_lowcost_modal_page();
            g_lowcost_rec_return_page = ui_page_get_id();
            g_lowcost_rec_return_mode = ui_get_hall_mode();
            g_lowcost_rec_return_valid = 1U;
            target_mode = UI_HALL_MODE_AUDIO_REC;
            target_page = UI_PAGE_AUDIO_REC;
            break;

        case 6U:
            if (ui_hall_mode_resolve_rout_context(ui_get_active_track(), ui_get_hall_mode())
                    == UI_HALL_ROUT_CONTEXT_NONE)
            {
                ui_hall_mode_flow_open_midi_fx();
            }
            return 1U;

        case 7U:
            target_mode = UI_HALL_MODE_MACRO;
            target_page = UI_PAGE_TEMPLATE_MACRO;
            break;

        case 8U:
            return 1U;

        case 9U:
            ui_hall_mode_flow_handle_lowcost_nav_button(BTN_PARAM_2);
            return 1U;

        case 10U:
            ui_hall_mode_flow_handle_lowcost_nav_button(BTN_PARAM_1);
            return 1U;

        case 11U:
            ui_hall_mode_flow_handle_lowcost_nav_button(BTN_PARAM_5);
            return 1U;

        case 12U:
            ui_hall_mode_flow_handle_lowcost_nav_button(BTN_PARAM_3);
            return 1U;

        case 13U:
            ui_hall_mode_flow_handle_lowcost_nav_button(BTN_PARAM_4);
            return 1U;

        case 14U:
            (void)ui_hall_mode_flow_open_looper_rout();
            return 1U;

        default:
            return 0U;
    }

    if (target_mode != UI_HALL_MODE_AUDIO_REC)
    {
        ui_hall_mode_flow_leave_lowcost_modal_page();
    }

    const uint32_t last_tap = mode_tap_ms[target_mode];
    const uint8_t is_double_tap = ((last_tap != 0U)
                                   && ((now_ms - last_tap) <= UI_HALL_MODE_DOUBLE_TAP_MS)) ? 1U : 0U;
    mode_tap_ms[target_mode] = now_ms;
    ui_hall_mode_flow_activate_mode(target_mode, target_page, is_double_tap);
    return 1U;
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
                                                uint8_t hall_note_suppressed[HALL_UI_LANE_COUNT])
{
    if (hall >= HALL_UI_LANE_COUNT)
    {
        return;
    }

    if (hall == 15U)
    {
        hall_note_suppressed[hall] = 1U;
        g_patch_pending.active = 0U;
        if ((ui_page_get_id() == UI_PAGE_TEMPLATE_TONE)
                && (ui_page_template_tone_is_global_master() != 0U))
        {
            ui_page_template_tone_toggle_subset();
        }
        else
        {
            ui_hall_mode_flow_leave_lowcost_modal_page();
            ui_page_template_tone_open_global_master();
        }
        return;
    }

    if (ui_hall_mode_flow_handle_lowcost_shift_step(hall, now_ms, mode_tap_ms, hall_note_suppressed) != 0U)
    {
        return;
    }

    if (hall == 0U)
    {
        hall_note_suppressed[hall] = 1U;
        if (track_topology_is_active(ui_get_active_track()) == 0U)
        {
            g_patch_pending.active = 0U;
            ui_core_feedback_set("TRACK ONLY", now_ms);
            return;
        }
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
        return;
    }

    g_patch_pending.active = 0U;

    if (hall == 9U)
    {
        hall_note_suppressed[hall] = 1U;
        if (ui_hall_mode_resolve_rout_context(ui_get_active_track(), ui_get_hall_mode())
                == UI_HALL_ROUT_CONTEXT_NONE)
        {
            ui_navigation_request_page_with_availability(UI_PAGE_MIDI_FX);
        }
        return;
    }

    if (hall == 14U)
    {
        hall_note_suppressed[hall] = 1U;
        (void)ui_hall_mode_flow_open_looper_rout();
        return;
    }

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
    ui_hall_mode_flow_activate_mode(target_mode, target_page, is_double_tap);
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
}

void ui_hall_mode_flow_handle_track_hall_action(uint8_t hall,
                                                uint32_t now_ms,
                                                uint8_t held_master_candidate,
                                                uint8_t has_held_master_candidate,
                                                uint32_t cfg_tap_ms[UI_TRACK_COUNT],
                                                uint8_t hall_note_suppressed[HALL_UI_LANE_COUNT],
                                                ui_hall_mode_flow_set_active_track_fn set_active_track,
                                                ui_hall_mode_flow_feedback_fn feedback)
{
    (void)held_master_candidate;
    (void)has_held_master_candidate;
    (void)feedback;
    if ((hall >= HALL_UI_LANE_COUNT) || (hall >= UI_ACTIVE_TRACK_COUNT))
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

    if (track_topology_is_active(hall) == 0U)
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

    if (set_active_track != 0)
    {
        set_active_track(hall);
    }
    if (is_double_tap != 0U)
    {
        ui_core_navigation_bridge_request_cfg_page();
    }
}
