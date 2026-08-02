/*
 * Module: seq_step_snapshot
 * Role: representation canonique d'un pas de sequence pour Clipboard et Undo.
 * Responsibilities: capture/apply deterministe des donnees Play ou Special,
 * validation des p-locks et prevalidation de la capacite du pool.
 */
#include "Seq/seq_step_snapshot.h"

#include <string.h>

#include "Core/track_topology.h"
#include "Seq/seq_param_iface.h"

static uint8_t seq_step_snapshot_track_role(seq_track_id_t track,
                                             seq_step_snapshot_role_t *out_role)
{
    if ((out_role == 0) || (track_topology_is_active(track) == 0U))
    {
        return 0U;
    }

    if (track_topology_is_play(track) != 0U)
    {
        *out_role = SEQ_STEP_SNAPSHOT_ROLE_PLAY;
        return 1U;
    }

    if (track_topology_is_special(track) != 0U)
    {
        *out_role = SEQ_STEP_SNAPSHOT_ROLE_SPECIAL;
        return 1U;
    }

    return 0U;
}

static uint8_t seq_step_snapshot_lock_compare(const seq_step_snapshot_plock_t *left,
                                               const seq_step_snapshot_plock_t *right)
{
    if (left->set_id != right->set_id)
    {
        return (left->set_id < right->set_id) ? 1U : 0U;
    }
    return (left->param_slot < right->param_slot) ? 1U : 0U;
}

static void seq_step_snapshot_sort_locks(seq_step_snapshot_t *snapshot)
{
    for (uint8_t i = 1U; i < snapshot->lock_count; ++i)
    {
        const seq_step_snapshot_plock_t value = snapshot->locks[i];
        uint8_t j = i;
        while ((j > 0U)
               && (seq_step_snapshot_lock_compare(&value, &snapshot->locks[j - 1U]) != 0U))
        {
            snapshot->locks[j] = snapshot->locks[j - 1U];
            --j;
        }
        snapshot->locks[j] = value;
    }
}

static uint8_t seq_step_snapshot_has_duplicate_lock(const seq_step_snapshot_t *snapshot,
                                                    uint8_t index)
{
    for (uint8_t i = 0U; i < index; ++i)
    {
        if ((snapshot->locks[i].set_id == snapshot->locks[index].set_id)
                && (snapshot->locks[i].param_slot == snapshot->locks[index].param_slot))
        {
            return 1U;
        }
    }
    return 0U;
}

static uint32_t seq_step_snapshot_track_lock_count(seq_track_id_t track)
{
    uint32_t count = 0U;
    for (seq_step_id_t step = 0U; step < (seq_step_id_t)SEQ_MAX_STEPS; ++step)
    {
        count += seq_model_step_plock_count(track, step);
    }
    return count;
}

static uint8_t seq_step_snapshot_validate_capacity(seq_track_id_t track,
                                                    seq_step_id_t step,
                                                    uint8_t incoming_count)
{
    const uint32_t current_count = seq_step_snapshot_track_lock_count(track);
    const uint32_t replaced_count = seq_model_step_plock_count(track, step);
    const uint32_t capacity = seq_model_get_track_plock_capacity(track);

    if (current_count < replaced_count)
    {
        return 0U;
    }

    return ((current_count - replaced_count + incoming_count) <= capacity) ? 1U : 0U;
}

static uint8_t seq_step_snapshot_write(seq_track_id_t track,
                                       seq_step_id_t step,
                                       const seq_step_snapshot_t *snapshot)
{
    seq_model_step_plock_clear(track, step);
    if (snapshot->role == (uint8_t)SEQ_STEP_SNAPSHOT_ROLE_PLAY)
    {
        seq_model_set_trig(track, step, snapshot->trig);
        if (snapshot->trig != 0U)
        {
            seq_model_set_step_roll(track, step, snapshot->roll);
        }
    }
    else
    {
        seq_model_set_special_action(track, step, snapshot->action);
    }

    for (uint8_t i = 0U; i < snapshot->lock_count; ++i)
    {
        const seq_step_snapshot_plock_t *const lock = &snapshot->locks[i];
        const seq_plock_op_status_t status = seq_model_step_plock_upsert(track,
                                                                          step,
                                                                          lock->set_id,
                                                                          lock->param_slot,
                                                                          lock->value16,
                                                                          lock->flags);
        if ((status != SEQ_PLOCK_OP_CREATED) && (status != SEQ_PLOCK_OP_UPDATED))
        {
            return 0U;
        }
    }

    return 1U;
}

