#include "Seq/seq_boundary_engine.h"

#include <string.h>

#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"

typedef struct
{
    uint8_t set_id;
    seq_param8_t param8;
    seq_value16_t value16;
    seq_value16_t base_value16;
} seq_boundary_engine_step_lock_t;

static uint8_t seq_boundary_engine_lock_equals(const seq_runtime_active_lock_t *active,
                                               uint8_t set_id,
                                               seq_param8_t param8)
{
    return ((active->active != 0U) && (active->set_id == set_id) && (active->param8 == param8)) ? 1U : 0U;
}

static uint8_t seq_boundary_engine_find_next_lock(const seq_boundary_engine_step_lock_t *locks,
                                                  uint8_t count,
                                                  uint8_t set_id,
                                                  seq_param8_t param8,
                                                  uint8_t *out_index)
{
    for (uint8_t i = 0U; i < count; ++i)
    {
        if ((locks[i].set_id == set_id) && (locks[i].param8 == param8))
        {
            if (out_index != 0)
            {
                *out_index = i;
            }
            return 1U;
        }
    }

    return 0U;
}

static uint8_t seq_boundary_engine_collect_step_locks(seq_track_id_t track,
                                                      seq_step_id_t step,
                                                      seq_boundary_engine_step_lock_t *out_locks,
                                                      uint8_t *out_count)
{
    if ((out_locks == 0) || (out_count == 0) || (track >= SEQ_TRACK_COUNT) || (step >= SEQ_MAX_STEPS))
    {
        return 0U;
    }

    uint8_t count = 0U;
    const uint8_t lock_count = seq_model_step_plock_count(track, step);
    for (uint8_t i = 0U; i < lock_count; ++i)
    {
        seq_plock_entry_t entry;
        if (seq_model_step_plock_get_at(track, step, i, &entry) == 0U)
        {
            continue;
        }

        if (seq_param_iface_is_param_supported(track, entry.set_id, entry.param8) == 0U)
        {
            continue;
        }

        if (count >= SEQ_STEP_MAX_LOCKS)
        {
            break;
        }

        out_locks[count].set_id = entry.set_id;
        out_locks[count].param8 = entry.param8;
        out_locks[count].value16 = entry.value16;
        out_locks[count].base_value16 = 0U;
        count++;
    }

    *out_count = count;
    return 1U;
}

void seq_boundary_engine_restore_all_active_locks(seq_runtime_state_t *state,
                                                  seq_track_id_t track)
{
    if ((state == 0) || (track >= SEQ_TRACK_COUNT))
    {
        return;
    }

    seq_runtime_active_lock_t *const active = state->active_locks[track];
    const uint8_t active_count = state->active_lock_count[track];

    for (uint8_t i = 0U; i < active_count; ++i)
    {
        if (active[i].active == 0U)
        {
            continue;
        }

        seq_param_iface_restore_base(track,
                                     active[i].set_id,
                                     active[i].param8,
                                     active[i].base_value16);
    }

    memset(active, 0, sizeof(state->active_locks[track]));
    state->active_lock_count[track] = 0U;
}

static void seq_boundary_engine_step_apply_restore(seq_runtime_state_t *state,
                                                   seq_track_id_t track,
                                                   uint8_t has_prev,
                                                   seq_step_id_t step_curr)
{
    seq_boundary_engine_step_lock_t next_locks[SEQ_STEP_MAX_LOCKS];
    uint8_t next_count = 0U;
    if ((state == 0)
        || (seq_boundary_engine_collect_step_locks(track, step_curr, next_locks, &next_count) == 0U))
    {
        return;
    }

    seq_runtime_active_lock_t *const active = state->active_locks[track];
    uint8_t active_count = state->active_lock_count[track];

    if (has_prev != 0U)
    {
        for (uint8_t i = 0U; i < active_count; ++i)
        {
            if (active[i].active == 0U)
            {
                continue;
            }

            if (seq_boundary_engine_find_next_lock(next_locks, next_count, active[i].set_id, active[i].param8, 0) == 0U)
            {
                seq_param_iface_restore_base(track,
                                             active[i].set_id,
                                             active[i].param8,
                                             active[i].base_value16);
            }
        }
    }

    for (uint8_t i = 0U; i < next_count; ++i)
    {
        uint8_t found_prev = 0U;
        if (has_prev != 0U)
        {
            for (uint8_t j = 0U; j < active_count; ++j)
            {
                if (seq_boundary_engine_lock_equals(&active[j], next_locks[i].set_id, next_locks[i].param8) != 0U)
                {
                    next_locks[i].base_value16 = active[j].base_value16;
                    found_prev = 1U;
                    break;
                }
            }
        }

        if (found_prev == 0U)
        {
            seq_value16_t base_value16 = 0U;
            if (seq_param_iface_get_base_value(track, next_locks[i].set_id, next_locks[i].param8, &base_value16) == 0U)
            {
                continue;
            }
            next_locks[i].base_value16 = base_value16;
        }

        seq_param_iface_apply_lock(track,
                                   next_locks[i].set_id,
                                   next_locks[i].param8,
                                   next_locks[i].value16);
    }

    memset(active, 0, sizeof(state->active_locks[track]));
    state->active_lock_count[track] = 0U;

    for (uint8_t i = 0U; i < next_count; ++i)
    {
        active[i].active = 1U;
        active[i].set_id = next_locks[i].set_id;
        active[i].param8 = next_locks[i].param8;
        active[i].base_value16 = next_locks[i].base_value16;
        state->active_lock_count[track]++;
    }
}

void seq_boundary_engine_process(seq_runtime_state_t *state,
                                 seq_boundary_hit_t *out_hits,
                                 uint8_t max_hits,
                                 uint8_t *out_hit_count)
{
    if (out_hit_count != 0)
    {
        *out_hit_count = 0U;
    }

    if ((state == 0) || (out_hits == 0) || (out_hit_count == 0) || (max_hits == 0U))
    {
        return;
    }

    uint8_t hit_count = 0U;
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const seq_step_id_t current_step = state->play_step[track];

        if ((state->prev_step_valid[track] == 0U)
            || (state->prev_step[track] != current_step))
        {
            seq_boundary_engine_step_apply_restore(state,
                                                   track,
                                                   state->prev_step_valid[track],
                                                   current_step);

            state->prev_step[track] = current_step;
            state->prev_step_valid[track] = 1U;

            if (hit_count < max_hits)
            {
                out_hits[hit_count].track = track;
                out_hits[hit_count].step = current_step;
                hit_count++;
            }
        }
    }

    *out_hit_count = hit_count;
}

void seq_boundary_engine_advance_one_step(seq_runtime_state_t *state)
{
    if (state == 0)
    {
        return;
    }

    const seq_project_data_t *const project = seq_model_get_project();

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        uint8_t length = project->tracks[track].length_steps;
        if ((length == 0U) || (length > SEQ_MAX_STEPS))
        {
            length = SEQ_MAX_STEPS;
        }

        uint8_t next = (uint8_t)(state->play_step[track] + 1U);
        if (next >= length)
        {
            next = 0U;
        }

        state->play_step[track] = next;
    }
}
