/*
 * Module: seq_clipboard
 * Role: Presse-papiers d'édition pour copier/coller des pas de séquence.
 * Responsibilities: stocke une sélection normalisée (ancre + offsets), trig + plocks,
 * et recolle sur une destination en respectant les paramètres supportés.
 * Integration: service utilisé par seq_edit; ne gère pas l'UI ni l'exécution temps réel.
 */
#include "Seq/seq_clipboard.h"
#include "Seq/seq_clipboard_storage.h"
#include "Track/entity_topology.h"
#include "Seq/seq_step_snapshot.h"

#include <string.h>

#include "Platform/memory_layout.h"

typedef struct
{
    seq_step_id_t step;
    seq_clipboard_step_t snapshot;
} seq_clipboard_step_entry_t;

_Static_assert(sizeof(seq_clipboard_step_entry_t) == 30U,
               "sequence clipboard step entry size changed");

typedef struct
{
    uint8_t count;
    uint8_t reserved[3];
    seq_clipboard_step_entry_t entries[SEQ_MAX_STEPS];
    seq_clipboard_plock_t locks[SEQ_PLOCK_POOL_CAP_PER_TRACK];
} seq_clipboard_step_list_t;

_Static_assert(sizeof(seq_clipboard_step_list_t) == 3972U,
               "sequence clipboard compact storage size changed");

typedef struct
{
    uint8_t valid;
    seq_step_id_t source_anchor;
    seq_clipboard_step_list_t steps;
} seq_clipboard_state_t;

_Static_assert(sizeof(seq_clipboard_state_t) == 3974U,
               "sequence clipboard state size changed");

UI_SDRAM static seq_clipboard_state_t g_seq_clipboard;

static uint8_t seq_clipboard_track_is_valid(seq_track_id_t track)
{
    entity_topology_descriptor_t entity;
    return (uint8_t)((entity_topology_get((brick_entity_id_t)track, &entity) != 0U)
            && (entity_topology_can_sequence(&entity) != 0U));
}

