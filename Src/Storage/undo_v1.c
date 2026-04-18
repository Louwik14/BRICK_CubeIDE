#include "Storage/undo_v1.h"

#include <string.h>

#include "Core/engine_tasklet.h"
#include "Param/param_store.h"
#include "Storage/memory_layout.h"
#include "Storage/pattern_live_ram.h"
#include "UI/ui_active_track_sync.h"
#include "main.h"

#define UNDO_V1_GESTURE_TIMEOUT_TICKS 300U
#define UNDO_V1_MAX_LEVELS 10U

typedef struct
{
    uint32_t gesture_key;
    uint32_t gesture_tick;
    PatternSaveV1 snapshot;
} undo_snapshot_v1_entry_t;

typedef struct
{
    uint8_t count;
    uint8_t oldest_index;
    undo_snapshot_v1_entry_t entries[UNDO_V1_MAX_LEVELS];
} undo_snapshot_v1_history_t;

UI_SDRAM static undo_snapshot_v1_history_t g_undo_v1;
UI_SDRAM static PatternSaveV1 g_undo_capture_work;
static uint8_t g_undo_capture_suspended;
static uint32_t g_undo_gesture_key;
static uint8_t g_undo_gesture_key_valid;

static void undo_v1_sanitize_snapshot(PatternSaveV1 *snapshot)
{
    if (snapshot == 0)
    {
        return;
    }

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        snapshot->mix.track_valid[track][PARAM_MIX_MUTE] = 0U;
        snapshot->mix.track_values[track][PARAM_MIX_MUTE] = 0.0f;
    }
}

static uint8_t undo_v1_history_is_empty(void)
{
    return (g_undo_v1.count == 0U) ? 1U : 0U;
}

static uint8_t undo_v1_history_top_index(void)
{
    if (undo_v1_history_is_empty() != 0U)
    {
        return 0U;
    }

    return (uint8_t)((g_undo_v1.oldest_index + g_undo_v1.count - 1U) % UNDO_V1_MAX_LEVELS);
}

static undo_snapshot_v1_entry_t *undo_v1_history_top_entry(void)
{
    if (undo_v1_history_is_empty() != 0U)
    {
        return 0;
    }

    return &g_undo_v1.entries[undo_v1_history_top_index()];
}

static undo_snapshot_v1_entry_t *undo_v1_history_push_entry(void)
{
    uint8_t index = 0U;

    if (g_undo_v1.count == 0U)
    {
        g_undo_v1.oldest_index = 0U;
        g_undo_v1.count = 1U;
        return &g_undo_v1.entries[0];
    }

    if (g_undo_v1.count < UNDO_V1_MAX_LEVELS)
    {
        index = (uint8_t)((g_undo_v1.oldest_index + g_undo_v1.count) % UNDO_V1_MAX_LEVELS);
        g_undo_v1.count++;
        return &g_undo_v1.entries[index];
    }

    index = g_undo_v1.oldest_index;
    g_undo_v1.oldest_index = (uint8_t)((g_undo_v1.oldest_index + 1U) % UNDO_V1_MAX_LEVELS);
    return &g_undo_v1.entries[index];
}

void undo_v1_init(void)
{
    undo_v1_clear_history();
    g_undo_capture_suspended = 0U;
    g_undo_gesture_key = 0U;
    g_undo_gesture_key_valid = 0U;
}

void undo_v1_clear_history(void)
{
    memset(&g_undo_v1, 0, sizeof(g_undo_v1));
    g_undo_gesture_key = 0U;
    g_undo_gesture_key_valid = 0U;
}

void undo_v1_begin_gesture(uint32_t gesture_key)
{
    g_undo_gesture_key = gesture_key;
    g_undo_gesture_key_valid = 1U;
}

uint8_t undo_v1_capture_before_edit(uint8_t source_hint)
{
    if ((g_undo_capture_suspended != 0U)
        || (pattern_live_is_apply_in_progress() != 0U)
        || (__get_IPSR() != 0U))
    {
        return 0U;
    }

    const uint32_t now_tick = engine_tick_count;
    const uint32_t effective_gesture_key = (g_undo_gesture_key_valid != 0U)
        ? g_undo_gesture_key
        : (0x80000000UL | (uint32_t)source_hint);

    undo_snapshot_v1_entry_t *const top_entry = undo_v1_history_top_entry();
    if ((top_entry != 0)
        && (top_entry->gesture_key == effective_gesture_key)
        && ((now_tick - top_entry->gesture_tick) <= UNDO_V1_GESTURE_TIMEOUT_TICKS))
    {
        return 1U;
    }

    if (pattern_live_capture_current(&g_undo_capture_work) == 0U)
    {
        return 0U;
    }

    undo_v1_sanitize_snapshot(&g_undo_capture_work);

    undo_snapshot_v1_entry_t *const entry = undo_v1_history_push_entry();
    if (entry == 0)
    {
        return 0U;
    }

    entry->snapshot = g_undo_capture_work;
    entry->gesture_key = effective_gesture_key;
    entry->gesture_tick = now_tick;
    return 1U;
}

uint8_t undo_v1_restore(uint8_t resume_transport)
{
    if ((undo_v1_history_is_empty() != 0U) || (__get_IPSR() != 0U))
    {
        return 0U;
    }

    const uint8_t top_index = undo_v1_history_top_index();
    g_undo_capture_suspended = 1U;
    const uint8_t ok = pattern_live_apply_snapshot(&g_undo_v1.entries[top_index].snapshot, resume_transport);
    if (ok != 0U)
    {
        if (g_undo_v1.count == 1U)
        {
            undo_v1_clear_history();
        }
        else
        {
            g_undo_v1.count--;
        }

        ui_active_track_sync_full_after_global_restore();
    }
    g_undo_capture_suspended = 0U;
    g_undo_gesture_key = 0U;
    g_undo_gesture_key_valid = 0U;

    return ok;
}

uint8_t undo_v1_is_available(void)
{
    return (undo_v1_history_is_empty() != 0U) ? 0U : 1U;
}
