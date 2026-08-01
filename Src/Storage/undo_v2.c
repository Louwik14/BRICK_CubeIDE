#include "Storage/undo_v2.h"

#include <string.h>

#include "Core/engine_tasklet.h"
#include "Param/param_registry.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_runtime_control.h"
#include "Keyboard/keyboard_runtime.h"
#include "NoteFx/note_fx_state.h"
#include "NoteFx/note_fx_pipeline.h"
#include "Storage/memory_layout.h"
#include "main.h"

typedef struct
{
    uint8_t tx_open;
    uint8_t tx_index;
    uint8_t undo_count;
    uint8_t redo_count;
    uint8_t oldest_index;
    uint8_t apply_in_progress;
    uint8_t capture_suspended;
    undo_v2_status_t last_status;
} undo_v2_runtime_t;

UI_SDRAM static undo_v2_runtime_t g_undo_v2_runtime;
UI_SDRAM static undo_v2_tx_entry_t g_undo_v2_transactions[UNDO_V2_MAX_TRANSACTIONS];
UI_SDRAM static undo_v2_param_delta_t g_undo_v2_param_deltas[UNDO_V2_MAX_PARAM_DELTAS];
UI_SDRAM static undo_v2_plock_delta_t g_undo_v2_plock_deltas[UNDO_V2_MAX_PLOCK_DELTAS];
UI_SDRAM static undo_v2_step_delta_t g_undo_v2_step_deltas[UNDO_V2_MAX_STEP_DELTAS];
UI_SDRAM static undo_v2_snapshot_payload_t g_undo_v2_snapshots[UNDO_V2_MAX_SNAPSHOTS];

#define UNDO_V2_INVALID_INDEX 0xFFFFU

static uint16_t g_undo_v2_param_delta_next_free[UNDO_V2_MAX_PARAM_DELTAS];
static uint16_t g_undo_v2_plock_delta_next_free[UNDO_V2_MAX_PLOCK_DELTAS];
static uint16_t g_undo_v2_step_delta_next_free[UNDO_V2_MAX_STEP_DELTAS];
static uint16_t g_undo_v2_snapshot_next_free[UNDO_V2_MAX_SNAPSHOTS];

static uint16_t g_undo_v2_param_delta_head_free;
static uint16_t g_undo_v2_plock_delta_head_free;
static uint16_t g_undo_v2_step_delta_head_free;
static uint16_t g_undo_v2_snapshot_head_free;

static uint8_t undo_v2_param_delta_index_is_valid(uint16_t index)
{
    return (index < (uint16_t)UNDO_V2_MAX_PARAM_DELTAS) ? 1U : 0U;
}

static uint8_t undo_v2_plock_delta_index_is_valid(uint16_t index)
{
    return (index < (uint16_t)UNDO_V2_MAX_PLOCK_DELTAS) ? 1U : 0U;
}

static uint8_t undo_v2_step_delta_index_is_valid(uint16_t index)
{
    return (index < (uint16_t)UNDO_V2_MAX_STEP_DELTAS) ? 1U : 0U;
}

static undo_v2_tx_entry_t *undo_v2_current_tx(void)
{
    if (g_undo_v2_runtime.tx_open == 0U)
    {
        return 0;
    }

    return &g_undo_v2_transactions[g_undo_v2_runtime.tx_index];
}

static void undo_v2_set_status(undo_v2_status_t status)
{
    g_undo_v2_runtime.last_status = status;
}

static uint8_t undo_v2_history_tail_index(void)
{
    return (uint8_t)((g_undo_v2_runtime.oldest_index
                      + g_undo_v2_runtime.undo_count
                      + g_undo_v2_runtime.redo_count) % UNDO_V2_MAX_TRANSACTIONS);
}

static uint8_t undo_v2_history_total_count(void)
{
    return (uint8_t)(g_undo_v2_runtime.undo_count + g_undo_v2_runtime.redo_count);
}

static uint8_t undo_v2_history_top_undo_index(void)
{
    return (uint8_t)((g_undo_v2_runtime.oldest_index + g_undo_v2_runtime.undo_count - 1U) % UNDO_V2_MAX_TRANSACTIONS);
}

static uint8_t undo_v2_history_top_redo_index(void)
{
    return (uint8_t)((g_undo_v2_runtime.oldest_index + g_undo_v2_runtime.undo_count) % UNDO_V2_MAX_TRANSACTIONS);
}

