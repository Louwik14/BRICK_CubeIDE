/*
 * Module: undo_v2
 * Role: Undo/Redo cible sur les mutations structurelles de steps et la base
 * normalisee MIDI FX.
 * Responsibilities: anneau fixe de huit transactions, validation de slot,
 * echange d'une image canonique et invalidation atomique.
 */
#include "Storage/undo_v2.h"

#include <string.h>

#include "Core/engine_tasklet.h"
#include "NoteFx/note_fx_pipeline.h"
#include "NoteFx/note_fx_state.h"
#include "Core/track_topology.h"
#include "Seq/seq_edit.h"
#include "Storage/memory_layout.h"
#include "main.h"

typedef enum
{
    UNDO_V2_TRANSACTION_NONE = 0U,
    UNDO_V2_TRANSACTION_SEQUENCE,
    UNDO_V2_TRANSACTION_NOTE_FX
} undo_v2_transaction_kind_t;

typedef struct
{
    uint8_t used;
    uint8_t kind;
    uint8_t track;
    uint8_t reserved;
    seq_step_snapshot_list_t snapshot;
    note_fx_track_state_t note_fx[NOTE_FX_TRACK_COUNT];
} undo_v2_transaction_t;

typedef struct
{
    uint8_t tx_open;
    uint8_t undo_count;
    uint8_t redo_count;
    uint8_t oldest_index;
    uint8_t apply_in_progress;
    uint8_t capture_suspended;
    uint8_t pending_track;
    uint8_t pending_kind;
    undo_v2_status_t last_status;
} undo_v2_runtime_t;

UI_SDRAM static undo_v2_runtime_t g_undo_v2_runtime;
UI_SDRAM static undo_v2_transaction_t g_undo_v2_transactions[UNDO_V2_MAX_TRANSACTIONS];
UI_SDRAM static seq_step_snapshot_list_t g_undo_v2_pending_snapshot;
UI_SDRAM static seq_step_snapshot_t g_undo_v2_exchange_step;
UI_SDRAM static note_fx_track_state_t g_undo_v2_pending_note_fx[NOTE_FX_TRACK_COUNT];
UI_SDRAM static note_fx_track_state_t g_undo_v2_exchange_note_fx[NOTE_FX_TRACK_COUNT];

static uint8_t undo_v2_copy_snapshot_list(seq_step_snapshot_list_t *destination,
                                          const seq_step_snapshot_list_t *source)
{
    if ((destination == 0) || (source == 0)
            || (source->count > (uint8_t)SEQ_STEP_SNAPSHOT_MAX_STEPS))
    {
        return 0U;
    }

    destination->count = source->count;
    memset(destination->reserved, 0, sizeof(destination->reserved));
    if (source->count != 0U)
    {
        memcpy(destination->entries,
               source->entries,
               (size_t)source->count * sizeof(source->entries[0]));
    }
    return 1U;
}

static void undo_v2_set_status(undo_v2_status_t status)
{
    g_undo_v2_runtime.last_status = status;
}

static uint8_t undo_v2_history_total_count(void)
{
    return (uint8_t)(g_undo_v2_runtime.undo_count + g_undo_v2_runtime.redo_count);
}

static uint8_t undo_v2_history_tail_index(void)
{
    return (uint8_t)((g_undo_v2_runtime.oldest_index
                      + g_undo_v2_runtime.undo_count
                      + g_undo_v2_runtime.redo_count)
                     % UNDO_V2_MAX_TRANSACTIONS);
}

static uint8_t undo_v2_history_top_undo_index(void)
{
    return (uint8_t)((g_undo_v2_runtime.oldest_index
                      + g_undo_v2_runtime.undo_count - 1U)
                     % UNDO_V2_MAX_TRANSACTIONS);
}

static uint8_t undo_v2_history_top_redo_index(void)
{
    return (uint8_t)((g_undo_v2_runtime.oldest_index
                      + g_undo_v2_runtime.undo_count)
                     % UNDO_V2_MAX_TRANSACTIONS);
}

static void undo_v2_release_transaction(undo_v2_transaction_t *transaction)
{
    if (transaction != 0)
    {
        transaction->used = 0U;
        transaction->kind = (uint8_t)UNDO_V2_TRANSACTION_NONE;
        transaction->track = 0U;
        transaction->snapshot.count = 0U;
        memset(transaction->note_fx, 0, sizeof(transaction->note_fx));
    }
}

