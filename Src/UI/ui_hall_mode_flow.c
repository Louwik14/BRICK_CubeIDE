#include "ui_hall_mode_flow.h"

#include "ui_core_navigation_bridge.h"
#include "ui_hall_mode_contract.h"

#define UI_HALL_MODE_DOUBLE_TAP_MS 400U

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
    ui_set_hall_mode(target_mode);
    ui_core_navigation_bridge_request_hall_mode_page(target_mode, target_page, is_double_tap);
}

void ui_hall_mode_flow_handle_track_hall_action(uint8_t hall,
                                                uint32_t now_ms,
                                                uint32_t cfg_tap_ms[UI_TRACK_COUNT],
                                                uint8_t hall_note_suppressed[HALL_KEY_COUNT],
                                                ui_hall_mode_flow_set_active_track_fn set_active_track)
{
    if ((hall >= HALL_KEY_COUNT) || (hall >= UI_TRACK_COUNT))
    {
        return;
    }

    hall_note_suppressed[hall] = 1U;

    const uint32_t last_tap = cfg_tap_ms[hall];
    const uint8_t is_double_tap = ((last_tap != 0U)
                                   && ((now_ms - last_tap) <= UI_HALL_MODE_DOUBLE_TAP_MS)) ? 1U : 0U;
    cfg_tap_ms[hall] = now_ms;

    if (set_active_track != 0)
    {
        set_active_track(hall);
    }
    if (is_double_tap != 0U)
    {
        ui_core_navigation_bridge_request_cfg_page();
    }
}