static void undo_v2_release_transaction_payload(undo_v2_tx_entry_t *tx)
{
    if (tx == 0)
    {
        return;
    }

    if (tx->mode == UNDO_V2_TX_MODE_DELTA)
    {
        uint16_t index = tx->payload_index;
        for (uint16_t i = 0U; (i < tx->payload_count) && (index != UNDO_V2_INVALID_INDEX); ++i)
        {
            uint16_t next = UNDO_V2_INVALID_INDEX;

            if (tx->kind == UNDO_V2_TX_KIND_PARAM)
            {
                if (undo_v2_param_delta_index_is_valid(index) == 0U)
                {
                    break;
                }
                next = g_undo_v2_param_delta_next_free[index];
                g_undo_v2_param_deltas[index].used = 0U;
                g_undo_v2_param_delta_next_free[index] = g_undo_v2_param_delta_head_free;
                g_undo_v2_param_delta_head_free = index;
            }
            else if (tx->kind == UNDO_V2_TX_KIND_PLOCK)
            {
                if (undo_v2_plock_delta_index_is_valid(index) == 0U)
                {
                    break;
                }
                next = g_undo_v2_plock_delta_next_free[index];
                g_undo_v2_plock_deltas[index].used = 0U;
                g_undo_v2_plock_delta_next_free[index] = g_undo_v2_plock_delta_head_free;
                g_undo_v2_plock_delta_head_free = index;
            }
            else if (tx->kind == UNDO_V2_TX_KIND_STEP)
            {
                if (undo_v2_step_delta_index_is_valid(index) == 0U)
                {
                    break;
                }
                next = g_undo_v2_step_delta_next_free[index];
                g_undo_v2_step_deltas[index].used = 0U;
                g_undo_v2_step_delta_next_free[index] = g_undo_v2_step_delta_head_free;
                g_undo_v2_step_delta_head_free = index;
            }

            index = next;
        }
    }
    else if ((tx->kind == UNDO_V2_TX_KIND_SNAPSHOT)
             && (tx->mode == UNDO_V2_TX_MODE_SNAPSHOT)
             && (tx->payload_index < UNDO_V2_MAX_SNAPSHOTS))
    {
        memset(&g_undo_v2_snapshots[tx->payload_index], 0, sizeof(g_undo_v2_snapshots[tx->payload_index]));
        g_undo_v2_snapshot_next_free[tx->payload_index] = g_undo_v2_snapshot_head_free;
        g_undo_v2_snapshot_head_free = tx->payload_index;
    }

    tx->payload_index = UNDO_V2_INVALID_INDEX;
    tx->payload_count = 0U;
    tx->committed = 0U;
}

static void undo_v2_purge_redo_history(void)
{
    while (g_undo_v2_runtime.redo_count != 0U)
    {
        undo_v2_tx_entry_t *const tx = &g_undo_v2_transactions[undo_v2_history_top_redo_index()];
        undo_v2_release_transaction_payload(tx);
        memset(tx, 0, sizeof(*tx));
        g_undo_v2_runtime.redo_count--;
    }
}

static uint16_t undo_v2_param_delta_alloc(void)
{
    const uint16_t index = g_undo_v2_param_delta_head_free;
    if ((index == UNDO_V2_INVALID_INDEX) || (undo_v2_param_delta_index_is_valid(index) == 0U))
    {
        g_undo_v2_param_delta_head_free = UNDO_V2_INVALID_INDEX;
        return UNDO_V2_INVALID_INDEX;
    }

    g_undo_v2_param_delta_head_free = g_undo_v2_param_delta_next_free[index];
    g_undo_v2_param_delta_next_free[index] = UNDO_V2_INVALID_INDEX;
    return index;
}

static uint16_t undo_v2_plock_delta_alloc(void)
{
    const uint16_t index = g_undo_v2_plock_delta_head_free;
    if ((index == UNDO_V2_INVALID_INDEX) || (undo_v2_plock_delta_index_is_valid(index) == 0U))
    {
        g_undo_v2_plock_delta_head_free = UNDO_V2_INVALID_INDEX;
        return UNDO_V2_INVALID_INDEX;
    }

    g_undo_v2_plock_delta_head_free = g_undo_v2_plock_delta_next_free[index];
    g_undo_v2_plock_delta_next_free[index] = UNDO_V2_INVALID_INDEX;
    return index;
}

static uint16_t undo_v2_step_delta_alloc(void)
{
    const uint16_t index = g_undo_v2_step_delta_head_free;
    if ((index == UNDO_V2_INVALID_INDEX) || (undo_v2_step_delta_index_is_valid(index) == 0U))
    {
        g_undo_v2_step_delta_head_free = UNDO_V2_INVALID_INDEX;
        return UNDO_V2_INVALID_INDEX;
    }

    g_undo_v2_step_delta_head_free = g_undo_v2_step_delta_next_free[index];
    g_undo_v2_step_delta_next_free[index] = UNDO_V2_INVALID_INDEX;
    return index;
}

static uint16_t undo_v2_snapshot_alloc(void)
{
    const uint16_t index = g_undo_v2_snapshot_head_free;
    if (index == UNDO_V2_INVALID_INDEX)
    {
        return UNDO_V2_INVALID_INDEX;
    }

    g_undo_v2_snapshot_head_free = g_undo_v2_snapshot_next_free[index];
    g_undo_v2_snapshot_next_free[index] = UNDO_V2_INVALID_INDEX;
    memset(&g_undo_v2_snapshots[index], 0, sizeof(g_undo_v2_snapshots[index]));
    return index;
}

static void undo_v2_discard_oldest_committed(void)
{
    if (undo_v2_history_total_count() == 0U)
    {
        return;
    }

    undo_v2_tx_entry_t *const tx = &g_undo_v2_transactions[g_undo_v2_runtime.oldest_index];
    undo_v2_release_transaction_payload(tx);
    memset(tx, 0, sizeof(*tx));
    g_undo_v2_runtime.oldest_index = (uint8_t)((g_undo_v2_runtime.oldest_index + 1U) % UNDO_V2_MAX_TRANSACTIONS);
    if (g_undo_v2_runtime.undo_count != 0U)
    {
        g_undo_v2_runtime.undo_count--;
    }
    else if (g_undo_v2_runtime.redo_count != 0U)
    {
        g_undo_v2_runtime.redo_count--;
    }
}