static void undo_v2_clear_pending(void)
{
    g_undo_v2_pending_snapshot.count = 0U;
    memset(g_undo_v2_pending_note_fx, 0, sizeof(g_undo_v2_pending_note_fx));
    g_undo_v2_runtime.pending_track = 0U;
    g_undo_v2_runtime.pending_kind = (uint8_t)UNDO_V2_TRANSACTION_NONE;
}

static void undo_v2_purge_redo_history(void)
{
    while (g_undo_v2_runtime.redo_count != 0U)
    {
        undo_v2_transaction_t *const transaction =
            &g_undo_v2_transactions[undo_v2_history_top_redo_index()];
        undo_v2_release_transaction(transaction);
        g_undo_v2_runtime.redo_count--;
    }
}

static void undo_v2_discard_oldest_transaction(void)
{
    if (undo_v2_history_total_count() == 0U)
    {
        return;
    }

    undo_v2_release_transaction(&g_undo_v2_transactions[g_undo_v2_runtime.oldest_index]);
    g_undo_v2_runtime.oldest_index =
        (uint8_t)((g_undo_v2_runtime.oldest_index + 1U) % UNDO_V2_MAX_TRANSACTIONS);
    if (g_undo_v2_runtime.undo_count != 0U)
    {
        g_undo_v2_runtime.undo_count--;
    }
    else if (g_undo_v2_runtime.redo_count != 0U)
    {
        g_undo_v2_runtime.redo_count--;
    }
}