uint8_t seq_step_snapshot_validate_for_track(seq_track_id_t track,
                                              const seq_step_snapshot_t *snapshot)
{
    seq_step_snapshot_role_t role;
    if ((snapshot == 0)
            || (snapshot->valid == 0U)
            || (seq_step_snapshot_track_role(track, &role) == 0U)
            || (snapshot->role != (uint8_t)role)
            || (snapshot->lock_count > seq_model_get_step_lock_limit(track)))
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < snapshot->lock_count; ++i)
    {
        const seq_step_snapshot_plock_t *const lock = &snapshot->locks[i];
        if ((seq_param_iface_slot_is_supported(track, lock->set_id, lock->param_slot) == 0U)
                || ((role == SEQ_STEP_SNAPSHOT_ROLE_SPECIAL)
                    && (lock->set_id == (uint8_t)SEQ_PLOCK_SET_PLAY))
                || (seq_step_snapshot_has_duplicate_lock(snapshot, i) != 0U))
        {
            return 0U;
        }
    }

    return 1U;
}

uint8_t seq_step_snapshot_capture(seq_track_id_t track,
                                   seq_step_id_t step,
                                   seq_step_snapshot_t *out_snapshot)
{
    seq_step_snapshot_t snapshot;
    seq_step_snapshot_role_t role;

    if ((out_snapshot == 0)
            || (seq_model_is_step_editable_index(step) == 0U)
            || (seq_step_snapshot_track_role(track, &role) == 0U))
    {
        return 0U;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.valid = 1U;
    snapshot.role = (uint8_t)role;
    if (role == SEQ_STEP_SNAPSHOT_ROLE_PLAY)
    {
        snapshot.trig = seq_model_get_trig(track, step);
        snapshot.roll = seq_model_get_step_roll(track, step);
    }
    else
    {
        snapshot.action = seq_model_get_special_action(track, step);
    }

    snapshot.lock_count = seq_model_step_plock_count(track, step);
    if (snapshot.lock_count > seq_model_get_step_lock_limit(track))
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < snapshot.lock_count; ++i)
    {
        seq_plock_entry_t entry;
        if ((seq_model_step_plock_get_at(track, step, i, &entry) == 0U)
                || ((role == SEQ_STEP_SNAPSHOT_ROLE_SPECIAL)
                    && (entry.set_id == (uint8_t)SEQ_PLOCK_SET_PLAY)))
        {
            return 0U;
        }

        snapshot.locks[i].set_id = entry.set_id;
        snapshot.locks[i].param_slot = entry.param_slot;
        snapshot.locks[i].value16 = entry.value16;
        snapshot.locks[i].flags = entry.flags;
    }

    seq_step_snapshot_sort_locks(&snapshot);
    if (seq_step_snapshot_validate_for_track(track, &snapshot) == 0U)
    {
        return 0U;
    }

    *out_snapshot = snapshot;
    return 1U;
}

uint8_t seq_step_snapshot_capture_list(seq_track_id_t track,
                                        const seq_step_id_t *steps,
                                        uint8_t step_count,
                                        seq_step_snapshot_list_t *out_list)
{
    if ((out_list == 0) || (steps == 0) || (step_count == 0U)
            || (step_count > (uint8_t)SEQ_STEP_SNAPSHOT_MAX_STEPS))
    {
        return 0U;
    }

    memset(out_list, 0, sizeof(*out_list));
    for (uint8_t i = 0U; i < step_count; ++i)
    {
        if ((seq_model_is_step_editable_index(steps[i]) == 0U)
                || (seq_step_snapshot_capture(track,
                                               steps[i],
                                               &out_list->entries[i].snapshot) == 0U))
        {
            memset(out_list, 0, sizeof(*out_list));
            return 0U;
        }

        for (uint8_t j = 0U; j < i; ++j)
        {
            if (out_list->entries[j].step == steps[i])
            {
                memset(out_list, 0, sizeof(*out_list));
                return 0U;
            }
        }
        out_list->entries[i].step = steps[i];
    }

    out_list->count = step_count;
    return 1U;
}