static uint8_t undo_v2_prepare_commit_slot(void)
{
    if (undo_v2_history_total_count() >= UNDO_V2_MAX_TRANSACTIONS)
    {
        undo_v2_discard_oldest_committed();
    }

    return undo_v2_history_tail_index();
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

static undo_v2_param_delta_t *undo_v2_find_param_delta(undo_v2_tx_entry_t *tx,
                                                        param_id_t param_id,
                                                        uint8_t is_track_aware,
                                                        uint8_t track)
{
    if ((tx == 0) || (tx->kind != UNDO_V2_TX_KIND_PARAM) || (tx->mode != UNDO_V2_TX_MODE_DELTA))
    {
        return 0;
    }

    uint16_t index = tx->payload_index;
    for (uint16_t i = 0U; (i < tx->payload_count) && (index != UNDO_V2_INVALID_INDEX); ++i)
    {
        if (undo_v2_param_delta_index_is_valid(index) == 0U)
        {
            return 0;
        }
        undo_v2_param_delta_t *const delta = &g_undo_v2_param_deltas[index];
        if ((delta->used != 0U)
            && (delta->param_id == param_id)
            && (delta->is_track_aware == is_track_aware)
            && (delta->track == track))
        {
            return delta;
        }

        index = g_undo_v2_param_delta_next_free[index];
    }

    return 0;
}

static undo_v2_plock_delta_t *undo_v2_find_plock_delta(undo_v2_tx_entry_t *tx,
                                                        uint8_t track,
                                                        uint8_t step,
                                                        uint8_t set_id,
                                                        uint8_t param_slot)
{
    if ((tx == 0) || (tx->kind != UNDO_V2_TX_KIND_PLOCK) || (tx->mode != UNDO_V2_TX_MODE_DELTA))
    {
        return 0;
    }

    uint16_t index = tx->payload_index;
    for (uint16_t i = 0U; (i < tx->payload_count) && (index != UNDO_V2_INVALID_INDEX); ++i)
    {
        if (undo_v2_plock_delta_index_is_valid(index) == 0U)
        {
            return 0;
        }
        undo_v2_plock_delta_t *const delta = &g_undo_v2_plock_deltas[index];
        if ((delta->used != 0U)
            && (delta->track == track)
            && (delta->step == step)
            && (delta->set_id == set_id)
            && (delta->param_slot == param_slot))
        {
            return delta;
        }

        index = g_undo_v2_plock_delta_next_free[index];
    }

    return 0;
}

static undo_v2_step_delta_t *undo_v2_find_step_delta(undo_v2_tx_entry_t *tx,
                                                      uint8_t track,
                                                      uint8_t step,
                                                      uint8_t field_id)
{
    if ((tx == 0) || (tx->kind != UNDO_V2_TX_KIND_STEP) || (tx->mode != UNDO_V2_TX_MODE_DELTA))
    {
        return 0;
    }

    uint16_t index = tx->payload_index;
    for (uint16_t i = 0U; (i < tx->payload_count) && (index != UNDO_V2_INVALID_INDEX); ++i)
    {
        if (undo_v2_step_delta_index_is_valid(index) == 0U)
        {
            return 0;
        }
        undo_v2_step_delta_t *const delta = &g_undo_v2_step_deltas[index];
        if ((delta->used != 0U)
            && (delta->track == track)
            && (delta->step == step)
            && (delta->field_id == field_id))
        {
            return delta;
        }

        index = g_undo_v2_step_delta_next_free[index];
    }

    return 0;
}

static uint8_t undo_v2_apply_param_value(const undo_v2_param_delta_t *delta, uint8_t use_after)
{
    if ((delta == 0) || (delta->used == 0U))
    {
        return 0U;
    }

    const float value = (use_after != 0U) ? delta->after : delta->before;
    if (delta->is_track_aware == 0U)
    {
        param_set(delta->param_id, value);
        return 1U;
    }

    switch (delta->param_id)
    {
        case PARAM_SEQ_LENGTH:
            if (seq_edit_track_sequence_is_locked((seq_track_id_t)delta->track) != 0U)
            {
                return 0U;
            }
            seq_model_set_track_length(delta->track, (uint8_t)(value + 0.5f));
            seq_runtime_on_track_length_changed(delta->track);
            return 1U;
        case PARAM_SEQ_DIV:
            if (seq_edit_track_sequence_is_locked((seq_track_id_t)delta->track) != 0U)
            {
                return 0U;
            }
            seq_runtime_set_track_div(delta->track, (value < 0.5f) ? 1U : (value < 1.5f) ? 2U : (value < 2.5f) ? 4U : 8U);
            return 1U;
        case PARAM_SEQ_QUANT:
            if (seq_edit_track_sequence_is_locked((seq_track_id_t)delta->track) != 0U)
            {
                return 0U;
            }
            seq_runtime_set_track_quant(delta->track, (uint8_t)(value + 0.5f));
            return 1U;
        case PARAM_SEQ_SWING:
            if (seq_edit_track_sequence_is_locked((seq_track_id_t)delta->track) != 0U)
            {
                return 0U;
            }
            seq_runtime_set_track_swing(delta->track, (uint8_t)(value + 0.5f));
            return 1U;
        default:
            break;
    }

    return param_registry_apply_track_value(delta->param_id, delta->track, value);
}

static undo_v2_status_t undo_v2_apply_param_transaction(const undo_v2_tx_entry_t *tx, uint8_t use_after)
{
    if ((tx == 0) || (tx->kind != UNDO_V2_TX_KIND_PARAM) || (tx->mode != UNDO_V2_TX_MODE_DELTA))
    {
        return UNDO_V2_STATUS_ERR_UNSUPPORTED;
    }

    uint16_t indices[UNDO_V2_MAX_PARAM_DELTAS];
    uint16_t count = 0U;
    uint16_t index = tx->payload_index;
    while ((count < tx->payload_count) && (count < UNDO_V2_MAX_PARAM_DELTAS) && (index != UNDO_V2_INVALID_INDEX))
    {
        if (undo_v2_param_delta_index_is_valid(index) == 0U)
        {
            return UNDO_V2_STATUS_ERR_APPLY_FAILED;
        }
        indices[count++] = index;
        index = g_undo_v2_param_delta_next_free[index];
    }

    for (uint16_t i = 0U; i < count; ++i)
    {
        const uint16_t offset = (use_after != 0U) ? i : (uint16_t)(count - 1U - i);
        const undo_v2_param_delta_t *const delta = &g_undo_v2_param_deltas[indices[offset]];
        if (undo_v2_apply_param_value(delta, use_after) == 0U)
        {
            return UNDO_V2_STATUS_ERR_APPLY_FAILED;
        }
    }

    return UNDO_V2_STATUS_OK;
}

static uint8_t undo_v2_locked_sequence_snapshot_matches_current(const PatternSaveV1 *snapshot)
{
    if (snapshot == 0)
    {
        return 0U;
    }

    for (seq_track_id_t track = 0U;
         track < (seq_track_id_t)track_topology_get_logical_track_count();
         ++track)
    {
        if (seq_edit_track_sequence_is_locked(track) == 0U)
        {
            continue;
        }

        const uint8_t is_play = track_topology_is_play(track);
        const pattern_v1_play_track_seq_t *const play = is_play
            ? &snapshot->seq.play[track] : 0;
        const pattern_v1_special_track_seq_t *const special = is_play ? 0
            : &snapshot->seq.special[track - TRACK_TOPOLOGY_PLAY_TRACK_COUNT];
        const uint8_t saved_length = is_play ? play->length_steps : special->length_steps;
        if (saved_length != seq_model_get_track_length(track))
        {
            return 0U;
        }

        uint8_t runtime_value = 0U;
        if ((seq_runtime_get_track_div(track, &runtime_value) == 0U)
                || (snapshot->globals.track_div[track] != runtime_value)
                || (seq_runtime_get_track_quant(track, &runtime_value) == 0U)
                || (snapshot->globals.track_quant[track] != runtime_value)
                || (seq_runtime_get_track_swing(track, &runtime_value) == 0U)
                || (snapshot->globals.track_swing[track] != runtime_value))
        {
            return 0U;
        }

        for (seq_step_id_t step = 0U; step < (seq_step_id_t)SEQ_MAX_STEPS; ++step)
        {
            const pattern_v1_play_step_t *const play_step = is_play ? &play->steps[step] : 0;
            const pattern_v1_special_step_t *const special_step = is_play ? 0 : &special->steps[step];
            if (((is_play != 0U)
                    && ((play_step->trig != seq_model_get_trig(track, step))
                        || (play_step->roll != seq_model_get_step_roll(track, step))))
                    || ((is_play == 0U)
                        && (special_step->action != seq_model_get_special_action(track, step))))
            {
                return 0U;
            }

            seq_plock_entry_t current_locks[SEQ_STEP_MAX_LOCKS];
            uint8_t current_count = 0U;
            const uint8_t saved_count = is_play ? play_step->lock_count : special_step->lock_count;
            if ((seq_model_step_plock_collect(track,
                                              step,
                                              current_locks,
                                              seq_model_get_step_lock_limit(track),
                                              &current_count) == 0U)
                    || (saved_count != current_count))
            {
                return 0U;
            }

            for (uint8_t i = 0U; i < current_count; ++i)
            {
                const pattern_v1_plock_t *const snap_lock = is_play
                    ? &play_step->locks[i] : &special_step->locks[i];
                const seq_plock_entry_t *const current_lock = &current_locks[i];
                if ((snap_lock->set_id != current_lock->set_id)
                        || (snap_lock->param_slot != current_lock->param_slot)
                        || (snap_lock->value16 != current_lock->value16)
                        || (snap_lock->flags != current_lock->flags))
                {
                    return 0U;
                }
            }
        }
    }

    return 1U;
}

static undo_v2_status_t undo_v2_apply_snapshot_transaction(const undo_v2_tx_entry_t *tx, uint8_t use_after)
{
    if ((tx == 0) || (tx->kind != UNDO_V2_TX_KIND_SNAPSHOT) || (tx->mode != UNDO_V2_TX_MODE_SNAPSHOT))
    {
        return UNDO_V2_STATUS_ERR_UNSUPPORTED;
    }

    if (tx->payload_index >= UNDO_V2_MAX_SNAPSHOTS)
    {
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    }

    const undo_v2_snapshot_payload_t *const payload = &g_undo_v2_snapshots[tx->payload_index];
    const PatternSaveV1 *snapshot = 0;

    if (use_after != 0U)
    {
        if (payload->after_valid == 0U)
        {
            return UNDO_V2_STATUS_ERR_APPLY_FAILED;
        }
        snapshot = &payload->after_snapshot;
    }
    else
    {
        if (payload->before_valid == 0U)
        {
            return UNDO_V2_STATUS_ERR_APPLY_FAILED;
        }
        snapshot = &payload->before_snapshot;
    }

    if (undo_v2_locked_sequence_snapshot_matches_current(snapshot) == 0U)
    {
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    }

    if (pattern_live_apply_snapshot(snapshot, 0U) == 0U)
    {
        return UNDO_V2_STATUS_ERR_APPLY_FAILED;
    }

    const note_fx_track_state_t *const note_fx = (use_after != 0U)
        ? payload->after_note_fx : payload->before_note_fx;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        note_fx_pipeline_reset_runtime_overrides(track);
        if (note_fx_state_restore_track(track, &note_fx[track]) == 0U)
        {
            return UNDO_V2_STATUS_ERR_APPLY_FAILED;
        }
    }
    return UNDO_V2_STATUS_OK;
}

