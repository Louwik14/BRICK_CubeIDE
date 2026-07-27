/*
 * Module: seq_clipboard
 * Role: Presse-papiers d'édition pour copier/coller des pas de séquence.
 * Responsibilities: stocke une sélection normalisée (ancre + offsets), trig + plocks,
 * et recolle sur une destination en respectant les paramètres supportés.
 * Integration: service utilisé par seq_edit; ne gère pas l'UI ni l'exécution temps réel.
 */
#include "Seq/seq_clipboard.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Seq/seq_param_iface.h"

#define SEQ_CLIPBOARD_GROUP_MEMBER_MAX 8U

typedef struct
{
    uint8_t set_id;
    seq_param_slot_t param_slot;
    seq_value16_t value16;
    uint8_t flags;
} seq_clipboard_lock_t;

typedef struct
{
    uint8_t used;
    seq_step_id_t offset;
    uint8_t trig;
    uint8_t roll;
    uint8_t lock_count;
    seq_clipboard_lock_t locks[SEQ_STEP_MAX_LOCKS];
} seq_clipboard_step_t;

typedef struct
{
    uint8_t used;
    uint8_t member_index;
    seq_track_id_t source_track;
    uint8_t lock_count;
    seq_clipboard_lock_t locks[SEQ_STEP_MAX_LOCKS];
} seq_clipboard_member_play_t;

typedef struct
{
    uint8_t valid;
    seq_track_id_t source_track;
    seq_step_id_t source_anchor;
    uint8_t group_member_count;
    uint8_t step_count;
    seq_clipboard_step_t steps[SEQ_MAX_STEPS];
    seq_clipboard_member_play_t member_play[SEQ_MAX_STEPS][SEQ_CLIPBOARD_GROUP_MEMBER_MAX];
} seq_clipboard_state_t;

UI_SDRAM static seq_clipboard_state_t g_seq_clipboard;

static uint8_t seq_clipboard_track_is_valid(seq_track_id_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
}

static uint8_t seq_clipboard_find_min_step(const seq_step_id_t *steps, uint8_t step_count, seq_step_id_t *out_min)
{
    if ((steps == 0) || (out_min == 0) || (step_count == 0U))
    {
        return 0U;
    }

    seq_step_id_t min_step = steps[0];
    for (uint8_t i = 1U; i < step_count; ++i)
    {
        if (steps[i] < min_step)
        {
            min_step = steps[i];
        }
    }

    *out_min = min_step;
    return 1U;
}

