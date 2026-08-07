/*
 * Module: seq_clipboard
 * Role: Presse-papiers d'édition pour copier/coller des pas de séquence.
 * Responsibilities: stocke une sélection normalisée (ancre + offsets), trig + plocks,
 * et recolle sur une destination en respectant les paramètres supportés.
 * Integration: service utilisé par seq_edit; ne gère pas l'UI ni l'exécution temps réel.
 */
#include "Seq/seq_clipboard.h"
#include "Seq/seq_lane.h"
#include "Seq/seq_step_snapshot.h"

#include <string.h>

#include "Storage/memory_layout.h"

typedef struct
{
    uint8_t valid;
    seq_step_id_t source_anchor;
    seq_step_snapshot_list_t steps;
} seq_clipboard_state_t;

UI_SDRAM static seq_clipboard_state_t g_seq_clipboard;

static uint8_t seq_clipboard_track_is_valid(seq_track_id_t track)
{
    seq_lane_descriptor_t descriptor;
    return (seq_lane_get_descriptor((seq_lane_id_t)track, &descriptor) != 0U)
            && (descriptor.active != 0U);
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
    const seq_step_snapshot_entry_t *source,
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
    g_seq_clipboard.valid = 1U;
    g_seq_clipboard.source_anchor = source_anchor;

    if (seq_step_snapshot_capture_list(track,
                                       steps,
                                       step_count,
                                       &g_seq_clipboard.steps) == 0U)
    {
        g_seq_clipboard.valid = 0U;
        return 0U;
    }

    for (uint8_t i = 0U; i < g_seq_clipboard.steps.count; ++i)
    {
        g_seq_clipboard.steps.entries[i].step =
            (seq_step_id_t)(g_seq_clipboard.steps.entries[i].step - source_anchor);
    }

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

    seq_step_snapshot_list_t paste_list;
    memset(&paste_list, 0, sizeof(paste_list));
    for (uint8_t i = 0U; i < g_seq_clipboard.steps.count; ++i)
    {
        const seq_step_snapshot_entry_t *const src = &g_seq_clipboard.steps.entries[i];

        seq_step_id_t target_step = 0U;
        if (seq_clipboard_resolve_target_step(src, dest_anchor, &target_step) == 0U)
        {
            result.trunc = 1U;
            continue;
        }

        paste_list.entries[paste_list.count].step = target_step;
        paste_list.entries[paste_list.count].snapshot = src->snapshot;
        paste_list.count++;
    }

    if ((paste_list.count == 0U)
            || (seq_step_snapshot_apply_list(target_track, &paste_list) == 0U))
    {
        result.partial = (paste_list.count != 0U) ? 1U : 0U;
        *out_result = result;
        return 0U;
    }
    result.pasted_steps = paste_list.count;

    *out_result = result;
    return (result.pasted_steps > 0U) ? 1U : 0U;
}