static undo_v2_status_t undo_v2_apply_plock_transaction(const undo_v2_tx_entry_t *tx, uint8_t use_after)
{
    if ((tx == 0) || (tx->kind != UNDO_V2_TX_KIND_PLOCK) || (tx->mode != UNDO_V2_TX_MODE_DELTA))
    {
        return UNDO_V2_STATUS_ERR_UNSUPPORTED;
    }

    uint16_t indices[UNDO_V2_MAX_PLOCK_DELTAS];
    uint16_t count = 0U;
    uint16_t index = tx->payload_index;
    while ((count < tx->payload_count) && (count < UNDO_V2_MAX_PLOCK_DELTAS) && (index != UNDO_V2_INVALID_INDEX))
    {
        if (undo_v2_plock_delta_index_is_valid(index) == 0U)
        {
            return UNDO_V2_STATUS_ERR_APPLY_FAILED;
        }
        indices[count++] = index;
        index = g_undo_v2_plock_delta_next_free[index];
    }

    for (uint16_t i = 0U; i < count; ++i)
    {
        const uint16_t offset = (use_after != 0U) ? i : (uint16_t)(count - 1U - i);
        const undo_v2_plock_delta_t *const delta = &g_undo_v2_plock_deltas[indices[offset]];
        const uint8_t present = (use_after != 0U) ? delta->after_present : delta->before_present;
        const uint16_t value16 = (use_after != 0U) ? delta->after_value16 : delta->before_value16;
        const uint8_t flags = (use_after != 0U) ? delta->after_flags : delta->before_flags;
        const uint8_t trig_active = (use_after != 0U) ? delta->after_trig : delta->before_trig;
        if (seq_edit_step_plock_apply_state(delta->track,
                                            delta->step,
                                            delta->set_id,
                                            delta->param_slot,
                                            present,
                                            (seq_value16_t)value16,
                                            flags,
                                            trig_active) == 0U)
        {
            return UNDO_V2_STATUS_ERR_APPLY_FAILED;
        }
    }

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
    memset(g_undo_v2_param_deltas, 0, sizeof(g_undo_v2_param_deltas));
    memset(g_undo_v2_plock_deltas, 0, sizeof(g_undo_v2_plock_deltas));
    memset(g_undo_v2_step_deltas, 0, sizeof(g_undo_v2_step_deltas));
    memset(g_undo_v2_snapshots, 0, sizeof(g_undo_v2_snapshots));
    for (uint16_t i = 0U; i < UNDO_V2_MAX_PARAM_DELTAS; ++i)
    {
        g_undo_v2_param_delta_next_free[i] = (i + 1U < UNDO_V2_MAX_PARAM_DELTAS) ? (uint16_t)(i + 1U) : UNDO_V2_INVALID_INDEX;
    }
    for (uint16_t i = 0U; i < UNDO_V2_MAX_PLOCK_DELTAS; ++i)
    {
        g_undo_v2_plock_delta_next_free[i] = (i + 1U < UNDO_V2_MAX_PLOCK_DELTAS) ? (uint16_t)(i + 1U) : UNDO_V2_INVALID_INDEX;
    }
    for (uint16_t i = 0U; i < UNDO_V2_MAX_STEP_DELTAS; ++i)
    {
        g_undo_v2_step_delta_next_free[i] = (i + 1U < UNDO_V2_MAX_STEP_DELTAS) ? (uint16_t)(i + 1U) : UNDO_V2_INVALID_INDEX;
    }
    for (uint16_t i = 0U; i < UNDO_V2_MAX_SNAPSHOTS; ++i)
    {
        g_undo_v2_snapshot_next_free[i] = (i + 1U < UNDO_V2_MAX_SNAPSHOTS) ? (uint16_t)(i + 1U) : UNDO_V2_INVALID_INDEX;
    }

    g_undo_v2_param_delta_head_free = 0U;
    g_undo_v2_plock_delta_head_free = 0U;
    g_undo_v2_step_delta_head_free = 0U;
    g_undo_v2_snapshot_head_free = 0U;

    undo_v2_set_status(UNDO_V2_STATUS_OK);
}