static uint8_t undo_v2_capture_allowed(void)
{
    if ((g_undo_v2_runtime.capture_suspended != 0U)
            || (g_undo_v2_runtime.apply_in_progress != 0U)
            || (__get_IPSR() != 0U))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t undo_v2_transaction_target_is_current(
    const undo_v2_transaction_t *transaction)
{
    if ((transaction == 0)
            || (transaction->used == 0U)
            || (transaction->kind != (uint8_t)UNDO_V2_TRANSACTION_SEQUENCE)
            || (transaction->track >= SEQ_TRACK_COUNT)
            || (seq_edit_track_sequence_is_locked(transaction->track) != 0U))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t undo_v2_note_fx_pending_has_effective_change(void)
{
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        if (note_fx_state_capture_track(track, &g_undo_v2_exchange_note_fx[track]) == 0U)
        {
            return 2U;
        }
        if (memcmp(&g_undo_v2_pending_note_fx[track],
                   &g_undo_v2_exchange_note_fx[track],
                   sizeof(g_undo_v2_pending_note_fx[track])) != 0)
        {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t undo_v2_pending_has_effective_change(void)
{
    for (uint8_t i = 0U; i < g_undo_v2_pending_snapshot.count; ++i)
    {
        if (seq_step_snapshot_capture(g_undo_v2_runtime.pending_track,
                                      g_undo_v2_pending_snapshot.entries[i].step,
                                      &g_undo_v2_exchange_step) == 0U)
        {
            return 2U;
        }

        if (seq_step_snapshot_equal(&g_undo_v2_pending_snapshot.entries[i].snapshot,
                                     &g_undo_v2_exchange_step) == 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

static undo_v2_status_t undo_v2_exchange_transaction(
    undo_v2_transaction_t *transaction)
{
    if (undo_v2_transaction_target_is_current(transaction) == 0U)
    {
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    }

    if (seq_step_snapshot_can_apply_list(transaction->track,
                                         &transaction->snapshot) == 0U)
    {
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    }

    g_undo_v2_pending_snapshot.count = transaction->snapshot.count;
    memset(g_undo_v2_pending_snapshot.reserved,
           0,
           sizeof(g_undo_v2_pending_snapshot.reserved));
    for (uint8_t i = 0U; i < transaction->snapshot.count; ++i)
    {
        const seq_step_id_t step = transaction->snapshot.entries[i].step;
        if (seq_step_snapshot_capture(transaction->track,
                                      step,
                                      &g_undo_v2_pending_snapshot.entries[i].snapshot) == 0U)
        {
            return UNDO_V2_STATUS_ERR_APPLY_FAILED;
        }
        g_undo_v2_pending_snapshot.entries[i].step = step;
    }

    if (seq_step_snapshot_apply_list(transaction->track,
                                     &transaction->snapshot) == 0U)
    {
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    }

    /* The current image becomes the sole persistent image for the next swap. */
    if (undo_v2_copy_snapshot_list(&transaction->snapshot,
                                   &g_undo_v2_pending_snapshot) == 0U)
    {
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    }
    g_undo_v2_pending_snapshot.count = 0U;

    return UNDO_V2_STATUS_OK;
}

static undo_v2_status_t undo_v2_exchange_note_fx_transaction(
    undo_v2_transaction_t *transaction)
{
    if ((transaction == 0)
            || (transaction->used == 0U)
            || (transaction->kind != (uint8_t)UNDO_V2_TRANSACTION_NOTE_FX))
    {
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    }

    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        if (note_fx_state_capture_track(track, &g_undo_v2_exchange_note_fx[track]) == 0U)
        {
            return UNDO_V2_STATUS_ERR_APPLY_FAILED;
        }
    }

    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        if (note_fx_state_restore_track(track, &transaction->note_fx[track]) == 0U)
        {
            return UNDO_V2_STATUS_ERR_APPLY_FAILED;
        }

        uint8_t model_changed = 0U;
        for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
        {
            if (g_undo_v2_exchange_note_fx[track].value[slot][3U]
                    != transaction->note_fx[track].value[slot][3U])
            {
                model_changed = 1U;
                break;
            }
        }
        if (model_changed != 0U)
        {
            (void)note_fx_pipeline_transition_track(
                track, NOTE_FX_TRANSITION_MODEL_RECONFIGURE);
        }
        (void)note_fx_pipeline_sync_track(track);
    }

    memcpy(transaction->note_fx,
           g_undo_v2_exchange_note_fx,
           sizeof(transaction->note_fx));
    return UNDO_V2_STATUS_OK;
}

void undo_v2_init(void)
{
    undo_v2_clear_all();
}

void undo_v2_clear_all(void)
{
    memset(&g_undo_v2_runtime, 0, sizeof(g_undo_v2_runtime));
    memset(g_undo_v2_transactions, 0, sizeof(g_undo_v2_transactions));
    memset(&g_undo_v2_pending_snapshot, 0, sizeof(g_undo_v2_pending_snapshot));
    memset(&g_undo_v2_exchange_step, 0, sizeof(g_undo_v2_exchange_step));
    memset(g_undo_v2_pending_note_fx, 0, sizeof(g_undo_v2_pending_note_fx));
    memset(g_undo_v2_exchange_note_fx, 0, sizeof(g_undo_v2_exchange_note_fx));
    undo_v2_set_status(UNDO_V2_STATUS_OK);
}

void undo_v2_invalidate_history(void)
{
    const uint8_t capture_suspended = g_undo_v2_runtime.capture_suspended;
    memset(&g_undo_v2_runtime, 0, sizeof(g_undo_v2_runtime));
    g_undo_v2_runtime.capture_suspended = capture_suspended;
    undo_v2_clear_pending();
    undo_v2_set_status(UNDO_V2_STATUS_OK);
}

undo_v2_status_t undo_v2_begin_sequence_transaction(seq_track_id_t track,
                                                    const seq_step_id_t *steps,
                                                    uint8_t step_count)
{
    if ((g_undo_v2_runtime.tx_open != 0U)
            || (undo_v2_capture_allowed() == 0U)
            || (track >= SEQ_TRACK_COUNT)
            || (steps == 0)
            || (step_count == 0U)
            || (step_count > (uint8_t)SEQ_STEP_SNAPSHOT_MAX_STEPS)
            || (seq_edit_track_sequence_is_locked(track) != 0U))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_INVALID_ARG);
        return g_undo_v2_runtime.last_status;
    }

    if (seq_step_snapshot_capture_list(track,
                                       steps,
                                       step_count,
                                       &g_undo_v2_pending_snapshot) == 0U)
    {
        undo_v2_clear_pending();
        undo_v2_set_status(UNDO_V2_STATUS_ERR_CAPTURE_BLOCKED);
        return g_undo_v2_runtime.last_status;
    }

    g_undo_v2_runtime.pending_track = track;
    g_undo_v2_runtime.pending_kind = (uint8_t)UNDO_V2_TRANSACTION_SEQUENCE;
    g_undo_v2_runtime.tx_open = 1U;
    undo_v2_set_status(UNDO_V2_STATUS_OK);
    return g_undo_v2_runtime.last_status;
}

undo_v2_status_t undo_v2_commit_sequence_transaction(void)
{
    if ((g_undo_v2_runtime.tx_open == 0U)
            || (g_undo_v2_runtime.pending_kind != (uint8_t)UNDO_V2_TRANSACTION_SEQUENCE))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_NO_TX);
        return g_undo_v2_runtime.last_status;
    }

    const uint8_t change_state = undo_v2_pending_has_effective_change();
    if (change_state == 2U)
    {
        undo_v2_cancel_transaction();
        undo_v2_set_status(UNDO_V2_STATUS_ERR_APPLY_FAILED);
        return g_undo_v2_runtime.last_status;
    }

    if (change_state == 0U)
    {
        undo_v2_cancel_transaction();
        undo_v2_set_status(UNDO_V2_STATUS_OK);
        return g_undo_v2_runtime.last_status;
    }

    undo_v2_purge_redo_history();
    if (g_undo_v2_runtime.undo_count >= UNDO_V2_MAX_TRANSACTIONS)
    {
        undo_v2_discard_oldest_transaction();
    }

    const uint8_t slot = undo_v2_history_tail_index();
    undo_v2_transaction_t *const transaction = &g_undo_v2_transactions[slot];
    transaction->used = 1U;
    transaction->kind = (uint8_t)UNDO_V2_TRANSACTION_SEQUENCE;
    transaction->track = g_undo_v2_runtime.pending_track;
    transaction->reserved = 0U;
    if (undo_v2_copy_snapshot_list(&transaction->snapshot,
                                   &g_undo_v2_pending_snapshot) == 0U)
    {
        undo_v2_release_transaction(transaction);
        undo_v2_cancel_transaction();
        undo_v2_set_status(UNDO_V2_STATUS_ERR_APPLY_FAILED);
        return g_undo_v2_runtime.last_status;
    }
    g_undo_v2_runtime.tx_open = 0U;
    g_undo_v2_runtime.undo_count++;
    undo_v2_clear_pending();
    undo_v2_set_status(UNDO_V2_STATUS_OK);
    return g_undo_v2_runtime.last_status;
}

undo_v2_status_t undo_v2_begin_note_fx_transaction(void)
{
    if ((g_undo_v2_runtime.tx_open != 0U)
            || (undo_v2_capture_allowed() == 0U))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_INVALID_ARG);
        return g_undo_v2_runtime.last_status;
    }

    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        if (note_fx_state_capture_track(track, &g_undo_v2_pending_note_fx[track]) == 0U)
        {
            undo_v2_clear_pending();
            undo_v2_set_status(UNDO_V2_STATUS_ERR_CAPTURE_BLOCKED);
            return g_undo_v2_runtime.last_status;
        }
    }

    g_undo_v2_runtime.pending_track = 0U;
    g_undo_v2_runtime.pending_kind = (uint8_t)UNDO_V2_TRANSACTION_NOTE_FX;
    g_undo_v2_runtime.tx_open = 1U;
    undo_v2_set_status(UNDO_V2_STATUS_OK);
    return g_undo_v2_runtime.last_status;
}

