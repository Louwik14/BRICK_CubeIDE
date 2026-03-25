#include "Seq/seq_model.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Seq/seq_param_iface.h"

#define SEQ_LOCK_NONE 0xFFFFU

SEQ_STATE_D2 static seq_project_data_t g_seq_project;

static uint8_t seq_model_track_is_valid(seq_track_id_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
}

static uint8_t seq_model_step_is_valid(seq_step_id_t step)
{
    return (step < SEQ_MAX_STEPS) ? 1U : 0U;
}

static seq_step_t *seq_model_get_step_mut(seq_track_id_t track, seq_step_id_t step)
{
    if ((seq_model_track_is_valid(track) == 0U) || (seq_model_step_is_valid(step) == 0U))
    {
        return 0;
    }

    return &g_seq_project.tracks[track].steps[step];
}

static const seq_step_t *seq_model_get_step_const(seq_track_id_t track, seq_step_id_t step)
{
    if ((seq_model_track_is_valid(track) == 0U) || (seq_model_step_is_valid(step) == 0U))
    {
        return 0;
    }

    return &g_seq_project.tracks[track].steps[step];
}

static uint16_t seq_model_alloc_lock_node(void)
{
    if ((g_seq_project.free_count == 0U) || (g_seq_project.free_head == SEQ_LOCK_NONE))
    {
        return SEQ_LOCK_NONE;
    }

    const uint16_t idx = g_seq_project.free_head;
    g_seq_project.free_head = g_seq_project.pool[idx].next;
    g_seq_project.pool[idx].next = SEQ_LOCK_NONE;
    g_seq_project.free_count--;
    return idx;
}

static void seq_model_free_lock_node(uint16_t idx)
{
    if (idx >= (uint16_t)SEQ_PLOCK_POOL_CAP)
    {
        return;
    }

    g_seq_project.pool[idx].next = g_seq_project.free_head;
    g_seq_project.free_head = idx;
    g_seq_project.free_count++;
}

static uint16_t seq_model_find_lock_idx(const seq_step_t *step,
                                        uint8_t set_id,
                                        seq_param8_t param8,
                                        uint16_t *out_prev)
{
    if (out_prev != 0)
    {
        *out_prev = SEQ_LOCK_NONE;
    }

    uint16_t prev = SEQ_LOCK_NONE;
    uint16_t idx = step->lock_head;
    while (idx != SEQ_LOCK_NONE)
    {
        const seq_plock_entry_t *entry = &g_seq_project.pool[idx];
        if ((entry->set_id == set_id) && (entry->param8 == param8))
        {
            if (out_prev != 0)
            {
                *out_prev = prev;
            }
            return idx;
        }

        prev = idx;
        idx = entry->next;
    }

    return SEQ_LOCK_NONE;
}

static uint8_t seq_model_compute_step_mask(const seq_step_t *step)
{
    uint8_t mask = 0U;

    uint16_t idx = step->lock_head;
    while (idx != SEQ_LOCK_NONE)
    {
        const seq_plock_entry_t *entry = &g_seq_project.pool[idx];
        mask |= seq_param_iface_set_to_mask(entry->set_id);
        idx = entry->next;
    }

    return mask;
}

void seq_model_init_defaults(void)
{
    memset(&g_seq_project, 0, sizeof(g_seq_project));

    for (uint8_t tr = 0U; tr < SEQ_TRACK_COUNT; ++tr)
    {
        g_seq_project.tracks[tr].length_steps = SEQ_MAX_STEPS;
        g_seq_project.tracks[tr].ui_page = 0U;

        for (uint8_t st = 0U; st < SEQ_MAX_STEPS; ++st)
        {
            g_seq_project.tracks[tr].steps[st].lock_head = SEQ_LOCK_NONE;
        }
    }

    g_seq_project.free_head = 0U;
    g_seq_project.free_count = SEQ_PLOCK_POOL_CAP;

    for (uint16_t i = 0U; i < (uint16_t)SEQ_PLOCK_POOL_CAP; ++i)
    {
        g_seq_project.pool[i].next = (i + 1U < (uint16_t)SEQ_PLOCK_POOL_CAP) ? (uint16_t)(i + 1U) : SEQ_LOCK_NONE;
    }
}

const seq_project_data_t *seq_model_get_project(void)
{
    return &g_seq_project;
}


