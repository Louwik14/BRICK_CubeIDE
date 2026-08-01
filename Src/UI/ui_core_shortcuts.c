#include "ui_core_shortcuts.h"

#include "buttons.h"
#include "ui_core_clipboard.h"
#include "ui_page_manager.h"
#include "pages/ui_page_settings.h"

uint8_t ui_core_shortcuts_handle_global_event(const ui_event_t *ev,
                                              uint8_t shift_down,
                                              uint8_t track_select_armed,
                                              uint8_t mute_active,
                                              ui_core_shortcuts_feedback_fn feedback)
{
    if (ev == 0)
    {
        return 0U;
    }

    if (ui_core_clipboard_handle_macro_lock_event(ev,
                                                  shift_down,
                                                  feedback) != 0U)
    {
        return 1U;
    }

    if (ui_core_clipboard_handle_track_event(ev,
                                             track_select_armed,
                                             shift_down,
                                             feedback) != 0U)
    {
        return 1U;
    }

    if (ui_core_clipboard_handle_ensemble_event(ev,
                                                shift_down,
                                                feedback) != 0U)
    {
        return 1U;
    }

    if (ui_core_clipboard_handle_page_event(ev,
                                            shift_down,
                                            feedback) != 0U)
    {
        return 1U;
    }

    if (ui_core_clipboard_handle_seq_track_event(ev,
                                                 track_select_armed,
                                                 shift_down,
                                                 feedback) != 0U)
    {
        return 1U;
    }

    if ((ev->type == UI_EVENT_BUTTON_PRESS) && (ev->id == (uint8_t)BTN_SETTINGS))
    {
        if (ui_page_settings_is_open() == 0U)
        {
            ui_page_settings_open(ui_page_get_id());
        }
        return 1U;
    }

    return 0U;
}
