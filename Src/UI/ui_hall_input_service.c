#include "ui_hall_input_service.h"

#include "App/Hall/hall_engine.h"
#include "buttons.h"
#include "stm32h7xx_hal.h"
#include "ui_core_mute.h"
#include "Keyboard/keyboard_runtime.h"
#include "ui_macro_interaction.h"
#include "ui_hall_mode_flow.h"
#include "Track/entity_topology.h"
#include "Seq/seq_types.h"
#if defined(BRICK6_VARIANT_LOWCOST)
#include "Seq/seq_edit.h"
#include "ui_core.h"
#endif

uint8_t ui_hall_input_service_handle_hall(uint8_t hall,
                                          uint8_t pressed,
                                          uint8_t was_pressed,
                                          ui_hall_mode_t hall_mode,
                                          uint8_t shift_down,
                                          uint8_t track_select_armed,
                                          uint8_t mute_active,
                                          uint32_t mode_tap_ms[UI_HALL_MODE_COUNT],
                                          uint32_t cfg_tap_ms[TRACK_COUNT],
                                          ui_hall_input_service_set_active_track_fn set_active_track,
                                          ui_hall_input_service_feedback_fn feedback)
{
    if ((mute_active != 0U) && (hall < SEQ_LANE_CAPACITY))
    {
        return 1U;
    }

    const ui_hall_direct_action_t action =
        ui_hall_mode_flow_resolve_direct_action(shift_down,
                                                track_select_armed,
                                                was_pressed,
                                                pressed);
    const uint32_t now_ms = HAL_GetTick();
    const uint8_t track_select_without_shift =
        (uint8_t)((action == UI_HALL_DIRECT_ACTION_TRACK_SELECT) && (shift_down == 0U));
    const uint8_t macro_overlay_hall_context =
        (uint8_t)((ui_macro_overlay_is_active() != 0U)
                  && (track_select_without_shift == 0U)
                  && !((ui_macro_overlay_is_latched() != 0U)
                       && (shift_down != 0U)
                       && (action == UI_HALL_DIRECT_ACTION_SHIFT_MODE)));

    uint8_t lowcost_range_length_candidate = 0U;
#if defined(BRICK6_VARIANT_LOWCOST)
    if ((action == UI_HALL_DIRECT_ACTION_SHIFT_MODE)
        && (macro_overlay_hall_context == 0U)
        && (seq_edit_lowcost_range_length_candidate(ui_get_active_lane(), hall) != 0U))
    {
        lowcost_range_length_candidate = 1U;
    }
#endif

    if ((action == UI_HALL_DIRECT_ACTION_SHIFT_MODE)
        && (macro_overlay_hall_context == 0U)
        && (lowcost_range_length_candidate == 0U))
    {
        ui_hall_mode_flow_handle_shift_hall_action(hall,
                                                   now_ms,
                                                   mode_tap_ms);
        return 1U;
    }

    (void)hall_mode;

    if ((macro_overlay_hall_context != 0U) && (mute_active == 0U))
    {
        if ((was_pressed == 0U) && (pressed != 0U))
        {
            ui_macro_interaction_note_hall_press(hall);
        }
        else if ((was_pressed != 0U) && (pressed == 0U))
        {
            ui_macro_interaction_note_hall_release(hall);
        }
        ui_macro_interaction_service_hall(hall, pressed);

        if (macro_overlay_hall_context != 0U)
        {
            return 1U;
        }
    }

    if ((action != UI_HALL_DIRECT_ACTION_TRACK_SELECT) || (mute_active != 0U) || (shift_down != 0U))
    {
        return 0U;
    }

    entity_topology_descriptor_t entity;
    if ((hall < HALL_UI_LANE_COUNT)
            && (entity_topology_get((brick_entity_id_t)hall, &entity) != 0U)
            && (entity.active != 0U))
    {
        if (entity.role == ENTITY_ROLE_GROUP_CHILD)
        {
            if (set_active_track != 0)
            {
                set_active_track(hall);
            }
            return 1U;
        }
        ui_hall_mode_flow_handle_track_hall_action(hall,
                                                   now_ms,
                                                   0U,
                                                   0U,
                                                   cfg_tap_ms,
                                                   set_active_track,
                                                   feedback);
        return 1U;
    }

    return 1U;
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
            keyboard_runtime_step_octave(1);
        }

        if (button_pressed(BTN_TRANSPOSE_DOWN) != 0U)
        {
            keyboard_runtime_step_octave(-1);
        }
    }
}