undo_v2_status_t undo_v2_commit_note_fx_transaction(void)
{
    if ((g_undo_v2_runtime.tx_open == 0U)
            || (g_undo_v2_runtime.pending_kind != (uint8_t)UNDO_V2_TRANSACTION_NOTE_FX))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_NO_TX);
        return g_undo_v2_runtime.last_status;
    }

    const uint8_t change_state = undo_v2_note_fx_pending_has_effective_change();
    if (change_state == 2U)
    {
        undo_v2_cancel_transaction();
        undo_v2_set_status(UNDO_V2_STATUS_ERR_APPLY_FAILED);
        return g_undo_v2_runtime.last_status;
    }

    if (change_state == 0U)
    {
        undo_v2_cancel_transaction();
        undo_v2_set_status(UNDO_V2_STATUS_OK);
        return g_undo_v2_runtime.last_status;
    }

    undo_v2_purge_redo_history();
    if (g_undo_v2_runtime.undo_count >= UNDO_V2_MAX_TRANSACTIONS)
    {
        undo_v2_discard_oldest_transaction();
    }

    const uint8_t slot = undo_v2_history_tail_index();
    undo_v2_transaction_t *const transaction = &g_undo_v2_transactions[slot];
    transaction->used = 1U;
    transaction->kind = (uint8_t)UNDO_V2_TRANSACTION_NOTE_FX;
    transaction->track = 0U;
    transaction->reserved = 0U;
    memcpy(transaction->note_fx,
           g_undo_v2_pending_note_fx,
           sizeof(transaction->note_fx));
    transaction->snapshot.count = 0U;
    g_undo_v2_runtime.tx_open = 0U;
    g_undo_v2_runtime.undo_count++;
    undo_v2_clear_pending();
    undo_v2_set_status(UNDO_V2_STATUS_OK);
    return g_undo_v2_runtime.last_status;
}

