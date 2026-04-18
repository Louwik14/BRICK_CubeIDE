#include "ui_edit_context_sync.h"

#include "Keyboard/keyboard_runtime.h"
#include "ui_page_manager.h"
#include "ui_param.h"

void ui_edit_context_sync_active_track(uint8_t include_keyboard_focus_sync)
{
    if (include_keyboard_focus_sync != 0U)
    {
        keyboard_runtime_sync_track_focus_context();
    }

    ui_page_sync_active_context();
    ui_param_sync_active_bank_values();
}
