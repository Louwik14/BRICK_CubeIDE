/* One chronological Undo/Redo history for sequence and REC_SOURCE actions. */
#include "Storage/undo_v2.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Seq/seq_edit.h"
#include "Storage/rec_source.h"
#include "Track/entity_topology.h"
#include "main.h"

typedef enum { UNDO_V2_ENTRY_FREE = 0, UNDO_V2_ENTRY_SEQUENCE,
               UNDO_V2_ENTRY_AUDIO } undo_v2_entry_kind_t;

typedef struct
{
    undo_v2_entry_kind_t kind;
    uint8_t track;
    uint8_t reserved[3];
    union
    {
        seq_step_snapshot_list_t sequence;
        struct { uint32_t before_generation, after_generation; } audio;
    } payload;
} undo_v2_entry_t;

typedef struct
{
    uint8_t tx_open, count, cursor, apply_in_progress;
    uint8_t capture_suspended, pending_track, reserved[2];
    undo_v2_status_t last_status;
} undo_v2_runtime_t;

UI_SDRAM static undo_v2_runtime_t g_undo_v2_runtime;
UI_SDRAM static undo_v2_entry_t g_undo_v2_entries[UNDO_V2_MAX_TRANSACTIONS];
UI_SDRAM static seq_step_snapshot_list_t g_undo_v2_pending_snapshot;
UI_SDRAM static seq_step_snapshot_t g_undo_v2_exchange_step;

static void set_status(undo_v2_status_t status) { g_undo_v2_runtime.last_status = status; }

static uint8_t copy_snapshot(seq_step_snapshot_list_t *dst,
                             const seq_step_snapshot_list_t *src)
{
    if ((dst == NULL) || (src == NULL)
            || (src->count > (uint8_t)SEQ_STEP_SNAPSHOT_MAX_STEPS)) return 0U;
    dst->count = src->count;
    memset(dst->reserved, 0, sizeof(dst->reserved));
    if (src->count != 0U)
        memcpy(dst->entries, src->entries,
               (size_t)src->count * sizeof(src->entries[0]));
    return 1U;
}

static void release_entry(undo_v2_entry_t *entry)
{
    if (entry == NULL) return;
    if (entry->kind == UNDO_V2_ENTRY_AUDIO)
        rec_source_release_history_pair(entry->payload.audio.before_generation,
                                        entry->payload.audio.after_generation);
    memset(entry, 0, sizeof(*entry));
}

static void remove_at(uint8_t index)
{
    if (index >= g_undo_v2_runtime.count) return;
    release_entry(&g_undo_v2_entries[index]);
    if ((uint8_t)(index + 1U) < g_undo_v2_runtime.count)
        memmove(&g_undo_v2_entries[index], &g_undo_v2_entries[index + 1U],
                (size_t)(g_undo_v2_runtime.count - index - 1U)
                    * sizeof(g_undo_v2_entries[0]));
    g_undo_v2_runtime.count--;
    memset(&g_undo_v2_entries[g_undo_v2_runtime.count], 0,
           sizeof(g_undo_v2_entries[0]));
    if (g_undo_v2_runtime.cursor > index) g_undo_v2_runtime.cursor--;
}

static void purge_redo(void)
{
    while (g_undo_v2_runtime.count > g_undo_v2_runtime.cursor)
        remove_at((uint8_t)(g_undo_v2_runtime.count - 1U));
}

static uint8_t sequence_count(void)
{
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < g_undo_v2_runtime.count; ++i)
        if (g_undo_v2_entries[i].kind == UNDO_V2_ENTRY_SEQUENCE) count++;
    return count;
}

static uint8_t capture_allowed(void)
{
    return (uint8_t)((g_undo_v2_runtime.capture_suspended == 0U)
        && (g_undo_v2_runtime.apply_in_progress == 0U) && (__get_IPSR() == 0U));
}

static void clear_pending(void)
{
    g_undo_v2_pending_snapshot.count = 0U;
    g_undo_v2_runtime.pending_track = 0U;
}

static uint8_t pending_change(void)
{
    for (uint8_t i = 0U; i < g_undo_v2_pending_snapshot.count; ++i)
    {
        if (seq_step_snapshot_capture(g_undo_v2_runtime.pending_track,
              g_undo_v2_pending_snapshot.entries[i].step,
              &g_undo_v2_exchange_step) == 0U) return 2U;
        if (seq_step_snapshot_equal(&g_undo_v2_pending_snapshot.entries[i].snapshot,
                                    &g_undo_v2_exchange_step) == 0U) return 1U;
    }
    return 0U;
}

