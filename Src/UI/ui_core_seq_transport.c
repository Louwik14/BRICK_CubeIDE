#include "ui_core_seq_transport.h"

#include "buttons.h"
#include "ui_page_manager.h"
#include "pages/ui_page_template_cfg.h"
#include "Core/brick6_master_buffer.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_clipboard.h"

static uint8_t ui_core_find_unique_master_buffer_track(uint8_t *out_track)
{
    uint8_t found = 0U;
    uint8_t found_track = 0U;

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if ((ui_get_track_family(track) == UI_TRACK_FAMILY_MASTER)
            && (ui_get_track_type(track) == UI_TRACK_TYPE_BUFFER))
        {
            if (found != 0U)
            {
                return 0U;
            }
            found = 1U;
            found_track = track;
        }
    }

    if (found == 0U)
    {
        return 0U;
    }

    if (out_track != 0)
    {
        *out_track = found_track;
    }
    return 1U;
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
        seq_runtime_toggle_play_stop();
        return 1U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)BTN_REC))
    {
        uint8_t master_buffer_track = 0U;
        const uint8_t has_master_buffer = ui_core_find_unique_master_buffer_track(&master_buffer_track);
        (void)master_buffer_track;

        if ((track_select_armed != 0U) && (has_master_buffer != 0U))
        {
            if (shift_down != 0U)
            {
                brick6_master_buffer_request_clear();
                if (feedback != 0)
                {
                    feedback("BUF CLR");
                }
            }
            else
            {
                brick6_master_buffer_request_record();
                if (feedback != 0)
                {
                    if (brick6_master_buffer_is_recording() != 0U)
                    {
                        feedback("BUF REC");
                    }
                    else if (brick6_master_buffer_is_armed() != 0U)
                    {
                        feedback("BUF ARM");
                    }
                    else
                    {
                        feedback("BUF STOP");
                    }
                }
            }
            return 1U;
        }

        if (shift_down != 0U)
        {
            ui_page_template_rec_cfg_open_main();
            ui_page_set(UI_PAGE_TEMPLATE_REC_CFG);
            return 1U;
        }

        seq_runtime_set_pattern_rec_target_track(ui_get_active_track());
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
    if ((ev == 0) || (ui_hall_is_seq_context(hall_mode) == 0U))
    {
        return 0U;
    }

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
        return 0U;
    }

    const uint8_t track = ui_get_active_track();

    if ((ev->type == UI_EVENT_HALL_PRESS) && (ev->id < SEQ_STEPS_PER_PAGE))
    {
        seq_edit_step_press(ui_get_active_track(), ev->id);
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
            seq_edit_change_page(track, 1);
            return 1U;
        }

        if (ev->id == (uint8_t)BTN_TRANSPOSE_DOWN)
        {
            seq_edit_change_page(track, -1);
            return 1U;
        }
    }

    return 0U;
}
