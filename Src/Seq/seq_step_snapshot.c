#include "Seq/seq_step_snapshot.h"

#include <string.h>

#include "Core/track_topology.h"
#include "Seq/seq_lane.h"
#include "Seq/seq_param_iface.h"

static uint8_t seq_step_snapshot_track_is_valid(seq_track_id_t track)
{
    seq_lane_descriptor_t descriptor;
    return (seq_lane_get_descriptor((seq_lane_id_t)track, &descriptor) != 0U)
            && (descriptor.active != 0U);
}

static uint8_t lock_before(const seq_step_snapshot_plock_t *a, const seq_step_snapshot_plock_t *b)
{
    return (a->set_id != b->set_id) ? (uint8_t)(a->set_id < b->set_id)
                                    : (uint8_t)(a->param_slot < b->param_slot);
}

static void sort_locks(seq_step_snapshot_t *snapshot)
{
    for (uint8_t i = 1U; i < snapshot->lock_count; ++i)
    {
        const seq_step_snapshot_plock_t value = snapshot->locks[i];
        uint8_t j = i;
        while ((j != 0U) && (lock_before(&value, &snapshot->locks[j - 1U]) != 0U))
        {
            snapshot->locks[j] = snapshot->locks[j - 1U];
            --j;
        }
        snapshot->locks[j] = value;
    }
}

static uint8_t play_state_is_valid(const seq_step_play_t *play)
{
    if (play == NULL) return 0U;
    for (uint8_t voice = 0U; voice < SEQ_STEP_PLAY_VOICE_COUNT; ++voice)
    {
        if ((play->voices[voice].present_mask & (uint8_t)~SEQ_STEP_PLAY_PRESENT_ALL) != 0U) return 0U;
        for (uint8_t field = 0U; field < SEQ_STEP_PLAY_FIELD_COUNT; ++field)
        {
            const uint8_t mask = (uint8_t)(1U << field);
            if ((play->voices[voice].present_mask & mask) != 0U)
            {
                int16_t value = 0;
                if (seq_step_play_get(play, voice, (seq_step_play_field_t)field, &value) == 0U) return 0U;
                seq_step_play_t validated;
                seq_step_play_init(&validated);
                if (seq_step_play_set(&validated, voice, (seq_step_play_field_t)field, value) == 0U) return 0U;
            }
        }
    }
    return 1U;
}

static uint8_t capture_play(seq_track_id_t track, seq_step_id_t step, seq_step_play_t *out_play)
{
    if (out_play == NULL) return 0U;
    seq_step_play_init(out_play);
    for (uint8_t voice = 0U; voice < SEQ_STEP_PLAY_VOICE_COUNT; ++voice)
    {
        for (uint8_t field = 0U; field < SEQ_STEP_PLAY_FIELD_COUNT; ++field)
        {
            int16_t value = 0;
            if (seq_model_step_play_get(track, step, voice, (seq_step_play_field_t)field, &value) != 0U)
            {
                if (seq_step_play_set(out_play, voice, (seq_step_play_field_t)field, value) == 0U) return 0U;
            }
        }
    }
    return 1U;
}

static uint8_t apply_play(seq_track_id_t track, seq_step_id_t step, const seq_step_play_t *play)
{
    if (play_state_is_valid(play) == 0U) return 0U;
    seq_model_step_play_clear(track, step);
    for (uint8_t voice = 0U; voice < SEQ_STEP_PLAY_VOICE_COUNT; ++voice)
    {
        for (uint8_t field = 0U; field < SEQ_STEP_PLAY_FIELD_COUNT; ++field)
        {
            int16_t value = 0;
            if (seq_step_play_get(play, voice, (seq_step_play_field_t)field, &value) != 0U)
            {
                if (seq_model_step_play_set(track, step, voice, (seq_step_play_field_t)field, value) == 0U) return 0U;
            }
        }
    }
    return 1U;
}

uint8_t seq_step_snapshot_validate_for_track(seq_track_id_t track,
                                              const seq_step_snapshot_t *snapshot)
{
    if ((seq_step_snapshot_track_is_valid(track) == 0U) || (snapshot == 0) || (snapshot->valid == 0U)
            || (snapshot->lock_count > SEQ_STEP_SNAPSHOT_MAX_LOCKS)
            || (play_state_is_valid(&snapshot->play) == 0U)) return 0U;
    if ((seq_step_play_has_any(&snapshot->play) != 0U)
            && (seq_model_track_can_store_play(track) == 0U)) return 0U;
    for (uint8_t i = 0U; i < snapshot->lock_count; ++i)
    {
        const seq_step_snapshot_plock_t *lock = &snapshot->locks[i];
        if (seq_param_iface_slot_is_storable(track, lock->set_id, lock->param_slot) == 0U) return 0U;
        for (uint8_t j = 0U; j < i; ++j)
            if ((snapshot->locks[j].set_id == lock->set_id)
                    && (snapshot->locks[j].param_slot == lock->param_slot)) return 0U;
    }
    return 1U;
}