static undo_v2_status_t exchange_sequence(undo_v2_entry_t *entry)
{
    if ((entry->track >= SEQ_LANE_CAPACITY)
            || (seq_edit_track_sequence_is_locked(entry->track) != 0U)
            || (seq_step_snapshot_can_apply_list(entry->track,
                    &entry->payload.sequence) == 0U))
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    g_undo_v2_pending_snapshot.count = entry->payload.sequence.count;
    memset(g_undo_v2_pending_snapshot.reserved, 0,
           sizeof(g_undo_v2_pending_snapshot.reserved));
    for (uint8_t i = 0U; i < entry->payload.sequence.count; ++i)
    {
        const seq_step_id_t step = entry->payload.sequence.entries[i].step;
        if (seq_step_snapshot_capture(entry->track, step,
                &g_undo_v2_pending_snapshot.entries[i].snapshot) == 0U)
            return UNDO_V2_STATUS_ERR_APPLY_FAILED;
        g_undo_v2_pending_snapshot.entries[i].step = step;
    }
    if (seq_step_snapshot_apply_list(entry->track,
                                     &entry->payload.sequence) == 0U)
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    if (copy_snapshot(&entry->payload.sequence,
                      &g_undo_v2_pending_snapshot) == 0U)
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    clear_pending();
    return UNDO_V2_STATUS_OK;
}

void undo_v2_init(void) { undo_v2_clear_all(); }

void undo_v2_clear_all(void)
{
    for (uint8_t i = 0U; i < g_undo_v2_runtime.count; ++i)
        release_entry(&g_undo_v2_entries[i]);
    memset(&g_undo_v2_runtime, 0, sizeof(g_undo_v2_runtime));
    memset(g_undo_v2_entries, 0, sizeof(g_undo_v2_entries));
    memset(&g_undo_v2_pending_snapshot, 0, sizeof(g_undo_v2_pending_snapshot));
    memset(&g_undo_v2_exchange_step, 0, sizeof(g_undo_v2_exchange_step));
    set_status(UNDO_V2_STATUS_OK);
}

void undo_v2_invalidate_history(void)
{
    const uint8_t suspended = g_undo_v2_runtime.capture_suspended;
    undo_v2_clear_all();
    g_undo_v2_runtime.capture_suspended = suspended;
}

void undo_v2_expire_audio(void)
{
    for (uint8_t i = 0U; i < g_undo_v2_runtime.count; ++i)
        if (g_undo_v2_entries[i].kind == UNDO_V2_ENTRY_AUDIO)
        { remove_at(i); break; }
}

undo_v2_status_t undo_v2_begin_sequence_transaction(seq_track_id_t track,
    const seq_step_id_t *steps, uint8_t step_count)
{
    if ((g_undo_v2_runtime.tx_open != 0U) || (capture_allowed() == 0U)
            || (track >= SEQ_LANE_CAPACITY) || (steps == NULL)
            || (step_count == 0U)
            || (step_count > (uint8_t)SEQ_STEP_SNAPSHOT_MAX_STEPS)
            || (seq_edit_track_sequence_is_locked(track) != 0U))
    { set_status(UNDO_V2_STATUS_ERR_INVALID_ARG); return g_undo_v2_runtime.last_status; }
    if (seq_step_snapshot_capture_list(track, steps, step_count,
                                      &g_undo_v2_pending_snapshot) == 0U)
    { clear_pending(); set_status(UNDO_V2_STATUS_ERR_CAPTURE_BLOCKED);
      return g_undo_v2_runtime.last_status; }
    g_undo_v2_runtime.pending_track = track;
    g_undo_v2_runtime.tx_open = 1U;
    set_status(UNDO_V2_STATUS_OK);
    return UNDO_V2_STATUS_OK;
}

