#include "Storage/undo_v1.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/pattern_live_ram.h"
#include "Audio/mixer.h"
#include "Core/track_runtime.h"
#include "Param/param_registry.h"
#include "main.h"

typedef struct
{
    uint8_t valid;
    uint8_t source_slot;
    uint8_t active_bank;
    uint8_t active_pattern;
    uint8_t queued_valid;
    uint8_t queued_bank;
    uint8_t queued_pattern;
    uint8_t mute_valid[SEQ_TRACK_COUNT];
    uint8_t mute_state[SEQ_TRACK_COUNT];
    PatternSaveV1 snapshot;
} undo_snapshot_v1_t;

UI_SDRAM static undo_snapshot_v1_t g_undo_v1;
static uint8_t g_undo_capture_suspended;

void undo_v1_init(void)
{
    memset(&g_undo_v1, 0, sizeof(g_undo_v1));
    g_undo_capture_suspended = 0U;
}

static void undo_v1_capture_mute_state(void)
{
    memset(g_undo_v1.mute_valid, 0, sizeof(g_undo_v1.mute_valid));
    memset(g_undo_v1.mute_state, 0, sizeof(g_undo_v1.mute_state));

    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        uint8_t mix_track = 0U;
        if (track_runtime_get_mix_target_track(track, &mix_track) == 0U)
        {
            continue;
        }

        g_undo_v1.mute_valid[track] = 1U;
        g_undo_v1.mute_state[track] = mixer_get_track_mute(mix_track);
    }
}

static void undo_v1_restore_mute_state(void)
{
    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        uint8_t mix_track = 0U;
        if ((g_undo_v1.mute_valid[track] == 0U)
            || (track_runtime_get_mix_target_track(track, &mix_track) == 0U))
        {
            continue;
        }

        (void)mix_track;
        (void)param_registry_apply_track_value(PARAM_MIX_MUTE, track, (g_undo_v1.mute_state[track] != 0U) ? 1.0f : 0.0f);
    }
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

    (void)pattern_live_get_active(&g_undo_v1.active_bank, &g_undo_v1.active_pattern);
    (void)pattern_live_get_queued(&g_undo_v1.queued_valid,
                                  &g_undo_v1.queued_bank,
                                  &g_undo_v1.queued_pattern);
    undo_v1_capture_mute_state();

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
    if (ok != 0U)
    {
        undo_v1_restore_mute_state();
        pattern_live_set_active_state(g_undo_v1.active_bank,
                                      g_undo_v1.active_pattern,
                                      g_undo_v1.queued_valid,
                                      g_undo_v1.queued_bank,
                                      g_undo_v1.queued_pattern);
    }
    g_undo_capture_suspended = 0U;

    return ok;
}

uint8_t undo_v1_is_available(void)
{
    return g_undo_v1.valid;
}