uint8_t undo_v2_param_is_undoable(param_id_t param_id)
{
    switch (param_id)
    {
        case PARAM_CFG_TRACK:
        case PARAM_CFG_TRACK_TYPE:
        case PARAM_CFG_MIDI_CH:
        case PARAM_CFG_MIDI_SRC:
        case PARAM_CFG_POLY_VOICES:
        case PARAM_CFG_POLY_SPREAD:
            return 0U;

        default:
            return (param_id < PARAM_COUNT) ? 1U : 0U;
    }
}

undo_v2_status_t undo_v2_begin_transaction(undo_v2_tx_kind_t kind,
                                           undo_v2_source_t source,
                                           uint32_t gesture_key,
                                           undo_v2_tx_mode_t mode)
{
    if ((kind == UNDO_V2_TX_KIND_NONE)
        || (mode == UNDO_V2_TX_MODE_NONE)
        || (g_undo_v2_runtime.tx_open != 0U))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_INVALID_ARG);
        return g_undo_v2_runtime.last_status;
    }

    if (undo_v2_capture_allowed() == 0U)
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_CAPTURE_BLOCKED);
        return g_undo_v2_runtime.last_status;
    }

    const uint8_t slot = undo_v2_prepare_commit_slot();
    undo_v2_tx_entry_t *const tx = &g_undo_v2_transactions[slot];
    memset(tx, 0, sizeof(*tx));
    tx->mode = mode;
    tx->kind = kind;
    tx->source = source;
    tx->gesture_key = gesture_key;
    tx->begin_tick = engine_tick_count;
    tx->end_tick = engine_tick_count;
    tx->committed = 0U;

    if (mode == UNDO_V2_TX_MODE_DELTA)
    {
        if (kind == UNDO_V2_TX_KIND_PARAM)
        {
            tx->payload_index = UNDO_V2_INVALID_INDEX;
        }
        else if (kind == UNDO_V2_TX_KIND_PLOCK)
        {
            tx->payload_index = UNDO_V2_INVALID_INDEX;
        }
        else if (kind == UNDO_V2_TX_KIND_STEP)
        {
            tx->payload_index = UNDO_V2_INVALID_INDEX;
        }
        else
        {
            undo_v2_set_status(UNDO_V2_STATUS_ERR_UNSUPPORTED);
            return g_undo_v2_runtime.last_status;
        }
    }
    else if ((mode == UNDO_V2_TX_MODE_SNAPSHOT) && (kind == UNDO_V2_TX_KIND_SNAPSHOT))
    {
        const uint16_t snapshot_slot = undo_v2_snapshot_alloc();
        if (snapshot_slot == UNDO_V2_INVALID_INDEX)
        {
            memset(tx, 0, sizeof(*tx));
            undo_v2_set_status(UNDO_V2_STATUS_ERR_OVERFLOW);
            return g_undo_v2_runtime.last_status;
        }

        tx->payload_index = snapshot_slot;
    }
    else
    {
        memset(tx, 0, sizeof(*tx));
        undo_v2_set_status(UNDO_V2_STATUS_ERR_UNSUPPORTED);
        return g_undo_v2_runtime.last_status;
    }

    g_undo_v2_runtime.tx_index = slot;
    g_undo_v2_runtime.tx_open = 1U;
    undo_v2_set_status(UNDO_V2_STATUS_OK);
    return g_undo_v2_runtime.last_status;
}

