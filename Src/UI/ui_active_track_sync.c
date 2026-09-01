#include "ui_active_track_sync.h"

#include "ui_core.h"
#include "ui_edit_context_sync.h"

void ui_active_track_sync_full_after_reconfigure(void)
{
    ui_edit_context_sync_active_track(0U);
}

void ui_active_track_sync_after_track_structure_change(uint8_t sync_active_track_ui_context)
{
    if (sync_active_track_ui_context == 0U)
    {
        return;
    }

    ui_edit_context_sync_active_track(sync_active_track_ui_context);
}

void ui_active_track_sync_after_track_creation_from_off(uint8_t sync_active_track_ui_context)
{
    if (sync_active_track_ui_context == 0U)
    {
        return;
    }

    ui_edit_context_sync_active_track_created_from_off(sync_active_track_ui_context);
}

void ui_active_track_sync_full_after_global_restore(void)
{
    ui_active_track_sync_after_track_structure_change(1U);
}