static uint8_t seq_clipboard_storage_is_valid(void)
{
    if ((g_seq_clipboard.steps.count == 0U)
            || (g_seq_clipboard.steps.count > SEQ_MAX_STEPS))
        return 0U;

    uint16_t expected_offset = 0U;
    for (uint8_t i = 0U; i < g_seq_clipboard.steps.count; ++i)
    {
        const seq_clipboard_step_t *const step =
            &g_seq_clipboard.steps.entries[i].snapshot;
        if ((step->lock_offset != expected_offset)
                || (step->valid == 0U)
                || (step->lock_count > SEQ_STEP_MAX_LOCKS)
                || ((uint32_t)expected_offset + step->lock_count
                    > SEQ_PLOCK_POOL_CAP_PER_TRACK))
            return 0U;
        expected_offset = (uint16_t)(expected_offset + step->lock_count);
    }
    return 1U;
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

static uint8_t seq_clipboard_resolve_target_step(
    const seq_clipboard_step_entry_t *source,
    seq_step_id_t dest_anchor,
    seq_step_id_t *out_step)
{
    if ((source == 0) || (out_step == 0))
    {
        return 0U;
    }

    const seq_step_id_t target_step = (seq_step_id_t)(dest_anchor + source->step);
    if (seq_model_is_step_editable_index(target_step) == 0U)
    {
        return 0U;
    }

    *out_step = target_step;
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
    g_seq_clipboard.source_anchor = source_anchor;

    uint16_t lock_count = 0U;
    for (uint8_t i = 0U; i < step_count; ++i)
    {
        if (seq_model_is_step_editable_index(steps[i]) == 0U)
            return 0U;
        for (uint8_t j = 0U; j < i; ++j)
            if (steps[j] == steps[i]) return 0U;

        seq_step_snapshot_t snapshot;
        if ((seq_step_snapshot_capture(track, steps[i], &snapshot) == 0U)
                || (seq_clipboard_storage_pack_step(&snapshot,
                        &g_seq_clipboard.steps.entries[i].snapshot,
                        g_seq_clipboard.steps.locks, &lock_count) == 0U))
            return 0U;
        g_seq_clipboard.steps.entries[i].step =
            (seq_step_id_t)(steps[i] - source_anchor);
    }

    g_seq_clipboard.steps.count = step_count;
    g_seq_clipboard.valid = 1U;

    return 1U;
}

uint8_t seq_clipboard_collect_paste_targets(seq_track_id_t target_track,
                                            const seq_step_id_t *dest_steps,
                                            uint8_t dest_count,
                                            seq_step_id_t *out_steps,
                                            uint8_t max_steps,
                                            uint8_t *out_count)
{
    if (out_count != 0)
    {
        *out_count = 0U;
    }

    if ((out_count == 0)
        || (out_steps == 0)
        || (max_steps == 0U)
        || (g_seq_clipboard.valid == 0U)
        || (seq_clipboard_storage_is_valid() == 0U)
        || (seq_clipboard_track_is_valid(target_track) == 0U)
        || (dest_steps == 0)
        || (dest_count == 0U))
    {
        return 0U;
    }

    seq_step_id_t dest_anchor = 0U;
    if (seq_clipboard_find_min_step(dest_steps, dest_count, &dest_anchor) == 0U)
    {
        return 0U;
    }

    uint8_t count = 0U;
    for (uint8_t i = 0U; i < g_seq_clipboard.steps.count; ++i)
    {
        seq_step_id_t target_step = 0U;
        if (seq_clipboard_resolve_target_step(&g_seq_clipboard.steps.entries[i],
                                              dest_anchor,
                                              &target_step) == 0U)
        {
            continue;
        }
        if (count >= max_steps)
        {
            return 0U;
        }
        out_steps[count++] = target_step;
    }

    *out_count = count;
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
        || (seq_clipboard_storage_is_valid() == 0U)
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

    uint32_t replaced = 0U;
    uint32_t incoming = 0U;
    uint8_t paste_count = 0U;
    seq_step_snapshot_t projected;
    for (uint8_t i = 0U; i < g_seq_clipboard.steps.count; ++i)
    {
        const seq_clipboard_step_entry_t *const src = &g_seq_clipboard.steps.entries[i];

        seq_step_id_t target_step = 0U;
        if (seq_clipboard_resolve_target_step(src, dest_anchor, &target_step) == 0U)
        {
            result.trunc = 1U;
            continue;
        }

        if (seq_clipboard_storage_unpack_step(&src->snapshot,
                g_seq_clipboard.steps.locks, &projected) == 0U)
        {
            result.partial = 1U;
            *out_result = result;
            return 0U;
        }
        if (seq_step_snapshot_project_play_for_track(
                target_track,
                &projected) == 0U
                || (seq_step_snapshot_validate_for_track(target_track,
                                                         &projected) == 0U))
        {
            result.partial = 1U;
            *out_result = result;
            return 0U;
        }
        replaced += seq_model_step_param_plock_count(target_track, target_step);
        incoming += projected.lock_count;
        paste_count++;
    }

    const uint32_t current = seq_model_get_track_plock_count(target_track);
    if ((paste_count == 0U) || (current < replaced)
            || ((current - replaced + incoming)
                > seq_model_get_track_plock_capacity(target_track)))
    {
        result.partial = (paste_count != 0U) ? 1U : 0U;
        *out_result = result;
        return 0U;
    }

    for (uint8_t i = 0U; i < g_seq_clipboard.steps.count; ++i)
    {
        seq_step_id_t target_step = 0U;
        if (seq_clipboard_resolve_target_step(&g_seq_clipboard.steps.entries[i],
                                              dest_anchor,
                                              &target_step) != 0U)
        {
            seq_model_step_plock_clear(target_track, target_step);
            seq_model_play_clear_step(target_track, target_step);
        }
    }

    for (uint8_t i = 0U; i < g_seq_clipboard.steps.count; ++i)
    {
        const seq_clipboard_step_entry_t *const src = &g_seq_clipboard.steps.entries[i];
        seq_step_id_t target_step = 0U;
        if (seq_clipboard_resolve_target_step(src, dest_anchor, &target_step) == 0U)
        {
            continue;
        }

        if ((seq_clipboard_storage_unpack_step(&src->snapshot,
                    g_seq_clipboard.steps.locks, &projected) == 0U)
                || (seq_step_snapshot_project_play_for_track(target_track, &projected) == 0U)
                || (seq_step_snapshot_apply(target_track, target_step, &projected) == 0U))
        {
            result.partial = 1U;
            *out_result = result;
            return 0U;
        }
        result.pasted_steps++;
    }

    *out_result = result;
    return (result.pasted_steps > 0U) ? 1U : 0U;
}
