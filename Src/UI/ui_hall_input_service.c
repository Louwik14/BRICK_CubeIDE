#include "ui_hall_input_service.h"

#include "App/Hall/hall_engine.h"
#include "buttons.h"
#include "stm32h7xx_hal.h"
#include "ui_core_mute.h"
#include "ui_core_runtime_bridge.h"
#include "ui_hall_mode_flow.h"

void ui_hall_input_service_handle_hall(uint8_t hall,
                                       uint8_t pressed,
                                       uint8_t was_pressed,
                                       uint8_t shift_down,
                                       uint8_t track_select_armed,
                                       uint8_t mute_active,
                                       uint32_t mode_tap_ms[UI_HALL_MODE_COUNT],
                                       uint32_t cfg_tap_ms[UI_TRACK_COUNT],
                                       uint8_t hall_note_suppressed[HALL_KEY_COUNT],
                                       ui_hall_input_service_set_active_track_fn set_active_track)
{
    const ui_hall_direct_action_t action =
        ui_hall_mode_flow_resolve_direct_action(shift_down,
                                                track_select_armed,
                                                was_pressed,
                                                pressed);
    const uint32_t now_ms = HAL_GetTick();

    if (action == UI_HALL_DIRECT_ACTION_SHIFT_MODE)
    {
        const ui_mute_submode_t mute_submode = ui_core_mute_get_submode();
        const uint8_t allow_shift_mode_redirect =
            (mute_active == 0U) || (mute_submode == UI_MUTE_SUBMODE_PREPARE);
        if (allow_shift_mode_redirect != 0U)
        {
            ui_hall_mode_flow_handle_shift_hall_action(hall,
                                                       now_ms,
                                                       mode_tap_ms,
                                                       hall_note_suppressed);
        }
        return;
    }

    if ((action != UI_HALL_DIRECT_ACTION_TRACK_SELECT) || (mute_active != 0U))
    {
        return;
    }

    if ((hall < HALL_KEY_COUNT) && (hall < UI_TRACK_COUNT))
    {
        ui_hall_mode_flow_handle_track_hall_action(hall,
                                                   now_ms,
                                                   cfg_tap_ms,
                                                   hall_note_suppressed,
                                                   set_active_track);
    }
}

void ui_hall_input_service_handle_transpose(uint8_t shift_down,
                                           uint8_t track_select_armed,
                                           uint8_t active_track)
{
    if ((ui_hall_allows_injection(active_track, ui_get_hall_mode()) != 0U)
        && (shift_down == 0U)
        && (track_select_armed == 0U))
    {
        if (button_pressed(BTN_TRANSPOSE_UP) != 0U)
        {
            ui_core_runtime_bridge_step_octave(1);
        }

        if (button_pressed(BTN_TRANSPOSE_DOWN) != 0U)
        {
            ui_core_runtime_bridge_step_octave(-1);
        }
    }
}
