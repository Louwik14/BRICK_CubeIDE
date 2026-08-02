/*
 * Module: undo_v2
 * Role: Undo/Redo cible sur les mutations structurelles de steps.
 * Responsibilities: anneau fixe de huit transactions, validation d'identite
 * de piste, echange d'une image canonique de steps et invalidation atomique.
 */
#include "Storage/undo_v2.h"

#include <string.h>

#include "Core/engine_tasklet.h"
#include "Core/track_topology.h"
#include "Seq/seq_edit.h"
#include "Storage/memory_layout.h"
#include "main.h"

typedef struct
{
    uint8_t used;
    uint8_t track;
    uint8_t reserved;
    track_topology_identity_t track_identity;
    seq_step_snapshot_list_t snapshot;
} undo_v2_sequence_transaction_t;

typedef struct
{
    uint8_t tx_open;
    uint8_t undo_count;
    uint8_t redo_count;
    uint8_t oldest_index;
    uint8_t apply_in_progress;
    uint8_t capture_suspended;
    uint8_t pending_track;
    track_topology_identity_t pending_track_identity;
    undo_v2_status_t last_status;
} undo_v2_runtime_t;

UI_SDRAM static undo_v2_runtime_t g_undo_v2_runtime;
UI_SDRAM static undo_v2_sequence_transaction_t g_undo_v2_transactions[UNDO_V2_MAX_TRANSACTIONS];
UI_SDRAM static seq_step_snapshot_list_t g_undo_v2_pending_snapshot;
UI_SDRAM static seq_step_snapshot_t g_undo_v2_exchange_step;

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

static void undo_v2_release_transaction(undo_v2_sequence_transaction_t *transaction)
{
    if (transaction != 0)
    {
        memset(transaction, 0, sizeof(*transaction));
    }
}

static void undo_v2_clear_pending(void)
{
    memset(&g_undo_v2_pending_snapshot, 0, sizeof(g_undo_v2_pending_snapshot));
    g_undo_v2_runtime.pending_track = 0U;
    memset(&g_undo_v2_runtime.pending_track_identity,
           0,
           sizeof(g_undo_v2_runtime.pending_track_identity));
}

static void undo_v2_purge_redo_history(void)
{
    while (g_undo_v2_runtime.redo_count != 0U)
    {
        undo_v2_sequence_transaction_t *const transaction =
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

static uint8_t undo_v2_transaction_identity_is_current(
    const undo_v2_sequence_transaction_t *transaction)
{
    if ((transaction == 0)
            || (transaction->used == 0U)
            || (track_topology_is_active(transaction->track) == 0U)
            || (seq_edit_track_sequence_is_locked(transaction->track) != 0U)
            || (track_topology_identity_is_compatible(transaction->track,
                                                       &transaction->track_identity) == 0U))
    {
        return 0U;
    }

    return 1U;
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
    undo_v2_sequence_transaction_t *transaction)
{
    if (undo_v2_transaction_identity_is_current(transaction) == 0U)
    {
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    }

    if (seq_step_snapshot_can_apply_list(transaction->track,
                                         &transaction->snapshot) == 0U)
    {
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    }

    memset(&g_undo_v2_pending_snapshot, 0, sizeof(g_undo_v2_pending_snapshot));
    g_undo_v2_pending_snapshot.count = transaction->snapshot.count;
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
    transaction->snapshot = g_undo_v2_pending_snapshot;
    memset(&g_undo_v2_pending_snapshot, 0, sizeof(g_undo_v2_pending_snapshot));

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
    undo_v2_set_status(UNDO_V2_STATUS_OK);
}

undo_v2_status_t undo_v2_begin_sequence_transaction(seq_track_id_t track,
                                                    const seq_step_id_t *steps,
                                                    uint8_t step_count)
{
    if ((g_undo_v2_runtime.tx_open != 0U)
            || (undo_v2_capture_allowed() == 0U)
            || (track_topology_is_active(track) == 0U)
            || (steps == 0)
            || (step_count == 0U)
            || (step_count > (uint8_t)SEQ_STEP_SNAPSHOT_MAX_STEPS)
            || (seq_edit_track_sequence_is_locked(track) != 0U)
            || (track_topology_get_identity(track,
                                             &g_undo_v2_runtime.pending_track_identity) == 0U))
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
    g_undo_v2_runtime.tx_open = 1U;
    undo_v2_set_status(UNDO_V2_STATUS_OK);
    return g_undo_v2_runtime.last_status;
}

undo_v2_status_t undo_v2_commit_sequence_transaction(void)
{
    if (g_undo_v2_runtime.tx_open == 0U)
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
    undo_v2_sequence_transaction_t *const transaction = &g_undo_v2_transactions[slot];
    memset(transaction, 0, sizeof(*transaction));
    transaction->used = 1U;
    transaction->track = g_undo_v2_runtime.pending_track;
    transaction->track_identity = g_undo_v2_runtime.pending_track_identity;
    transaction->snapshot = g_undo_v2_pending_snapshot;
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

    undo_v2_sequence_transaction_t *const transaction =
        &g_undo_v2_transactions[undo_v2_history_top_undo_index()];
    g_undo_v2_runtime.apply_in_progress = 1U;
    const undo_v2_status_t status = undo_v2_exchange_transaction(transaction);
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

    undo_v2_sequence_transaction_t *const transaction =
        &g_undo_v2_transactions[undo_v2_history_top_redo_index()];
    g_undo_v2_runtime.apply_in_progress = 1U;
    const undo_v2_status_t status = undo_v2_exchange_transaction(transaction);
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