undo_v2_status_t undo_v2_commit_transaction(void)
{
    undo_v2_tx_entry_t *const tx = undo_v2_current_tx();
    if (tx == 0)
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_NO_TX);
        return g_undo_v2_runtime.last_status;
    }

    if ((tx->mode == UNDO_V2_TX_MODE_DELTA) && (tx->payload_count == 0U))
    {
        undo_v2_cancel_transaction();
        undo_v2_set_status(UNDO_V2_STATUS_OK);
        return g_undo_v2_runtime.last_status;
    }

    tx->end_tick = engine_tick_count;
    tx->committed = 1U;
    g_undo_v2_runtime.tx_open = 0U;
    undo_v2_purge_redo_history();
    g_undo_v2_runtime.undo_count++;
    undo_v2_set_status(UNDO_V2_STATUS_OK);
    return g_undo_v2_runtime.last_status;
}

void undo_v2_cancel_transaction(void)
{
    undo_v2_tx_entry_t *const tx = undo_v2_current_tx();
    if (tx != 0)
    {
        undo_v2_release_transaction_payload(tx);
        memset(tx, 0, sizeof(*tx));
    }

    g_undo_v2_runtime.tx_open = 0U;
    undo_v2_set_status(UNDO_V2_STATUS_OK);
}

undo_v2_status_t undo_v2_record_param_change(param_id_t param_id,
                                             uint8_t is_track_aware,
                                             uint8_t track,
                                             float before,
                                             float after)
{
    undo_v2_tx_entry_t *const tx = undo_v2_current_tx();
    if ((tx == 0) || (tx->kind != UNDO_V2_TX_KIND_PARAM) || (tx->mode != UNDO_V2_TX_MODE_DELTA))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_NO_TX);
        return g_undo_v2_runtime.last_status;
    }

    if (undo_v2_param_is_undoable(param_id) == 0U)
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_UNSUPPORTED);
        return g_undo_v2_runtime.last_status;
    }

    undo_v2_param_delta_t *const existing = undo_v2_find_param_delta(tx, param_id, is_track_aware, track);
    if (existing != 0)
    {
        existing->after = after;
        tx->end_tick = engine_tick_count;
        undo_v2_set_status(UNDO_V2_STATUS_OK);
        return g_undo_v2_runtime.last_status;
    }

    const uint16_t index = undo_v2_param_delta_alloc();
    if (index == UNDO_V2_INVALID_INDEX)
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_OVERFLOW);
        return g_undo_v2_runtime.last_status;
    }

    undo_v2_param_delta_t *const delta = &g_undo_v2_param_deltas[index];
    memset(delta, 0, sizeof(*delta));
    delta->param_id = param_id;
    delta->is_track_aware = is_track_aware;
    delta->track = track;
    delta->before = before;
    delta->after = after;
    delta->used = 1U;
    g_undo_v2_param_delta_next_free[index] = tx->payload_index;
    tx->payload_index = index;
    tx->payload_count++;
    tx->end_tick = engine_tick_count;
    undo_v2_set_status(UNDO_V2_STATUS_OK);
    return g_undo_v2_runtime.last_status;
}