undo_v2_status_t undo_v2_commit_sequence_transaction(void)
{
    if (g_undo_v2_runtime.tx_open == 0U)
    { set_status(UNDO_V2_STATUS_ERR_NO_TX); return g_undo_v2_runtime.last_status; }
    const uint8_t changed = pending_change();
    if (changed != 1U)
    {
        undo_v2_cancel_transaction();
        set_status((changed == 0U) ? UNDO_V2_STATUS_OK
                                  : UNDO_V2_STATUS_ERR_APPLY_FAILED);
        return g_undo_v2_runtime.last_status;
    }
    purge_redo();
    if (sequence_count() >= UNDO_V2_MAX_SEQUENCE_TRANSACTIONS)
        for (uint8_t i = 0U; i < g_undo_v2_runtime.count; ++i)
            if (g_undo_v2_entries[i].kind == UNDO_V2_ENTRY_SEQUENCE)
            { remove_at(i); break; }
    undo_v2_entry_t *const entry = &g_undo_v2_entries[g_undo_v2_runtime.count];
    memset(entry, 0, sizeof(*entry));
    entry->kind = UNDO_V2_ENTRY_SEQUENCE;
    entry->track = g_undo_v2_runtime.pending_track;
    if (copy_snapshot(&entry->payload.sequence, &g_undo_v2_pending_snapshot) == 0U)
    { undo_v2_cancel_transaction(); set_status(UNDO_V2_STATUS_ERR_APPLY_FAILED);
      return g_undo_v2_runtime.last_status; }
    g_undo_v2_runtime.count++;
    g_undo_v2_runtime.cursor = g_undo_v2_runtime.count;
    g_undo_v2_runtime.tx_open = 0U;
    clear_pending();
    set_status(UNDO_V2_STATUS_OK);
    return UNDO_V2_STATUS_OK;
}

undo_v2_status_t undo_v2_commit_audio_transition(uint32_t before_generation,
                                                 uint32_t after_generation)
{
    if ((g_undo_v2_runtime.tx_open != 0U) || (capture_allowed() == 0U)
            || (before_generation == after_generation))
    { set_status(UNDO_V2_STATUS_ERR_INVALID_ARG); return g_undo_v2_runtime.last_status; }
    purge_redo();
    undo_v2_expire_audio();
    if (g_undo_v2_runtime.count >= UNDO_V2_MAX_TRANSACTIONS) remove_at(0U);
    undo_v2_entry_t *const entry = &g_undo_v2_entries[g_undo_v2_runtime.count++];
    memset(entry, 0, sizeof(*entry));
    entry->kind = UNDO_V2_ENTRY_AUDIO;
    entry->payload.audio.before_generation = before_generation;
    entry->payload.audio.after_generation = after_generation;
    g_undo_v2_runtime.cursor = g_undo_v2_runtime.count;
    set_status(UNDO_V2_STATUS_OK);
    return UNDO_V2_STATUS_OK;
}

void undo_v2_cancel_transaction(void)
{
    g_undo_v2_runtime.tx_open = 0U;
    clear_pending();
    set_status(UNDO_V2_STATUS_OK);
}

static undo_v2_status_t apply_entry(undo_v2_entry_t *entry, uint8_t redo)
{
    if (entry->kind == UNDO_V2_ENTRY_SEQUENCE) return exchange_sequence(entry);
    if (entry->kind == UNDO_V2_ENTRY_AUDIO)
    {
        const uint32_t target = (redo != 0U) ? entry->payload.audio.after_generation
                                             : entry->payload.audio.before_generation;
        return (rec_source_switch_current(target) != 0U)
            ? UNDO_V2_STATUS_OK : UNDO_V2_STATUS_ERR_APPLY_FAILED;
    }
    return UNDO_V2_STATUS_ERR_UNSUPPORTED;
}

undo_v2_status_t undo_v2_undo(void)
{
    if ((g_undo_v2_runtime.tx_open != 0U) || (g_undo_v2_runtime.cursor == 0U))
    { set_status(UNDO_V2_STATUS_ERR_NO_TX); return g_undo_v2_runtime.last_status; }
    g_undo_v2_runtime.apply_in_progress = 1U;
    const undo_v2_status_t status = apply_entry(
        &g_undo_v2_entries[g_undo_v2_runtime.cursor - 1U], 0U);
    g_undo_v2_runtime.apply_in_progress = 0U;
    if (status == UNDO_V2_STATUS_OK) g_undo_v2_runtime.cursor--;
    set_status(status);
    return status;
}

undo_v2_status_t undo_v2_redo(void)
{
    if ((g_undo_v2_runtime.tx_open != 0U)
            || (g_undo_v2_runtime.cursor >= g_undo_v2_runtime.count))
    { set_status(UNDO_V2_STATUS_ERR_NO_TX); return g_undo_v2_runtime.last_status; }
    g_undo_v2_runtime.apply_in_progress = 1U;
    const undo_v2_status_t status = apply_entry(
        &g_undo_v2_entries[g_undo_v2_runtime.cursor], 1U);
    g_undo_v2_runtime.apply_in_progress = 0U;
    if (status == UNDO_V2_STATUS_OK) g_undo_v2_runtime.cursor++;
    set_status(status);
    return status;
}

void undo_v2_set_capture_suspended(uint8_t suspended)
{
    g_undo_v2_runtime.capture_suspended = (suspended != 0U) ? 1U : 0U;
    set_status(UNDO_V2_STATUS_OK);
}
