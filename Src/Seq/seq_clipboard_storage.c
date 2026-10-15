#include "Seq/seq_clipboard_storage.h"

#include <string.h>

#include "Seq/seq_param_iface.h"

uint8_t seq_clipboard_storage_pack_step(
    const seq_step_snapshot_t *snapshot,
    seq_clipboard_step_t *out_step,
    seq_clipboard_plock_t locks[SEQ_PLOCK_POOL_CAP_PER_TRACK],
    uint16_t *io_lock_count)
{
    if ((snapshot == NULL) || (out_step == NULL) || (locks == NULL)
            || (io_lock_count == NULL) || (snapshot->valid == 0U)
            || (snapshot->lock_count > SEQ_STEP_MAX_LOCKS)
            || (*io_lock_count > SEQ_PLOCK_POOL_CAP_PER_TRACK)
            || ((uint32_t)*io_lock_count + snapshot->lock_count
                > SEQ_PLOCK_POOL_CAP_PER_TRACK))
        return 0U;

    seq_clipboard_step_t packed;
    memset(&packed, 0, sizeof(packed));
    packed.lock_offset = *io_lock_count;
    packed.valid = snapshot->valid;
    packed.trig = snapshot->trig;
    packed.roll = snapshot->roll;
    packed.lock_count = snapshot->lock_count;
    packed.play = snapshot->play;

    for (uint8_t i = 0U; i < snapshot->lock_count; ++i)
    {
        seq_plock_key_t key = 0U;
        if (seq_param_iface_address_to_key(snapshot->locks[i].set_id,
                snapshot->locks[i].param_slot, &key) == 0U)
            return 0U;
        locks[*io_lock_count] = (seq_clipboard_plock_t){
            .value16 = snapshot->locks[i].value16,
            .key = key,
            .flags = snapshot->locks[i].flags
        };
        ++(*io_lock_count);
    }

    *out_step = packed;
    return 1U;
}

uint8_t seq_clipboard_storage_unpack_step(
    const seq_clipboard_step_t *step,
    const seq_clipboard_plock_t locks[SEQ_PLOCK_POOL_CAP_PER_TRACK],
    seq_step_snapshot_t *out_snapshot)
{
    if ((step == NULL) || (locks == NULL) || (out_snapshot == NULL)
            || (step->valid == 0U) || (step->lock_count > SEQ_STEP_MAX_LOCKS)
            || (step->lock_offset > SEQ_PLOCK_POOL_CAP_PER_TRACK)
            || ((uint32_t)step->lock_offset + step->lock_count
                > SEQ_PLOCK_POOL_CAP_PER_TRACK))
        return 0U;

    seq_step_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.valid = step->valid;
    snapshot.trig = step->trig;
    snapshot.roll = step->roll;
    snapshot.lock_count = step->lock_count;
    snapshot.play = step->play;

    for (uint8_t i = 0U; i < step->lock_count; ++i)
    {
        const seq_clipboard_plock_t *const lock =
            &locks[step->lock_offset + i];
        if (seq_param_iface_key_to_address(lock->key,
                &snapshot.locks[i].set_id,
                &snapshot.locks[i].param_slot) == 0U)
            return 0U;
        snapshot.locks[i].value16 = lock->value16;
        snapshot.locks[i].flags = lock->flags;
    }

    *out_snapshot = snapshot;
    return 1U;
}
