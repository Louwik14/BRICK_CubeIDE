#include "ui_core_seq_transport.h"

#include "stm32h7xx_hal.h"
#include "App/control_domain.h"
#include "App/control_clipboard.h"
#include "Board/board_product.h"
#include "buttons.h"
#include "ui_page_manager.h"
#include "pages/ui_page_template_cfg.h"
#include "Track/track_runtime.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_model.h"
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

    return (uint8_t)(hall_mode == UI_HALL_MODE_KEYBOARD);
}

static uint8_t ui_core_seq_transport_request_roll(int8_t delta)
{
    seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
    seq_track_id_t held_track = 0U;
    const uint8_t held_count = seq_edit_collect_held_steps(&held_track,
                                                           held_steps,
                                                           (uint8_t)SEQ_STEPS_PER_PAGE,
                                                           1U);
    if (held_count == 0U) return 0U;

    uint8_t roll = seq_model_get_step_roll(held_track, held_steps[0]);
    if ((delta > 0) && (roll < (uint8_t)(SEQ_STEP_ROLL_COUNT - 1U))) ++roll;
    else if ((delta < 0) && (roll > (uint8_t)SEQ_STEP_ROLL_OFF)) --roll;

    const control_seq_intent_t intent = {
        .operation = CONTROL_SEQ_ROLL_DELTA,
        .delta = delta
    };
    if (control_domain_request_seq(&intent) == 0U) return 0U;
    ui_roll_popup_show(held_track, held_steps[0], roll, HAL_GetTick());
    return 1U;
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

    const uint8_t track = ui_get_active_lane();

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
            if (control_clipboard_ui_available() == 0U)
            {
                if (feedback != 0) feedback("CLIP BUSY");
                return 1U;
            }
            if (shift_down != 0U)
            {
                (void)control_clipboard_request_sequence_apply(
                    held_track, held_steps, held_count, 1U);
                return 1U;
            }

            (void)seq_edit_copy_steps(held_track, held_steps, held_count);
            return 1U;
        }

        if (control_clipboard_request_sequence_apply(
                held_track, held_steps, held_count, 0U) == 0U)
        {
            if (feedback != 0) feedback("CLIP BUSY");
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
            const control_seq_intent_t intent = {
                .operation = CONTROL_SEQ_STEP_PRESS,
                .track = track,
                .step = ev->id
            };
            return control_domain_request_seq(&intent);
        }
#endif
        return 0U;
    }

    if ((ev->type == UI_EVENT_HALL_PRESS) && (ev->id < SEQ_STEPS_PER_PAGE))
    {
        const control_seq_intent_t intent = {
            .operation = CONTROL_SEQ_STEP_PRESS,
            .track = track,
            .step = ev->id
        };
        return control_domain_request_seq(&intent);
    }

    if ((ev->type == UI_EVENT_HALL_RELEASE) && (ev->id < SEQ_STEPS_PER_PAGE))
    {
        const control_seq_intent_t intent = {
            .operation = CONTROL_SEQ_STEP_RELEASE,
            .track = ui_get_active_lane(),
            .step = ev->id
        };
        return control_domain_request_seq(&intent);
    }

    if (ev->type == UI_EVENT_BUTTON_PRESS)
    {
        if (ev->id == (uint8_t)BTN_TRANSPOSE_UP)
        {
            if (ui_core_seq_transport_request_roll(1) != 0U) return 1U;
            const control_seq_intent_t intent = {
                .operation = CONTROL_SEQ_CHANGE_PAGE,
                .track = track,
                .delta = 1
            };
            return control_domain_request_seq(&intent);
        }

        if (ev->id == (uint8_t)BTN_TRANSPOSE_DOWN)
        {
            if (ui_core_seq_transport_request_roll(-1) != 0U) return 1U;
            const control_seq_intent_t intent = {
                .operation = CONTROL_SEQ_CHANGE_PAGE,
                .track = track,
                .delta = -1
            };
            return control_domain_request_seq(&intent);
        }
    }

    return 0U;
}