static uint8_t seq_clipboard_collect_group_members(seq_track_id_t master_track,
                                                   uint8_t *out_members,
                                                   uint8_t *out_count)
{
    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    if ((out_count == 0) || (seq_clipboard_track_is_valid(master_track) == 0U))
    {
        return 0U;
    }

    *out_count = 0U;
    (void)track_runtime_get_voice_group_role(master_track, &role_u8);
    if (role_u8 != (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER)
    {
        return 0U;
    }

    if (track_runtime_collect_voice_group_members(master_track,
                                                  out_members,
                                                  SEQ_CLIPBOARD_GROUP_MEMBER_MAX,
                                                  out_count) == 0U)
    {
        return 0U;
    }

    if (*out_count > SEQ_CLIPBOARD_GROUP_MEMBER_MAX)
    {
        *out_count = SEQ_CLIPBOARD_GROUP_MEMBER_MAX;
    }
    return (*out_count > 1U) ? 1U : 0U;
}

static uint8_t seq_clipboard_lock_find(const seq_clipboard_lock_t *locks,
                                       uint8_t lock_count,
                                       uint8_t set_id,
                                       seq_param_slot_t param_slot)
{
    for (uint8_t i = 0U; i < lock_count; ++i)
    {
        if ((locks[i].set_id == set_id) && (locks[i].param_slot == param_slot))
        {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t seq_clipboard_lock_append(seq_clipboard_lock_t *locks,
                                         uint8_t *lock_count,
                                         uint8_t set_id,
                                         seq_param_slot_t param_slot,
                                         seq_value16_t value16,
                                         uint8_t flags)
{
    if ((locks == 0) || (lock_count == 0) || (*lock_count >= SEQ_STEP_MAX_LOCKS))
    {
        return 0U;
    }

    if (seq_clipboard_lock_find(locks, *lock_count, set_id, param_slot) != 0U)
    {
        return 1U;
    }

    seq_clipboard_lock_t *const lock = &locks[*lock_count];
    lock->set_id = set_id;
    lock->param_slot = param_slot;
    lock->value16 = value16;
    lock->flags = flags;
    (*lock_count)++;
    return 1U;
}

static void seq_clipboard_copy_member_play(seq_clipboard_member_play_t *dst,
                                           seq_track_id_t member_track,
                                           uint8_t member_index,
                                           seq_step_id_t step)
{
    static const param_id_t mandatory_play_params[4U] = {
        PARAM_SEQ_PLAY_V1_NOTE,
        PARAM_SEQ_PLAY_V1_VEL,
        PARAM_SEQ_PLAY_V1_LEN,
        PARAM_SEQ_PLAY_V1_MICTIM
    };

    if ((dst == 0) || (seq_clipboard_track_is_valid(member_track) == 0U))
    {
        return;
    }

    dst->used = 1U;
    dst->member_index = member_index;
    dst->source_track = member_track;

    const uint8_t lock_count = seq_model_step_plock_count(member_track, step);
    for (uint8_t l = 0U; l < lock_count; ++l)
    {
        seq_plock_entry_t entry;
        if (seq_model_step_plock_get_at(member_track, step, l, &entry) == 0U)
        {
            continue;
        }
        if (entry.set_id != (uint8_t)SEQ_PLOCK_SET_PLAY)
        {
            continue;
        }

        (void)seq_clipboard_lock_append(dst->locks,
                                        &dst->lock_count,
                                        entry.set_id,
                                        entry.param_slot,
                                        entry.value16,
                                        entry.flags);
    }

    track_runtime_refresh_track(member_track);
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        seq_param_slot_t slot = 0U;
        if (seq_param_iface_param_to_slot(member_track,
                                          (uint8_t)SEQ_PLOCK_SET_PLAY,
                                          mandatory_play_params[i],
                                          &slot) == 0U)
        {
            continue;
        }
        if (seq_clipboard_lock_find(dst->locks, dst->lock_count, (uint8_t)SEQ_PLOCK_SET_PLAY, slot) != 0U)
        {
            continue;
        }

        seq_value16_t base_value = 0U;
        if (seq_param_iface_get_play_base_value(member_track, slot, &base_value) == 0U)
        {
            continue;
        }

        (void)seq_clipboard_lock_append(dst->locks,
                                        &dst->lock_count,
                                        (uint8_t)SEQ_PLOCK_SET_PLAY,
                                        slot,
                                        base_value,
                                        0U);
    }
}

static void seq_clipboard_clear_play_locks(seq_track_id_t track, seq_step_id_t step)
{
    seq_plock_entry_t entries[SEQ_STEP_MAX_LOCKS];
    uint8_t count = 0U;
    if (seq_model_step_plock_collect(track, step, entries, SEQ_STEP_MAX_LOCKS, &count) == 0U)
    {
        return;
    }

    for (uint8_t i = 0U; i < count; ++i)
    {
        if (entries[i].set_id == (uint8_t)SEQ_PLOCK_SET_PLAY)
        {
            (void)seq_model_step_plock_delete(track, step, entries[i].set_id, entries[i].param_slot);
        }
    }
}

static void seq_clipboard_paste_locks(seq_track_id_t target_track,
                                      seq_step_id_t target_step,
                                      const seq_clipboard_lock_t *locks,
                                      uint8_t lock_count,
                                      seq_clipboard_paste_result_t *result)
{
    track_runtime_refresh_track(target_track);
    for (uint8_t l = 0U; l < lock_count; ++l)
    {
        const seq_clipboard_lock_t *const lock = &locks[l];
        if (seq_param_iface_slot_is_supported(target_track, lock->set_id, lock->param_slot) == 0U)
        {
            result->partial = 1U;
            continue;
        }

        const seq_plock_op_status_t status = seq_model_step_plock_upsert(target_track,
                                                                          target_step,
                                                                          lock->set_id,
                                                                          lock->param_slot,
                                                                          lock->value16,
                                                                          lock->flags);
        if ((status != SEQ_PLOCK_OP_CREATED) && (status != SEQ_PLOCK_OP_UPDATED))
        {
            result->partial = 1U;
        }
    }
}

void seq_clipboard_init(void)
{
    memset(&g_seq_clipboard, 0, sizeof(g_seq_clipboard));
}

uint8_t seq_clipboard_is_valid(void)
{
    return g_seq_clipboard.valid;
}

uint8_t seq_clipboard_copy(seq_track_id_t track,
                           const seq_step_id_t *steps,
                           uint8_t step_count)
{
    if ((seq_clipboard_track_is_valid(track) == 0U)
        || (steps == 0)
        || (step_count == 0U)
        || (step_count > seq_model_get_editable_step_capacity()))
    {
        return 0U;
    }

    seq_step_id_t source_anchor = 0U;
    if (seq_clipboard_find_min_step(steps, step_count, &source_anchor) == 0U)
    {
        return 0U;
    }

    memset(&g_seq_clipboard, 0, sizeof(g_seq_clipboard));
    g_seq_clipboard.valid = 1U;
    g_seq_clipboard.source_track = track;
    g_seq_clipboard.source_anchor = source_anchor;

    uint8_t source_members[SEQ_CLIPBOARD_GROUP_MEMBER_MAX];
    uint8_t source_member_count = 0U;
    const uint8_t source_is_group = seq_clipboard_collect_group_members(track,
                                                                        source_members,
                                                                        &source_member_count);
    if (source_is_group != 0U)
    {
        g_seq_clipboard.group_member_count = source_member_count;
    }

    for (uint8_t i = 0U; i < step_count; ++i)
    {
        const seq_step_id_t step = steps[i];
        if (seq_model_is_step_editable_index(step) == 0U)
        {
            continue;
        }

        seq_clipboard_step_t *const dst = &g_seq_clipboard.steps[g_seq_clipboard.step_count];
        dst->used = 1U;
        dst->offset = (seq_step_id_t)(step - source_anchor);
        dst->trig = seq_model_get_trig(track, step);
        dst->roll = seq_model_get_step_roll(track, step);

        const uint8_t lock_count = seq_model_step_plock_count(track, step);
        for (uint8_t l = 0U; l < lock_count; ++l)
        {
            if (dst->lock_count >= SEQ_STEP_MAX_LOCKS)
            {
                break;
            }

            seq_plock_entry_t entry;
            if (seq_model_step_plock_get_at(track, step, l, &entry) == 0U)
            {
                continue;
            }

            seq_clipboard_lock_t *const lock = &dst->locks[dst->lock_count];
            lock->set_id = entry.set_id;
            lock->param_slot = entry.param_slot;
            lock->value16 = entry.value16;
            lock->flags = entry.flags;
            dst->lock_count++;
        }

        if (source_is_group != 0U)
        {
            for (uint8_t member_index = 0U; member_index < source_member_count; ++member_index)
            {
                seq_clipboard_copy_member_play(&g_seq_clipboard.member_play[g_seq_clipboard.step_count][member_index],
                                               source_members[member_index],
                                               member_index,
                                               step);
            }
        }

        g_seq_clipboard.step_count++;
    }

    if (g_seq_clipboard.step_count == 0U)
    {
        g_seq_clipboard.valid = 0U;
        return 0U;
    }

    return 1U;
}

uint8_t seq_clipboard_paste(seq_track_id_t target_track,
                            const seq_step_id_t *dest_steps,
                            uint8_t dest_count,
                            seq_clipboard_paste_result_t *out_result)
{
    seq_clipboard_paste_result_t result = { 0U, 0U, 0U };

    if ((out_result == 0)
        || (g_seq_clipboard.valid == 0U)
        || (seq_clipboard_track_is_valid(target_track) == 0U)
        || (dest_steps == 0)
        || (dest_count == 0U))
    {
        if (out_result != 0)
        {
            *out_result = result;
        }
        return 0U;
    }

    seq_step_id_t dest_anchor = 0U;
    if (seq_clipboard_find_min_step(dest_steps, dest_count, &dest_anchor) == 0U)
    {
        *out_result = result;
        return 0U;
    }

    uint8_t target_members[SEQ_CLIPBOARD_GROUP_MEMBER_MAX];
    uint8_t target_member_count = 0U;
    const uint8_t target_is_group = seq_clipboard_collect_group_members(target_track,
                                                                        target_members,
                                                                        &target_member_count);

    for (uint8_t i = 0U; i < g_seq_clipboard.step_count; ++i)
    {
        const seq_clipboard_step_t *const src = &g_seq_clipboard.steps[i];
        if (src->used == 0U)
        {
            continue;
        }

        const seq_step_id_t target_step = (seq_step_id_t)(dest_anchor + src->offset);
        if (seq_model_is_step_editable_index(target_step) == 0U)
        {
            result.trunc = 1U;
            continue;
        }

        seq_model_set_trig(target_track, target_step, src->trig);
        seq_model_set_step_roll(target_track, target_step, src->roll);
        seq_model_step_plock_clear(target_track, target_step);

        seq_clipboard_paste_locks(target_track, target_step, src->locks, src->lock_count, &result);

        if ((g_seq_clipboard.group_member_count > 1U) && (target_is_group != 0U))
        {
            const uint8_t copy_count = (g_seq_clipboard.group_member_count < target_member_count)
                ? g_seq_clipboard.group_member_count
                : target_member_count;
            if (g_seq_clipboard.group_member_count != target_member_count)
            {
                result.partial = 1U;
            }

            for (uint8_t member_index = 0U; member_index < copy_count; ++member_index)
            {
                const seq_clipboard_member_play_t *const member = &g_seq_clipboard.member_play[i][member_index];
                if (member->used == 0U)
                {
                    continue;
                }

                const seq_track_id_t member_target_track = target_members[member_index];
                if (member_index != 0U)
                {
                    seq_clipboard_clear_play_locks(member_target_track, target_step);
                }

                seq_clipboard_paste_locks(member_target_track,
                                          target_step,
                                          member->locks,
                                          member->lock_count,
                                          &result);
            }
        }
        else if (g_seq_clipboard.group_member_count > 1U)
        {
            result.partial = 1U;
        }

        result.pasted_steps++;
    }

    *out_result = result;
    return (result.pasted_steps > 0U) ? 1U : 0U;
}

