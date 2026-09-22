#include "ui_active_track_sync.h"

#include "ui_core.h"
#include "ui_edit_context_sync.h"
#include "Seq/seq_edit.h"
#include "Storage/persistence_debug.h"

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
    g_persist_dbg.active_track_before = ui_get_active_track();
    g_persist_dbg.ui_sync_reason =
        (g_persist_dbg.op == PERSIST_DBG_OP_PATTERN_LOAD)
            ? PERSIST_DBG_UI_SYNC_PATTERN_COMMIT
            : ((g_persist_dbg.op == PERSIST_DBG_OP_PROJECT_BLANK)
                ? PERSIST_DBG_UI_SYNC_PROJECT_BLANK
                : PERSIST_DBG_UI_SYNC_PROJECT_COMMIT);
    seq_edit_reset_after_global_restore();
    ui_normalize_active_track_after_global_restore();
    ui_active_track_sync_after_track_structure_change(1U);
    persist_debug_ui_sync(ui_get_active_track(),ui_get_active_track(),
                          g_persist_dbg.sequence);
}
