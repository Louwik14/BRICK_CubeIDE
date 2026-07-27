#include "ui_core_seq_transport.h"

#include "stm32h7xx_hal.h"
#include "Board/board_product.h"
#include "buttons.h"
#include "ui_page_manager.h"
#include "pages/ui_page_template_cfg.h"
#include "Core/track_runtime.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_clipboard.h"
#include "ui_roll_popup.h"

static uint8_t ui_core_seq_transport_hall_steps_available_in_mode(ui_hall_mode_t hall_mode)
{
    if (ui_hall_is_seq_context(hall_mode) != 0U)
    {
        return 1U;
    }

    const board_product_capabilities_t *const caps = board_product_capabilities();
    if ((caps == 0)
        || (caps->has_step_binary_lanes == 0U)
        || (caps->has_separate_hall_keyboard == 0U))
    {
        return 0U;
    }

    return (uint8_t)(((hall_mode == UI_HALL_MODE_KEYBOARD) || (hall_mode == UI_HALL_MODE_ARP)) ? 1U : 0U);
}

uint8_t ui_core_seq_transport_handle_transport_event(const ui_event_t *ev,
                                                     uint8_t mute_active,
                                                     uint8_t shift_down,
                                                     uint8_t track_select_armed,
                                                     ui_core_seq_transport_pattern_enter_fn pattern_enter,
                                                     ui_core_seq_transport_feedback_fn feedback)
{
    if (ev == 0)
    {
        return 0U;
    }

    (void)mute_active;

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)BTN_PLAY))
    {
        /* Command surface: transport toggle is an explicit runtime command, not a query side effect. */
        seq_runtime_toggle_play_stop();
        return 1U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)BTN_REC))
    {
        if (shift_down != 0U)
        {
            ui_page_template_rec_cfg_open_main();
            ui_page_set(UI_PAGE_TEMPLATE_REC_CFG);
            return 1U;
        }

        /* Command surface: pattern-rec arm is an explicit runtime command with target track preselection. */
        uint8_t rec_target_track = ui_get_active_track();
        (void)track_runtime_get_voice_group_effective_master(rec_target_track, &rec_target_track);
        seq_runtime_set_pattern_rec_target_track(rec_target_track);
        seq_runtime_rec_toggle_arm();
        return 1U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS)
        && (ev->id == (uint8_t)BTN_TRANSPOSE_DOWN)
        && (shift_down != 0U))
    {
        if (pattern_enter != 0)
        {
            pattern_enter(UI_PATTERN_MODE_RECALL);
        }
        return 1U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS)
        && (ev->id == (uint8_t)BTN_TRANSPOSE_DOWN)
        && (track_select_armed != 0U))
    {
        if (pattern_enter != 0)
        {
            pattern_enter(UI_PATTERN_MODE_STORE);
        }
        return 1U;
    }

    return 0U;
}

uint8_t ui_core_seq_transport_handle_seq_mode_event(const ui_event_t *ev,
                                                    ui_hall_mode_t hall_mode,
                                                    uint8_t shift_down,
                                                    ui_core_seq_transport_feedback_fn feedback)
{
    if ((ev == 0) || (ui_core_seq_transport_hall_steps_available_in_mode(hall_mode) == 0U))
    {
        return 0U;
    }

    const uint8_t track = ui_get_active_track();

    if ((ev->type == UI_EVENT_BUTTON_PRESS)
        && ((ev->id == (uint8_t)BTN_COPY) || (ev->id == (uint8_t)BTN_PASTE)))
    {
        seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
        seq_track_id_t held_track = 0U;
        const uint8_t held_count = seq_edit_collect_held_steps(&held_track,
                                                               held_steps,
                                                               (uint8_t)SEQ_STEPS_PER_PAGE,
                                                               1U);
        if (held_count == 0U)
        {
            return 1U;
        }

        if (ev->id == (uint8_t)BTN_COPY)
        {
            (void)seq_edit_copy_steps(held_track, held_steps, held_count);
            return 1U;
        }

        if (shift_down != 0U)
        {
            seq_edit_clear_steps(held_track, held_steps, held_count);
            return 1U;
        }

        seq_clipboard_paste_result_t paste_result;
        if (seq_edit_paste_steps(held_track, held_steps, held_count, &paste_result) != 0U)
        {
            if ((paste_result.trunc != 0U) && (feedback != 0))
            {
                feedback("PASTE TRUNC");
            }
            else if ((paste_result.partial != 0U) && (feedback != 0))
            {
                feedback("PASTE PARTIAL");
            }
        }

        return 1U;
    }

    if (shift_down != 0U)
    {
#if defined(BRICK6_VARIANT_LOWCOST)
        if ((ev->type == UI_EVENT_HALL_PRESS)
            && (ev->id < SEQ_STEPS_PER_PAGE)
            && (seq_edit_lowcost_range_length_candidate(track, ev->id) != 0U))
        {
            seq_edit_step_press(track, ev->id);
            return 1U;
        }
#endif
        return 0U;
    }

    if ((ev->type == UI_EVENT_HALL_PRESS) && (ev->id < SEQ_STEPS_PER_PAGE))
    {
        seq_edit_step_press(track, ev->id);
        return 1U;
    }

    if ((ev->type == UI_EVENT_HALL_RELEASE) && (ev->id < SEQ_STEPS_PER_PAGE))
    {
        seq_edit_step_release(ui_get_active_track(), ev->id);
        return 1U;
    }

    if (ev->type == UI_EVENT_BUTTON_PRESS)
    {
        if (ev->id == (uint8_t)BTN_TRANSPOSE_UP)
        {
            seq_track_id_t roll_track = 0U;
            seq_step_id_t roll_step = 0U;
            uint8_t roll = 0U;
            if (seq_edit_adjust_held_step_roll(1, &roll_track, &roll_step, &roll) != 0U)
            {
                ui_roll_popup_show(roll_track, roll_step, roll, HAL_GetTick());
                return 1U;
            }
            seq_edit_change_page(track, 1);
            return 1U;
        }

        if (ev->id == (uint8_t)BTN_TRANSPOSE_DOWN)
        {
            seq_track_id_t roll_track = 0U;
            seq_step_id_t roll_step = 0U;
            uint8_t roll = 0U;
            if (seq_edit_adjust_held_step_roll(-1, &roll_track, &roll_step, &roll) != 0U)
            {
                ui_roll_popup_show(roll_track, roll_step, roll, HAL_GetTick());
                return 1U;
            }
            seq_edit_change_page(track, -1);
            return 1U;
        }
    }

    return 0U;
}