undo_v2_status_t undo_v2_record_plock_change(uint8_t track,
                                             uint8_t step,
                                             uint8_t set_id,
                                             uint8_t param_slot,
                                             uint8_t before_present,
                                             uint16_t before_value16,
                                             uint8_t before_flags,
                                             uint8_t before_trig,
                                             uint8_t after_present,
                                             uint16_t after_value16,
                                             uint8_t after_flags,
                                             uint8_t after_trig)
{
    if ((track >= (uint8_t)SEQ_TRACK_COUNT)
        || (step >= (uint8_t)SEQ_MAX_STEPS)
        || (seq_param_iface_is_param_supported(track, set_id, param_slot) == 0U))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_INVALID_ARG);
        return g_undo_v2_runtime.last_status;
    }

    undo_v2_tx_entry_t *const tx = undo_v2_current_tx();
    if ((tx == 0) || (tx->kind != UNDO_V2_TX_KIND_PLOCK) || (tx->mode != UNDO_V2_TX_MODE_DELTA))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_NO_TX);
        return g_undo_v2_runtime.last_status;
    }

    undo_v2_plock_delta_t *const existing = undo_v2_find_plock_delta(tx, track, step, set_id, param_slot);
    if (existing != 0)
    {
        existing->after_present = after_present;
        existing->after_value16 = after_value16;
        existing->after_flags = after_flags;
        existing->after_trig = after_trig;
        tx->end_tick = engine_tick_count;
        undo_v2_set_status(UNDO_V2_STATUS_OK);
        return g_undo_v2_runtime.last_status;
    }

    const uint16_t index = undo_v2_plock_delta_alloc();
    if (index == UNDO_V2_INVALID_INDEX)
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_OVERFLOW);
        return g_undo_v2_runtime.last_status;
    }

    undo_v2_plock_delta_t *const delta = &g_undo_v2_plock_deltas[index];
    memset(delta, 0, sizeof(*delta));
    delta->track = track;
    delta->step = step;
    delta->set_id = set_id;
    delta->param_slot = param_slot;
    delta->before_present = before_present;
    delta->before_value16 = before_value16;
    delta->before_flags = before_flags;
    delta->before_trig = before_trig;
    delta->after_present = after_present;
    delta->after_value16 = after_value16;
    delta->after_flags = after_flags;
    delta->after_trig = after_trig;
    delta->used = 1U;
    g_undo_v2_plock_delta_next_free[index] = tx->payload_index;
    tx->payload_index = index;
    tx->payload_count++;
    tx->end_tick = engine_tick_count;
    undo_v2_set_status(UNDO_V2_STATUS_OK);
    return g_undo_v2_runtime.last_status;
}

undo_v2_status_t undo_v2_record_step_change(uint8_t track,
                                            uint8_t step,
                                            uint8_t field_id,
                                            uint16_t before_value,
                                            uint16_t after_value)
{
    undo_v2_tx_entry_t *const tx = undo_v2_current_tx();
    if ((tx == 0) || (tx->kind != UNDO_V2_TX_KIND_STEP) || (tx->mode != UNDO_V2_TX_MODE_DELTA))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_NO_TX);
        return g_undo_v2_runtime.last_status;
    }

    undo_v2_step_delta_t *const existing = undo_v2_find_step_delta(tx, track, step, field_id);
    if (existing != 0)
    {
        existing->after_value = after_value;
        tx->end_tick = engine_tick_count;
        undo_v2_set_status(UNDO_V2_STATUS_OK);
        return g_undo_v2_runtime.last_status;
    }

    const uint16_t index = undo_v2_step_delta_alloc();
    if (index == UNDO_V2_INVALID_INDEX)
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_OVERFLOW);
        return g_undo_v2_runtime.last_status;
    }

    undo_v2_step_delta_t *const delta = &g_undo_v2_step_deltas[index];
    memset(delta, 0, sizeof(*delta));
    delta->track = track;
    delta->step = step;
    delta->field_id = field_id;
    delta->before_value = before_value;
    delta->after_value = after_value;
    delta->used = 1U;
    g_undo_v2_step_delta_next_free[index] = tx->payload_index;
    tx->payload_index = index;
    tx->payload_count++;
    tx->end_tick = engine_tick_count;
    undo_v2_set_status(UNDO_V2_STATUS_OK);
    return g_undo_v2_runtime.last_status;
}

undo_v2_status_t undo_v2_begin_snapshot_transaction(undo_v2_source_t source,
                                                    uint32_t gesture_key)
{
    return undo_v2_begin_transaction(UNDO_V2_TX_KIND_SNAPSHOT,
                                     source,
                                     gesture_key,
                                     UNDO_V2_TX_MODE_SNAPSHOT);
}

