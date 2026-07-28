#include "ui_edit_context_sync.h"

#include "Keyboard/keyboard_runtime.h"
#include "Mod/mod_lfo_v1.h"
#include "ui_core.h"
#include "ui_core_navigation_bridge.h"
#include "ui_page_manager.h"
#include "ui_param.h"

void ui_edit_context_sync_active_track(uint8_t include_keyboard_focus_sync)
{
    if (include_keyboard_focus_sync != 0U)
    {
        keyboard_runtime_sync_track_focus_context();
    }

    mod_lfo_v1_invalidate_dest_cache_track(ui_get_active_track());
    ui_core_navigation_bridge_sync_active_track_ensemble();
    ui_page_sync_active_context();
    ui_param_sync_active_bank_values();
}

void ui_edit_context_sync_active_track_created_from_off(uint8_t include_keyboard_focus_sync)
{
    if (include_keyboard_focus_sync != 0U)
    {
        keyboard_runtime_sync_track_focus_context();
    }

    mod_lfo_v1_invalidate_dest_cache_track(ui_get_active_track());
    ui_core_navigation_bridge_sync_created_track_destination();
    ui_page_sync_active_context();
    ui_param_sync_active_bank_values();
}