uint8_t seq_model_load_project(const seq_project_data_t *project)
{
    if (project == 0)
    {
        return 0U;
    }

    seq_model_init_defaults();

    uint8_t pool_seen[SEQ_PLOCK_POOL_CAP];
    memset(pool_seen, 0, sizeof(pool_seen));

    for (seq_track_id_t tr = 0U; tr < SEQ_TRACK_COUNT; ++tr)
    {
        uint8_t length_steps = project->tracks[tr].length_steps;
        if ((length_steps == 0U) || (length_steps > SEQ_MAX_STEPS))
        {
            length_steps = SEQ_MAX_STEPS;
        }
        g_seq_project.tracks[tr].length_steps = length_steps;

        uint8_t ui_page = project->tracks[tr].ui_page;
        if (ui_page >= SEQ_PAGE_COUNT)
        {
            ui_page = (uint8_t)(SEQ_PAGE_COUNT - 1U);
        }
        g_seq_project.tracks[tr].ui_page = ui_page;

        for (seq_step_id_t st = 0U; st < SEQ_MAX_STEPS; ++st)
        {
            const seq_step_t *in_step = &project->tracks[tr].steps[st];
            g_seq_project.tracks[tr].steps[st].trig = (in_step->trig != 0U) ? 1U : 0U;

            uint16_t idx = in_step->lock_head;
            uint8_t imported = 0U;
            uint16_t guard = 0U;

            while ((idx != SEQ_LOCK_NONE) &&
                   (idx < (uint16_t)SEQ_PLOCK_POOL_CAP) &&
                   (imported < in_step->lock_count) &&
                   (imported < SEQ_STEP_MAX_LOCKS) &&
                   (guard < (uint16_t)SEQ_PLOCK_POOL_CAP))
            {
                if (pool_seen[idx] != 0U)
                {
                    break;
                }

                pool_seen[idx] = 1U;
                guard++;

                const seq_plock_entry_t *entry = &project->pool[idx];
                (void)seq_model_step_plock_upsert(tr,
                                                  st,
                                                  entry->set_id,
                                                  entry->param8,
                                                  entry->value16,
                                                  entry->flags);

                idx = entry->next;
                imported++;
            }
        }
    }

    return 1U;
}

uint8_t seq_model_get_trig(seq_track_id_t track, seq_step_id_t step)
{
    if ((seq_model_track_is_valid(track) == 0U) || (seq_model_step_is_valid(step) == 0U))
    {
        return 0U;
    }

    return g_seq_project.tracks[track].steps[step].trig;
}

void seq_model_toggle_trig(seq_track_id_t track, seq_step_id_t step)
{
    if ((seq_model_track_is_valid(track) == 0U) || (seq_model_step_is_valid(step) == 0U))
    {
        return;
    }

    seq_step_t *const s = &g_seq_project.tracks[track].steps[step];
    s->trig = (s->trig == 0U) ? 1U : 0U;
}

void seq_model_set_trig(seq_track_id_t track, seq_step_id_t step, uint8_t trig)
{
    if ((seq_model_track_is_valid(track) == 0U) || (seq_model_step_is_valid(step) == 0U))
    {
        return;
    }

    g_seq_project.tracks[track].steps[step].trig = (trig != 0U) ? 1U : 0U;
}

uint8_t seq_model_get_track_page(seq_track_id_t track)
{
    if (seq_model_track_is_valid(track) == 0U)
    {
        return 0U;
    }

    return g_seq_project.tracks[track].ui_page;
}

void seq_model_set_track_page(seq_track_id_t track, uint8_t page)
{
    if (seq_model_track_is_valid(track) == 0U)
    {
        return;
    }

    if (page >= SEQ_PAGE_COUNT)
    {
        page = (uint8_t)(SEQ_PAGE_COUNT - 1U);
    }

    g_seq_project.tracks[track].ui_page = page;
}

uint8_t seq_model_step_plock_find(seq_track_id_t track,
                                  seq_step_id_t step,
                                  uint8_t set_id,
                                  seq_param8_t param8,
                                  seq_plock_entry_t *out_entry)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if ((s == 0) || (out_entry == 0))
    {
        return 0U;
    }

    const uint16_t idx = seq_model_find_lock_idx(s, set_id, param8, 0);
    if (idx == SEQ_LOCK_NONE)
    {
        return 0U;
    }

    *out_entry = g_seq_project.pool[idx];
    return 1U;
}

