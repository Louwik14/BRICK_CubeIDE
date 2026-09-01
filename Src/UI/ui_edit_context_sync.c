#include "ui_edit_context_sync.h"

#include "Mod/mod_lfo_v1_control.h"
#include "ui_core.h"
#include "ui_navigation.h"
#include "ui_page_manager.h"
#include "ui_param.h"

void ui_edit_context_sync_active_track(uint8_t include_keyboard_focus_sync)
{
    (void)include_keyboard_focus_sync;

    mod_lfo_v1_invalidate_dest_cache_track(ui_get_active_lane());
    ui_navigation_sync_active_track_ensemble();
    ui_page_sync_active_context();
}

void ui_edit_context_sync_active_track_created_from_off(uint8_t include_keyboard_focus_sync)
{
    (void)include_keyboard_focus_sync;

    mod_lfo_v1_invalidate_dest_cache_track(ui_get_active_lane());
    ui_navigation_sync_created_track_destination();
    ui_page_sync_active_context();
}
