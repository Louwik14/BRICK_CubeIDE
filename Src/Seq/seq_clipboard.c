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
#include "Seq/seq_param_iface.h"

typedef struct
{
    uint8_t set_id;
    seq_param8_t param8;
    seq_value16_t value16;
    uint8_t flags;
} seq_clipboard_lock_t;

typedef struct
{
    uint8_t used;
    seq_step_id_t offset;
    uint8_t trig;
    uint8_t lock_count;
    seq_clipboard_lock_t locks[SEQ_STEP_MAX_LOCKS];
} seq_clipboard_step_t;

typedef struct
{
    uint8_t valid;
    seq_track_id_t source_track;
    seq_step_id_t source_anchor;
    uint8_t step_count;
    seq_clipboard_step_t steps[SEQ_MAX_STEPS];
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
            lock->param8 = entry.param8;
            lock->value16 = entry.value16;
            lock->flags = entry.flags;
            dst->lock_count++;
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
        seq_model_step_plock_clear(target_track, target_step);

        track_runtime_refresh_track(target_track);
        for (uint8_t l = 0U; l < src->lock_count; ++l)
        {
            const seq_clipboard_lock_t *const lock = &src->locks[l];
            if (seq_param_iface_is_param_supported(target_track, lock->set_id, lock->param8) == 0U)
            {
                result.partial = 1U;
                continue;
            }

            const seq_plock_op_status_t status = seq_model_step_plock_upsert(target_track,
                                                                              target_step,
                                                                              lock->set_id,
                                                                              lock->param8,
                                                                              lock->value16,
                                                                              lock->flags);
            if ((status != SEQ_PLOCK_OP_CREATED) && (status != SEQ_PLOCK_OP_UPDATED))
            {
                result.partial = 1U;
            }
        }

        result.pasted_steps++;
    }

    *out_result = result;
    return (result.pasted_steps > 0U) ? 1U : 0U;
}