uint8_t seq_step_snapshot_capture(seq_track_id_t track, seq_step_id_t step,
                                   seq_step_snapshot_t *out_snapshot)
{
    if ((out_snapshot == 0) || (seq_step_snapshot_track_is_valid(track) == 0U)
            || (seq_model_is_step_editable_index(step) == 0U)) return 0U;
    seq_step_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.valid = 1U;
    snapshot.trig = seq_model_get_trig(track, step);
    snapshot.roll = seq_model_get_step_roll(track, step);
    if (capture_play(track, step, &snapshot.play) == 0U) return 0U;
    snapshot.lock_count = seq_model_step_param_plock_count(track, step);
    if (snapshot.lock_count > SEQ_STEP_SNAPSHOT_MAX_LOCKS) return 0U;
    for (uint8_t i = 0U; i < snapshot.lock_count; ++i)
    {
        seq_plock_entry_t entry;
        if (seq_model_step_param_plock_get_at(track, step, i, &entry) == 0U) return 0U;
        snapshot.locks[i] = (seq_step_snapshot_plock_t){ entry.set_id,
            entry.param_slot, entry.value16, entry.flags };
    }
    sort_locks(&snapshot);
    if (seq_step_snapshot_validate_for_track(track, &snapshot) == 0U) return 0U;
    *out_snapshot = snapshot;
    return 1U;
}

uint8_t seq_step_snapshot_capture_list(seq_track_id_t track, const seq_step_id_t *steps,
                                        uint8_t count, seq_step_snapshot_list_t *out_list)
{
    if ((steps == 0) || (out_list == 0) || (count == 0U)
            || (count > SEQ_STEP_SNAPSHOT_MAX_STEPS)) return 0U;
    memset(out_list, 0, sizeof(*out_list));
    for (uint8_t i = 0U; i < count; ++i)
    {
        for (uint8_t j = 0U; j < i; ++j) if (steps[j] == steps[i]) return 0U;
        out_list->entries[i].step = steps[i];
        if (seq_step_snapshot_capture(track, steps[i], &out_list->entries[i].snapshot) == 0U) return 0U;
    }
    out_list->count = count;
    return 1U;
}

uint8_t seq_step_snapshot_can_apply_list(seq_track_id_t track,
                                         const seq_step_snapshot_list_t *list)
{
    if ((seq_step_snapshot_track_is_valid(track) == 0U) || (list == 0) || (list->count == 0U)
            || (list->count > SEQ_STEP_SNAPSHOT_MAX_STEPS)) return 0U;
    uint32_t replaced = 0U, incoming = 0U;
    for (uint8_t i = 0U; i < list->count; ++i)
    {
        if ((seq_model_is_step_editable_index(list->entries[i].step) == 0U)
                || (seq_step_snapshot_validate_for_track(track, &list->entries[i].snapshot) == 0U)) return 0U;
        for (uint8_t j = 0U; j < i; ++j)
            if (list->entries[j].step == list->entries[i].step) return 0U;
        replaced += seq_model_step_param_plock_count(track, list->entries[i].step);
        incoming += list->entries[i].snapshot.lock_count;
    }
    const uint32_t current = seq_model_get_track_plock_count(track);
    return (uint8_t)((current >= replaced)
        && ((current - replaced + incoming) <= seq_model_get_track_plock_capacity(track)));
}

static uint8_t write_step(seq_track_id_t track, seq_step_id_t step,
                          const seq_step_snapshot_t *snapshot)
{
    seq_model_step_plock_clear(track, step);
    seq_model_step_play_clear(track, step);
    if (apply_play(track, step, &snapshot->play) == 0U) return 0U;
    seq_model_set_trig(track, step, snapshot->trig);
    if (snapshot->trig != 0U) seq_model_set_step_roll(track, step, snapshot->roll);
    for (uint8_t i = 0U; i < snapshot->lock_count; ++i)
    {
        const seq_step_snapshot_plock_t *lock = &snapshot->locks[i];
        const seq_plock_op_status_t status = seq_model_step_plock_upsert(
            track, step, lock->set_id, lock->param_slot, lock->value16, lock->flags);
        if ((status != SEQ_PLOCK_OP_CREATED) && (status != SEQ_PLOCK_OP_UPDATED)) return 0U;
    }
    return 1U;
}

uint8_t seq_step_snapshot_apply_list(seq_track_id_t track, const seq_step_snapshot_list_t *list)
{
    if (seq_step_snapshot_can_apply_list(track, list) == 0U) return 0U;
    for (uint8_t i = 0U; i < list->count; ++i)
    {
        seq_model_step_plock_clear(track, list->entries[i].step);
        seq_model_step_play_clear(track, list->entries[i].step);
    }
    for (uint8_t i = 0U; i < list->count; ++i)
        if (write_step(track, list->entries[i].step, &list->entries[i].snapshot) == 0U) return 0U;
    return 1U;
}

uint8_t seq_step_snapshot_apply(seq_track_id_t track, seq_step_id_t step,
                                const seq_step_snapshot_t *snapshot)
{
    if (snapshot == 0) return 0U;
    seq_step_snapshot_list_t list;
    memset(&list, 0, sizeof(list));
    list.count = 1U; list.entries[0].step = step; list.entries[0].snapshot = *snapshot;
    return seq_step_snapshot_apply_list(track, &list);
}

uint8_t seq_step_snapshot_equal(const seq_step_snapshot_t *a, const seq_step_snapshot_t *b)
{
    if ((a == 0) || (b == 0) || (a->valid != b->valid) || (a->trig != b->trig)
            || (a->roll != b->roll) || (a->lock_count != b->lock_count)
            || (memcmp(&a->play, &b->play, sizeof(a->play)) != 0)) return 0U;
    for (uint8_t i = 0U; i < a->lock_count; ++i)
        if (memcmp(&a->locks[i], &b->locks[i], sizeof(a->locks[i])) != 0) return 0U;
    return 1U;
}