void undo_v2_cancel_transaction(void)
{
    if (g_undo_v2_runtime.tx_open != 0U)
    {
        g_undo_v2_runtime.tx_open = 0U;
        undo_v2_clear_pending();
    }
    undo_v2_set_status(UNDO_V2_STATUS_OK);
}

undo_v2_status_t undo_v2_undo(void)
{
    if ((g_undo_v2_runtime.tx_open != 0U) || (g_undo_v2_runtime.undo_count == 0U))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_NO_TX);
        return g_undo_v2_runtime.last_status;
    }

    undo_v2_transaction_t *const transaction =
        &g_undo_v2_transactions[undo_v2_history_top_undo_index()];
    g_undo_v2_runtime.apply_in_progress = 1U;
    undo_v2_status_t status = UNDO_V2_STATUS_ERR_UNSUPPORTED;
    if (transaction->kind == (uint8_t)UNDO_V2_TRANSACTION_SEQUENCE)
    {
        status = undo_v2_exchange_transaction(transaction);
    }
    else if (transaction->kind == (uint8_t)UNDO_V2_TRANSACTION_NOTE_FX)
    {
        status = undo_v2_exchange_note_fx_transaction(transaction);
    }
    g_undo_v2_runtime.apply_in_progress = 0U;
    if (status == UNDO_V2_STATUS_OK)
    {
        g_undo_v2_runtime.undo_count--;
        g_undo_v2_runtime.redo_count++;
    }
    undo_v2_set_status(status);
    return g_undo_v2_runtime.last_status;
}

undo_v2_status_t undo_v2_redo(void)
{
    if ((g_undo_v2_runtime.tx_open != 0U) || (g_undo_v2_runtime.redo_count == 0U))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_NO_TX);
        return g_undo_v2_runtime.last_status;
    }

    undo_v2_transaction_t *const transaction =
        &g_undo_v2_transactions[undo_v2_history_top_redo_index()];
    g_undo_v2_runtime.apply_in_progress = 1U;
    undo_v2_status_t status = UNDO_V2_STATUS_ERR_UNSUPPORTED;
    if (transaction->kind == (uint8_t)UNDO_V2_TRANSACTION_SEQUENCE)
    {
        status = undo_v2_exchange_transaction(transaction);
    }
    else if (transaction->kind == (uint8_t)UNDO_V2_TRANSACTION_NOTE_FX)
    {
        status = undo_v2_exchange_note_fx_transaction(transaction);
    }
    g_undo_v2_runtime.apply_in_progress = 0U;
    if (status == UNDO_V2_STATUS_OK)
    {
        g_undo_v2_runtime.redo_count--;
        g_undo_v2_runtime.undo_count++;
    }
    undo_v2_set_status(status);
    return g_undo_v2_runtime.last_status;
}

void undo_v2_set_capture_suspended(uint8_t suspended)
{
    g_undo_v2_runtime.capture_suspended = (suspended != 0U) ? 1U : 0U;
    undo_v2_set_status(UNDO_V2_STATUS_OK);
}
