#include "ui_core_pattern.h"

#include "buttons.h"
#include "App/Hall/hall_engine.h"
#include "Storage/pattern_live_ram.h"

typedef struct
{
    ui_pattern_substate_t substate;
    ui_pattern_mode_t mode;
    uint8_t selected_bank;
    ui_hall_mode_t prev_mode;
    uint8_t prev_mode_valid;
} ui_core_pattern_state_t;

static ui_core_pattern_state_t g_ui_core_pattern = {
    .substate = UI_PATTERN_SUBSTATE_BANK_SELECT,
    .mode = UI_PATTERN_MODE_RECALL,
    .selected_bank = 0U,
    .prev_mode = UI_HALL_MODE_SEQ,
    .prev_mode_valid = 0U
};

static void ui_core_pattern_reset_selection_only(void)
{
    g_ui_core_pattern.substate = UI_PATTERN_SUBSTATE_BANK_SELECT;
    g_ui_core_pattern.selected_bank = 0U;
}

static void ui_core_pattern_exit_to_previous_mode(ui_core_pattern_set_hall_mode_fn set_hall_mode)
{
    if (set_hall_mode == 0)
    {
        return;
    }

    const ui_hall_mode_t target = (g_ui_core_pattern.prev_mode_valid != 0U)
            ? g_ui_core_pattern.prev_mode
            : UI_HALL_MODE_SEQ;
    g_ui_core_pattern.prev_mode_valid = 0U;
    set_hall_mode(target);
}

void ui_core_pattern_init(void)
{
    g_ui_core_pattern.substate = UI_PATTERN_SUBSTATE_BANK_SELECT;
    g_ui_core_pattern.mode = UI_PATTERN_MODE_RECALL;
    g_ui_core_pattern.selected_bank = 0U;
    g_ui_core_pattern.prev_mode = UI_HALL_MODE_SEQ;
    g_ui_core_pattern.prev_mode_valid = 0U;
}

void ui_core_pattern_abort(void)
{
    ui_core_pattern_reset_selection_only();
    g_ui_core_pattern.prev_mode_valid = 0U;
}

void ui_core_pattern_enter(ui_pattern_mode_t mode,
                           ui_hall_mode_t current_hall_mode,
                           ui_core_pattern_set_hall_mode_fn set_hall_mode)
{
    if (set_hall_mode == 0)
    {
        return;
    }

    g_ui_core_pattern.prev_mode = current_hall_mode;
    g_ui_core_pattern.prev_mode_valid = (current_hall_mode != UI_HALL_MODE_PATTERN) ? 1U : 0U;
    g_ui_core_pattern.mode = mode;
    ui_core_pattern_reset_selection_only();
    set_hall_mode(UI_HALL_MODE_PATTERN);
}

uint8_t ui_core_pattern_handle_mode_event(const ui_event_t *ev,
                                          ui_hall_mode_t hall_mode,
                                          uint8_t shift_down,
                                          uint8_t track_select_armed,
                                          ui_core_pattern_set_hall_mode_fn set_hall_mode,
                                          ui_core_pattern_feedback_fn feedback)
{
    if ((ev == 0) || (hall_mode != UI_HALL_MODE_PATTERN))
    {
        return 0U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)BTN_TRANSPOSE_DOWN)
        && (((shift_down != 0U) && (g_ui_core_pattern.mode == UI_PATTERN_MODE_RECALL))
            || ((track_select_armed != 0U) && (g_ui_core_pattern.mode == UI_PATTERN_MODE_STORE))))
    {
        ui_core_pattern_exit_to_previous_mode(set_hall_mode);
        return 1U;
    }

    if ((ev->type != UI_EVENT_HALL_PRESS) || (ev->id >= HALL_UI_LANE_COUNT))
    {
        return 0U;
    }

    if (g_ui_core_pattern.substate == UI_PATTERN_SUBSTATE_BANK_SELECT)
    {
        g_ui_core_pattern.selected_bank = ev->id;
        g_ui_core_pattern.substate = UI_PATTERN_SUBSTATE_PATTERN_SELECT;
        return 1U;
    }

    if (g_ui_core_pattern.mode == UI_PATTERN_MODE_STORE)
    {
        if (pattern_live_capture_to_slot(g_ui_core_pattern.selected_bank, ev->id) != 0U)
        {
            if (feedback != 0)
            {
                feedback("PAT STORED");
            }
            ui_core_pattern_exit_to_previous_mode(set_hall_mode);
            return 1U;
        }
    }
    else if (pattern_live_queue_slot(g_ui_core_pattern.selected_bank, ev->id,
                                    ui_get_active_track()) != 0U)
    {
        if (feedback != 0)
        {
            feedback("PAT QUEUED");
        }
        ui_core_pattern_exit_to_previous_mode(set_hall_mode);
        return 1U;
    }

    if (feedback != 0)
    {
        feedback("PAT FAIL");
    }
    ui_core_pattern_exit_to_previous_mode(set_hall_mode);
    return 1U;
}

ui_pattern_substate_t ui_core_pattern_get_substate(void)
{
    return g_ui_core_pattern.substate;
}

uint8_t ui_core_pattern_get_selected_bank(void)
{
    return g_ui_core_pattern.selected_bank;
}

ui_pattern_mode_t ui_core_pattern_get_mode(void)
{
    return g_ui_core_pattern.mode;
}