uint8_t seq_step_snapshot_apply(seq_track_id_t track,
                                seq_step_id_t step,
                                const seq_step_snapshot_t *snapshot)
{
    seq_step_snapshot_t previous;
    if ((seq_model_is_step_editable_index(step) == 0U)
            || (seq_step_snapshot_validate_for_track(track, snapshot) == 0U)
            || (seq_step_snapshot_validate_capacity(track, step, snapshot->lock_count) == 0U)
            || (seq_step_snapshot_capture(track, step, &previous) == 0U))
    {
        return 0U;
    }

    if (seq_step_snapshot_write(track, step, snapshot) != 0U)
    {
        return 1U;
    }

    (void)seq_step_snapshot_write(track, step, &previous);
    return 0U;
}

uint8_t seq_step_snapshot_can_apply_list(seq_track_id_t track,
                                         const seq_step_snapshot_list_t *list)
{
    uint32_t current_count;
    uint32_t replaced_count = 0U;
    uint32_t incoming_count = 0U;

    if ((list == 0) || (list->count == 0U)
            || (list->count > (uint8_t)SEQ_STEP_SNAPSHOT_MAX_STEPS))
    {
        return 0U;
    }

    current_count = seq_step_snapshot_track_lock_count(track);
    for (uint8_t i = 0U; i < list->count; ++i)
    {
        if ((seq_model_is_step_editable_index(list->entries[i].step) == 0U)
                || (seq_step_snapshot_validate_for_track(track,
                                                          &list->entries[i].snapshot) == 0U))
        {
            return 0U;
        }

        for (uint8_t j = 0U; j < i; ++j)
        {
            if (list->entries[j].step == list->entries[i].step)
            {
                return 0U;
            }
        }
        replaced_count += seq_model_step_plock_count(track, list->entries[i].step);
        incoming_count += list->entries[i].snapshot.lock_count;
    }

    if ((current_count < replaced_count)
            || (current_count - replaced_count + incoming_count
                > seq_model_get_track_plock_capacity(track)))
    {
        return 0U;
    }

    return 1U;
}

uint8_t seq_step_snapshot_apply_list(seq_track_id_t track,
                                     const seq_step_snapshot_list_t *list)
{
    if (seq_step_snapshot_can_apply_list(track, list) == 0U)
    {
        return 0U;
    }

    /* Release all target locks first. The preflight above guarantees that the
     * complete destination image fits before any mutation starts. */
    for (uint8_t i = 0U; i < list->count; ++i)
    {
        seq_model_step_plock_clear(track, list->entries[i].step);
    }

    for (uint8_t i = 0U; i < list->count; ++i)
    {
        if (seq_step_snapshot_write(track,
                                    list->entries[i].step,
                                    &list->entries[i].snapshot) == 0U)
        {
            return 0U;
        }
    }

    return 1U;
}

uint8_t seq_step_snapshot_equal(const seq_step_snapshot_t *left,
                                const seq_step_snapshot_t *right)
{
    if ((left == 0) || (right == 0)
            || (left->valid != right->valid)
            || (left->role != right->role)
            || (left->lock_count != right->lock_count))
    {
        return 0U;
    }

    if ((left->role == (uint8_t)SEQ_STEP_SNAPSHOT_ROLE_PLAY)
            && ((left->trig != right->trig) || (left->roll != right->roll)))
    {
        return 0U;
    }
    if ((left->role == (uint8_t)SEQ_STEP_SNAPSHOT_ROLE_SPECIAL)
            && (left->action != right->action))
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < left->lock_count; ++i)
    {
        if ((left->locks[i].set_id != right->locks[i].set_id)
                || (left->locks[i].param_slot != right->locks[i].param_slot)
                || (left->locks[i].value16 != right->locks[i].value16)
                || (left->locks[i].flags != right->locks[i].flags))
        {
            return 0U;
        }
    }

    return 1U;
}
