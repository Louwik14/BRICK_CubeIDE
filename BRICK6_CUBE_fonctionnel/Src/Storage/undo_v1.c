#include "Storage/undo_v1.h"

#include <string.h>

#include "Storage/pattern_live_ram.h"
#include "main.h"

typedef struct
{
    uint8_t valid;
    uint8_t source_slot;
    PatternSaveV1 snapshot;
} undo_snapshot_v1_t;

static undo_snapshot_v1_t g_undo_v1;
static uint8_t g_undo_capture_suspended;

void undo_v1_init(void)
{
    memset(&g_undo_v1, 0, sizeof(g_undo_v1));
    g_undo_capture_suspended = 0U;
}

uint8_t undo_v1_capture_before_edit(uint8_t source_slot)
{
    if ((g_undo_capture_suspended != 0U)
        || (pattern_live_is_apply_in_progress() != 0U)
        || (__get_IPSR() != 0U))
    {
        return 0U;
    }

    if (pattern_live_capture_current(&g_undo_v1.snapshot) == 0U)
    {
        return 0U;
    }

    g_undo_v1.valid = 1U;
    g_undo_v1.source_slot = source_slot;
    return 1U;
}

uint8_t undo_v1_restore(uint8_t resume_transport)
{
    if ((g_undo_v1.valid == 0U) || (__get_IPSR() != 0U))
    {
        return 0U;
    }

    g_undo_capture_suspended = 1U;
    const uint8_t ok = pattern_live_apply_snapshot(&g_undo_v1.snapshot, resume_transport);
    g_undo_capture_suspended = 0U;

    return ok;
}

uint8_t undo_v1_is_available(void)
{
    return g_undo_v1.valid;
}