seq_plock_op_status_t seq_model_step_plock_upsert(seq_track_id_t track,
                                                   seq_step_id_t step,
                                                   uint8_t set_id,
                                                   seq_param8_t param8,
                                                   seq_value16_t value16,
                                                   uint8_t flags)
{
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    if (s == 0)
    {
        return SEQ_PLOCK_OP_INVALID;
    }

    if (seq_param_iface_is_set_plockable(set_id) == 0U)
    {
        return SEQ_PLOCK_OP_SET_NOT_PLOCKABLE;
    }

    const uint16_t existing_idx = seq_model_find_lock_idx(s, set_id, param8, 0);
    if (existing_idx != SEQ_LOCK_NONE)
    {
        g_seq_project.pool[existing_idx].value16 = value16;
        g_seq_project.pool[existing_idx].flags = flags;
        return SEQ_PLOCK_OP_UPDATED;
    }

    if (s->lock_count >= SEQ_STEP_MAX_LOCKS)
    {
        return SEQ_PLOCK_OP_STEP_FULL;
    }

    const uint16_t new_idx = seq_model_alloc_lock_node();
    if (new_idx == SEQ_LOCK_NONE)
    {
        return SEQ_PLOCK_OP_POOL_EMPTY;
    }

    seq_plock_entry_t *const entry = &g_seq_project.pool[new_idx];
    entry->set_id = set_id;
    entry->param8 = param8;
    entry->value16 = value16;
    entry->flags = flags;
    entry->reserved = 0U;
    entry->next = s->lock_head;

    s->lock_head = new_idx;
    s->lock_count++;
    s->lock_set_mask |= seq_param_iface_set_to_mask(set_id);

    return SEQ_PLOCK_OP_CREATED;
}

seq_plock_op_status_t seq_model_step_plock_delete(seq_track_id_t track,
                                                   seq_step_id_t step,
                                                   uint8_t set_id,
                                                   seq_param8_t param8)
{
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    if (s == 0)
    {
        return SEQ_PLOCK_OP_INVALID;
    }

    uint16_t prev = SEQ_LOCK_NONE;
    const uint16_t idx = seq_model_find_lock_idx(s, set_id, param8, &prev);
    if (idx == SEQ_LOCK_NONE)
    {
        return SEQ_PLOCK_OP_NOT_FOUND;
    }

    const uint16_t next = g_seq_project.pool[idx].next;
    if (prev == SEQ_LOCK_NONE)
    {
        s->lock_head = next;
    }
    else
    {
        g_seq_project.pool[prev].next = next;
    }

    if (s->lock_count > 0U)
    {
        s->lock_count--;
    }

    s->lock_set_mask = seq_model_compute_step_mask(s);
    seq_model_free_lock_node(idx);

    return SEQ_PLOCK_OP_DELETED;
}

void seq_model_step_plock_clear(seq_track_id_t track, seq_step_id_t step)
{
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    if (s == 0)
    {
        return;
    }

    uint16_t idx = s->lock_head;
    while (idx != SEQ_LOCK_NONE)
    {
        const uint16_t next = g_seq_project.pool[idx].next;
        seq_model_free_lock_node(idx);
        idx = next;
    }

    s->lock_head = SEQ_LOCK_NONE;
    s->lock_count = 0U;
    s->lock_set_mask = 0U;
}

uint8_t seq_model_step_plock_count(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if (s == 0)
    {
        return 0U;
    }

    return s->lock_count;
}

uint8_t seq_model_step_plock_get_at(seq_track_id_t track,
                                    seq_step_id_t step,
                                    uint8_t ordinal,
                                    seq_plock_entry_t *out_entry)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if ((s == 0) || (out_entry == 0) || (ordinal >= s->lock_count))
    {
        return 0U;
    }

    uint8_t index = 0U;
    uint16_t idx = s->lock_head;
    while (idx != SEQ_LOCK_NONE)
    {
        if (index == ordinal)
        {
            *out_entry = g_seq_project.pool[idx];
            return 1U;
        }

        index++;
        idx = g_seq_project.pool[idx].next;
    }

    return 0U;
}
