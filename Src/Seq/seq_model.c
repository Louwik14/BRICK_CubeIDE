/*
 * Module: seq_model
 * Role: Modèle de données central du séquenceur (project/tracks/steps/plock pool).
 * Responsibilities: CRUD sur trigs/pages/plocks, validations d'index,
 * allocation/libération du pool de locks et accès cohérent à l'état persistant.
 * Integration: backend partagé par édition, runtime, persistence et modules Seq.
 */
#include "Seq/seq_model.h"

#include <string.h>

#include "stm32h7xx_hal.h"
#include "Storage/memory_layout.h"
#include "Seq/seq_param_iface.h"

#define SEQ_LOCK_NONE 0xFFFFU

SEQ_STATE_D2 static seq_project_data_t g_seq_project;

static uint32_t seq_model_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void seq_model_exit_critical(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static uint8_t seq_model_track_is_valid(seq_track_id_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
}

static uint8_t seq_model_clamp_playback_length(uint8_t length_steps)
{
    if (length_steps == 0U)
    {
        return 1U;
    }
    if (length_steps > SEQ_MAX_STEPS)
    {
        return SEQ_MAX_STEPS;
    }
    return length_steps;
}

static uint8_t seq_model_step_is_valid(seq_step_id_t step)
{
    return (step < seq_model_get_editable_step_capacity()) ? 1U : 0U;
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

static uint16_t seq_model_alloc_lock_node(seq_track_id_t track)
{
    if ((seq_model_track_is_valid(track) == 0U) ||
        (g_seq_project.free_count[track] == 0U) ||
        (g_seq_project.free_head[track] == SEQ_LOCK_NONE))
    {
        return SEQ_LOCK_NONE;
    }

    const uint16_t idx = g_seq_project.free_head[track];
    g_seq_project.free_head[track] = g_seq_project.pool[track][idx].next;
    g_seq_project.pool[track][idx].next = SEQ_LOCK_NONE;
    g_seq_project.free_count[track]--;
    return idx;
}

static void seq_model_free_lock_node(seq_track_id_t track, uint16_t idx)
{
    if ((seq_model_track_is_valid(track) == 0U) ||
        (idx >= (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK))
    {
        return;
    }

    g_seq_project.pool[track][idx].next = g_seq_project.free_head[track];
    g_seq_project.free_head[track] = idx;
    g_seq_project.free_count[track]++;
}

static uint16_t seq_model_find_lock_idx(seq_track_id_t track,
                                        const seq_step_t *step,
                                        uint8_t set_id,
                                        seq_param_slot_t param_slot,
                                        uint16_t *out_prev)
{
    if (out_prev != 0)
    {
        *out_prev = SEQ_LOCK_NONE;
    }

    uint16_t prev = SEQ_LOCK_NONE;
    uint16_t idx = step->lock_head;
    uint16_t guard = 0U;
    while (idx != SEQ_LOCK_NONE)
    {
        if (guard++ >= (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK)
        {
            break;
        }

        if (idx >= (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK)
        {
            break;
        }

        const seq_plock_entry_t *entry = &g_seq_project.pool[track][idx];
        if ((entry->set_id == set_id) && (entry->param_slot == param_slot))
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

static uint8_t seq_model_compute_step_mask(seq_track_id_t track, const seq_step_t *step)
{
    uint8_t mask = 0U;

    uint16_t idx = step->lock_head;
    uint16_t guard = 0U;
    while (idx != SEQ_LOCK_NONE)
    {
        if (guard++ >= (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK)
        {
            break;
        }

        if (idx >= (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK)
        {
            break;
        }

        const seq_plock_entry_t *entry = &g_seq_project.pool[track][idx];
        mask |= seq_param_iface_set_to_mask(entry->set_id);
        idx = entry->next;
    }

    return mask;
}

static void seq_model_step_scan_lock_sets(seq_track_id_t track,
                                          const seq_step_t *step,
                                          uint8_t *out_has_play_plock,
                                          uint8_t *out_has_non_play_plock)
{
    uint8_t has_play_plock = 0U;
    uint8_t has_non_play_plock = 0U;

    if ((seq_model_track_is_valid(track) == 0U) || (step == 0))
    {
        if (out_has_play_plock != 0)
        {
            *out_has_play_plock = 0U;
        }
        if (out_has_non_play_plock != 0)
        {
            *out_has_non_play_plock = 0U;
        }
        return;
    }

    uint16_t idx = step->lock_head;
    uint16_t guard = 0U;
    while (idx != SEQ_LOCK_NONE)
    {
        if (guard++ >= (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK)
        {
            break;
        }

        if (idx >= (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK)
        {
            break;
        }

        const seq_plock_entry_t *entry = &g_seq_project.pool[track][idx];
        if (entry->set_id == (uint8_t)SEQ_PLOCK_SET_PLAY)
        {
            has_play_plock = 1U;
        }
        else
        {
            has_non_play_plock = 1U;
        }

        idx = entry->next;
    }

    if (out_has_play_plock != 0)
    {
        *out_has_play_plock = has_play_plock;
    }
    if (out_has_non_play_plock != 0)
    {
        *out_has_non_play_plock = has_non_play_plock;
    }
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

        g_seq_project.free_head[tr] = 0U;
        g_seq_project.free_count[tr] = SEQ_PLOCK_POOL_CAP_PER_TRACK;

        for (uint16_t i = 0U; i < (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK; ++i)
        {
            g_seq_project.pool[tr][i].next =
                (i + 1U < (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK) ? (uint16_t)(i + 1U) : SEQ_LOCK_NONE;
        }
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

    for (seq_track_id_t tr = 0U; tr < SEQ_TRACK_COUNT; ++tr)
    {
        uint8_t pool_seen[SEQ_PLOCK_POOL_CAP_PER_TRACK];
        memset(pool_seen, 0, sizeof(pool_seen));
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
                   (idx < (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK) &&
                   (imported < in_step->lock_count) &&
                   (imported < SEQ_STEP_MAX_LOCKS) &&
                   (guard < (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK))
            {
                if (pool_seen[idx] != 0U)
                {
                    break;
                }

                pool_seen[idx] = 1U;
                guard++;

                const seq_plock_entry_t *entry = &project->pool[tr][idx];
                (void)seq_model_step_plock_upsert(tr,
                                                  st,
                                                  entry->set_id,
                                                  entry->param_slot,
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
    const uint32_t primask = seq_model_enter_critical();
    s->trig = (s->trig == 0U) ? 1U : 0U;
    seq_model_exit_critical(primask);
}

void seq_model_set_trig(seq_track_id_t track, seq_step_id_t step, uint8_t trig)
{
    if ((seq_model_track_is_valid(track) == 0U) || (seq_model_step_is_valid(step) == 0U))
    {
        return;
    }

    const uint32_t primask = seq_model_enter_critical();
    g_seq_project.tracks[track].steps[step].trig = (trig != 0U) ? 1U : 0U;
    seq_model_exit_critical(primask);
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

void seq_model_set_track_length(seq_track_id_t track, uint8_t length_steps)
{
    if (seq_model_track_is_valid(track) == 0U)
    {
        return;
    }

    g_seq_project.tracks[track].length_steps = seq_model_clamp_playback_length(length_steps);
}

uint8_t seq_model_get_track_length(seq_track_id_t track)
{
    return seq_model_get_track_playback_length(track);
}

uint8_t seq_model_get_editable_step_capacity(void)
{
    return SEQ_MAX_STEPS;
}

uint8_t seq_model_is_step_editable_index(seq_step_id_t step)
{
    return (step < seq_model_get_editable_step_capacity()) ? 1U : 0U;
}

uint8_t seq_model_get_track_playback_length(seq_track_id_t track)
{
    if (seq_model_track_is_valid(track) == 0U)
    {
        return SEQ_MAX_STEPS;
    }

    return seq_model_clamp_playback_length(g_seq_project.tracks[track].length_steps);
}

uint8_t seq_model_is_step_in_track_playback_window(seq_track_id_t track, seq_step_id_t step)
{
    if (seq_model_track_is_valid(track) == 0U)
    {
        return 0U;
    }

    return (step < seq_model_get_track_playback_length(track)) ? 1U : 0U;
}

uint8_t seq_model_step_is_active(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if (s == 0)
    {
        return 0U;
    }

    return (s->trig != 0U) ? 1U : 0U;
}

seq_step_content_t seq_model_get_step_content(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if (s == 0)
    {
        return SEQ_STEP_CONTENT_EMPTY;
    }

    uint8_t has_play_plock = 0U;
    uint8_t has_non_play_plock = 0U;
    seq_model_step_scan_lock_sets(track, s, &has_play_plock, &has_non_play_plock);

    if ((has_play_plock == 0U) && (has_non_play_plock == 0U))
    {
        return SEQ_STEP_CONTENT_EMPTY;
    }
    if ((has_play_plock != 0U) && (has_non_play_plock != 0U))
    {
        return SEQ_STEP_CONTENT_PLAY_AND_NON_PLAY;
    }
    if (has_play_plock != 0U)
    {
        return SEQ_STEP_CONTENT_PLAY_ONLY;
    }

    return SEQ_STEP_CONTENT_NON_PLAY_ONLY;
}

seq_step_visual_t seq_model_get_step_visual(seq_track_id_t track, seq_step_id_t step)
{
    if (seq_model_step_is_active(track, step) == 0U)
    {
        return SEQ_STEP_VISUAL_OFF;
    }

    const seq_step_content_t content = seq_model_get_step_content(track, step);
    if ((content == SEQ_STEP_CONTENT_PLAY_ONLY) || (content == SEQ_STEP_CONTENT_PLAY_AND_NON_PLAY))
    {
        return SEQ_STEP_VISUAL_GREEN;
    }
    if (content == SEQ_STEP_CONTENT_NON_PLAY_ONLY)
    {
        return SEQ_STEP_VISUAL_BLUE;
    }

    return SEQ_STEP_VISUAL_OFF;
}

seq_step_state_t seq_model_get_step_state(seq_track_id_t track, seq_step_id_t step)
{
    if (seq_model_step_is_active(track, step) == 0U)
    {
        return SEQ_STEP_STATE_EMPTY;
    }

    const seq_step_content_t content = seq_model_get_step_content(track, step);
    if (content == SEQ_STEP_CONTENT_NON_PLAY_ONLY)
    {
        return SEQ_STEP_STATE_PARAM_LOCK_ONLY;
    }
    if (content == SEQ_STEP_CONTENT_PLAY_AND_NON_PLAY)
    {
        return SEQ_STEP_STATE_NOTE_WITH_PLOCKS;
    }

    return SEQ_STEP_STATE_NOTE;
}

uint8_t seq_model_step_has_play_plock(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if (s == 0)
    {
        return 0U;
    }

    uint8_t has_play_plock = 0U;
    seq_model_step_scan_lock_sets(track, s, &has_play_plock, 0);
    return has_play_plock;
}

uint8_t seq_model_step_has_non_play_plock(seq_track_id_t track, seq_step_id_t step)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if (s == 0)
    {
        return 0U;
    }

    uint8_t has_non_play_plock = 0U;
    seq_model_step_scan_lock_sets(track, s, 0, &has_non_play_plock);
    return has_non_play_plock;
}

uint8_t seq_model_step_is_empty(seq_track_id_t track, seq_step_id_t step)
{
    return (seq_model_get_step_content(track, step) == SEQ_STEP_CONTENT_EMPTY) ? 1U : 0U;
}

uint8_t seq_model_step_is_quick_note_eligible(seq_track_id_t track, seq_step_id_t step)
{
    return (uint8_t)((seq_model_step_is_active(track, step) == 0U)
                     && (seq_model_step_is_empty(track, step) != 0U));
}

uint8_t seq_model_step_plock_find(seq_track_id_t track,
                                  seq_step_id_t step,
                                  uint8_t set_id,
                                  seq_param_slot_t param_slot,
                                  seq_plock_entry_t *out_entry)
{
    const seq_step_t *const s = seq_model_get_step_const(track, step);
    if ((s == 0) || (out_entry == 0))
    {
        return 0U;
    }

    const uint16_t idx = seq_model_find_lock_idx(track, s, set_id, param_slot, 0);
    if (idx == SEQ_LOCK_NONE)
    {
        return 0U;
    }

    *out_entry = g_seq_project.pool[track][idx];
    return 1U;
}

seq_plock_op_status_t seq_model_step_plock_upsert(seq_track_id_t track,
                                                   seq_step_id_t step,
                                                   uint8_t set_id,
                                                   seq_param_slot_t param_slot,
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

    const uint32_t primask = seq_model_enter_critical();

    const uint16_t existing_idx = seq_model_find_lock_idx(track, s, set_id, param_slot, 0);
    if (existing_idx != SEQ_LOCK_NONE)
    {
        g_seq_project.pool[track][existing_idx].value16 = value16;
        g_seq_project.pool[track][existing_idx].flags = flags;
        seq_model_exit_critical(primask);
        return SEQ_PLOCK_OP_UPDATED;
    }

    if (s->lock_count >= SEQ_STEP_MAX_LOCKS)
    {
        seq_model_exit_critical(primask);
        return SEQ_PLOCK_OP_STEP_FULL;
    }

    const uint16_t new_idx = seq_model_alloc_lock_node(track);
    if (new_idx == SEQ_LOCK_NONE)
    {
        seq_model_exit_critical(primask);
        return SEQ_PLOCK_OP_POOL_EMPTY;
    }

    seq_plock_entry_t *const entry = &g_seq_project.pool[track][new_idx];
    entry->set_id = set_id;
    entry->param_slot = param_slot;
    entry->value16 = value16;
    entry->flags = flags;
    entry->reserved = 0U;
    entry->next = s->lock_head;

    s->lock_head = new_idx;
    s->lock_count++;
    s->lock_set_mask |= seq_param_iface_set_to_mask(set_id);

    seq_model_exit_critical(primask);
    return SEQ_PLOCK_OP_CREATED;
}

seq_plock_op_status_t seq_model_step_plock_delete(seq_track_id_t track,
                                                   seq_step_id_t step,
                                                   uint8_t set_id,
                                                   seq_param_slot_t param_slot)
{
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    if (s == 0)
    {
        return SEQ_PLOCK_OP_INVALID;
    }

    const uint32_t primask = seq_model_enter_critical();
    uint16_t prev = SEQ_LOCK_NONE;
    const uint16_t idx = seq_model_find_lock_idx(track, s, set_id, param_slot, &prev);
    if (idx == SEQ_LOCK_NONE)
    {
        seq_model_exit_critical(primask);
        return SEQ_PLOCK_OP_NOT_FOUND;
    }

    const uint16_t next = g_seq_project.pool[track][idx].next;
    if (prev == SEQ_LOCK_NONE)
    {
        s->lock_head = next;
    }
    else
    {
        g_seq_project.pool[track][prev].next = next;
    }

    if (s->lock_count > 0U)
    {
        s->lock_count--;
    }

    s->lock_set_mask = seq_model_compute_step_mask(track, s);
    seq_model_free_lock_node(track, idx);

    seq_model_exit_critical(primask);
    return SEQ_PLOCK_OP_DELETED;
}

void seq_model_step_plock_clear(seq_track_id_t track, seq_step_id_t step)
{
    seq_step_t *const s = seq_model_get_step_mut(track, step);
    if (s == 0)
    {
        return;
    }

    const uint32_t primask = seq_model_enter_critical();
    uint16_t idx = s->lock_head;
    while (idx != SEQ_LOCK_NONE)
    {
        if (idx >= (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK)
        {
            break;
        }

        const uint16_t next = g_seq_project.pool[track][idx].next;
        seq_model_free_lock_node(track, idx);
        idx = next;
    }

    s->lock_head = SEQ_LOCK_NONE;
    s->lock_count = 0U;
    s->lock_set_mask = 0U;
    seq_model_exit_critical(primask);
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
    uint16_t guard = 0U;
    while (idx != SEQ_LOCK_NONE)
    {
        if (guard++ >= (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK)
        {
            return 0U;
        }

        if (idx >= (uint16_t)SEQ_PLOCK_POOL_CAP_PER_TRACK)
        {
            return 0U;
        }

        if (index == ordinal)
        {
            *out_entry = g_seq_project.pool[track][idx];
            return 1U;
        }

        index++;
        idx = g_seq_project.pool[track][idx].next;
    }

    return 0U;
}