undo_v2_status_t undo_v2_capture_snapshot_before(void)
{
    undo_v2_tx_entry_t *const tx = undo_v2_current_tx();
    if ((tx == 0) || (tx->kind != UNDO_V2_TX_KIND_SNAPSHOT) || (tx->mode != UNDO_V2_TX_MODE_SNAPSHOT))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_NO_TX);
        return g_undo_v2_runtime.last_status;
    }

    if (tx->payload_index >= UNDO_V2_MAX_SNAPSHOTS)
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_APPLY_FAILED);
        return g_undo_v2_runtime.last_status;
    }

    undo_v2_snapshot_payload_t *const payload = &g_undo_v2_snapshots[tx->payload_index];
    if (pattern_live_capture_current(&payload->before_snapshot) == 0U)
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_APPLY_FAILED);
        return g_undo_v2_runtime.last_status;
    }
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        if (note_fx_state_capture_track(track, &payload->before_note_fx[track]) == 0U)
        {
            undo_v2_set_status(UNDO_V2_STATUS_ERR_APPLY_FAILED);
            return g_undo_v2_runtime.last_status;
        }
    }

    payload->before_valid = 1U;
    tx->end_tick = engine_tick_count;
    undo_v2_set_status(UNDO_V2_STATUS_OK);
    return g_undo_v2_runtime.last_status;
}

undo_v2_status_t undo_v2_capture_snapshot_after(void)
{
    undo_v2_tx_entry_t *const tx = undo_v2_current_tx();
    if ((tx == 0) || (tx->kind != UNDO_V2_TX_KIND_SNAPSHOT) || (tx->mode != UNDO_V2_TX_MODE_SNAPSHOT))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_NO_TX);
        return g_undo_v2_runtime.last_status;
    }

    if (tx->payload_index >= UNDO_V2_MAX_SNAPSHOTS)
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_APPLY_FAILED);
        return g_undo_v2_runtime.last_status;
    }

    undo_v2_snapshot_payload_t *const payload = &g_undo_v2_snapshots[tx->payload_index];
    if (pattern_live_capture_current(&payload->after_snapshot) == 0U)
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_APPLY_FAILED);
        return g_undo_v2_runtime.last_status;
    }
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        if (note_fx_state_capture_track(track, &payload->after_note_fx[track]) == 0U)
        {
            undo_v2_set_status(UNDO_V2_STATUS_ERR_APPLY_FAILED);
            return g_undo_v2_runtime.last_status;
        }
    }

    payload->after_valid = 1U;
    tx->end_tick = engine_tick_count;
    undo_v2_set_status(UNDO_V2_STATUS_OK);
    return g_undo_v2_runtime.last_status;
}

undo_v2_status_t undo_v2_undo(void)
{
    if ((g_undo_v2_runtime.tx_open != 0U) || (g_undo_v2_runtime.undo_count == 0U))
    {
        undo_v2_set_status(UNDO_V2_STATUS_ERR_NO_TX);
        return g_undo_v2_runtime.last_status;
    }

    const uint8_t index = undo_v2_history_top_undo_index();
    const undo_v2_tx_entry_t *const tx = &g_undo_v2_transactions[index];

    g_undo_v2_runtime.apply_in_progress = 1U;
    undo_v2_status_t status = UNDO_V2_STATUS_ERR_UNSUPPORTED;
    if ((tx->kind == UNDO_V2_TX_KIND_PARAM) && (tx->mode == UNDO_V2_TX_MODE_DELTA))
    {
        status = undo_v2_apply_param_transaction(tx, 0U);
    }
    else if ((tx->kind == UNDO_V2_TX_KIND_PLOCK) && (tx->mode == UNDO_V2_TX_MODE_DELTA))
    {
        status = undo_v2_apply_plock_transaction(tx, 0U);
    }
    else if ((tx->kind == UNDO_V2_TX_KIND_SNAPSHOT) && (tx->mode == UNDO_V2_TX_MODE_SNAPSHOT))
    {
        status = undo_v2_apply_snapshot_transaction(tx, 0U);
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

    const uint8_t index = undo_v2_history_top_redo_index();
    const undo_v2_tx_entry_t *const tx = &g_undo_v2_transactions[index];

    g_undo_v2_runtime.apply_in_progress = 1U;
    undo_v2_status_t status = UNDO_V2_STATUS_ERR_UNSUPPORTED;
    if ((tx->kind == UNDO_V2_TX_KIND_PARAM) && (tx->mode == UNDO_V2_TX_MODE_DELTA))
    {
        status = undo_v2_apply_param_transaction(tx, 1U);
    }
    else if ((tx->kind == UNDO_V2_TX_KIND_PLOCK) && (tx->mode == UNDO_V2_TX_MODE_DELTA))
    {
        status = undo_v2_apply_plock_transaction(tx, 1U);
    }
    else if ((tx->kind == UNDO_V2_TX_KIND_SNAPSHOT) && (tx->mode == UNDO_V2_TX_MODE_SNAPSHOT))
    {
        status = undo_v2_apply_snapshot_transaction(tx, 1U);
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

undo_v2_status_t undo_v2_get_last_status(void)
{
    return g_undo_v2_runtime.last_status;
}

uint8_t undo_v2_is_apply_in_progress(void)
{
    return g_undo_v2_runtime.apply_in_progress;
}

uint8_t undo_v2_is_transaction_open(void)
{
    return g_undo_v2_runtime.tx_open;
}

uint8_t undo_v2_is_undo_available(void)
{
    return (g_undo_v2_runtime.undo_count != 0U) ? 1U : 0U;
}

uint8_t undo_v2_is_redo_available(void)
{
    return (g_undo_v2_runtime.redo_count != 0U) ? 1U : 0U;
}

void undo_v2_set_capture_suspended(uint8_t suspended)
{
    g_undo_v2_runtime.capture_suspended = (suspended != 0U) ? 1U : 0U;
    undo_v2_set_status(UNDO_V2_STATUS_OK);
}
