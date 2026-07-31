/*
 * Module: seq_boundary_engine
 * Role: Noyau boundary des parameter-locks au passage de pas.
 * Responsibilities: capture/restitue valeurs de base, applique/verifie locks actifs,
 * gère les transitions step->step sans casser l'état runtime des paramètres.
 * Integration: appelé par seq_runtime autour du scheduling PLAY; ne pilote ni clock ni transport.
 */
#define SEQ_BOUNDARY_ENGINE_IMPLEMENTATION 1
#include "Seq/seq_boundary_engine.h"

#include <string.h>

#include "Core/track_runtime.h"
#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_plock_route.h"
#include "Seq/seq_runtime_control.h"

typedef struct
{
    seq_track_id_t source_track;
    seq_step_id_t source_step;
    uint8_t set_id;
    uint8_t status;
    seq_param_slot_t source_slot;
    seq_param_slot_t target_slot;
    seq_value16_t value16;
    seq_value16_t base_value16;
} seq_boundary_engine_step_lock_t;

typedef enum
{
    SEQ_BOUNDARY_LOCK_LOCAL = 0,
    SEQ_BOUNDARY_LOCK_LINKED = 1
} seq_boundary_engine_lock_status_t;

static uint8_t seq_boundary_engine_track_length(seq_track_id_t track)
{
    return seq_model_get_track_playback_length(track);
}

static uint8_t seq_boundary_engine_lock_equals(const seq_runtime_active_lock_t *active,
                                               uint8_t set_id,
                                               seq_param_slot_t param_slot)
{
    return ((active->active != 0U) && (active->set_id == set_id) && (active->param_slot == param_slot)) ? 1U : 0U;
}

static uint8_t seq_boundary_engine_find_next_lock(const seq_boundary_engine_step_lock_t *locks,
                                                  uint8_t count,
                                                  uint8_t set_id,
                                                  seq_param_slot_t param_slot,
                                                  uint8_t *out_index)
{
    for (uint8_t i = 0U; i < count; ++i)
    {
        if ((locks[i].set_id == set_id) && (locks[i].target_slot == param_slot))
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

static uint8_t seq_boundary_engine_set_is_seq_linked(uint8_t set_id)
{
    return (uint8_t)((set_id == (uint8_t)SEQ_PLOCK_SET_TONE)
                     || (set_id == (uint8_t)SEQ_PLOCK_SET_COLORS)
                     || (set_id == (uint8_t)SEQ_PLOCK_SET_MOD)
                     || (set_id == (uint8_t)SEQ_PLOCK_SET_MIX));
}

static void seq_boundary_engine_prioritize_md_model(seq_track_id_t target_track,
                                                    seq_boundary_engine_step_lock_t *locks,
                                                    uint8_t count)
{
    for (uint8_t i = 0U; i < count; ++i)
    {
        param_id_t param = PARAM_COUNT;
        if ((locks[i].set_id != (uint8_t)SEQ_PLOCK_SET_TONE)
                || (seq_param_iface_slot_to_param(target_track,
                                                  locks[i].set_id,
                                                  locks[i].target_slot,
                                                  &param) == 0U)
                || (param != PARAM_DRUM_MD_MODEL))
        {
            continue;
        }

        const seq_boundary_engine_step_lock_t model_lock = locks[i];
        for (uint8_t move = i; move > 0U; --move)
        {
            locks[move] = locks[move - 1U];
        }
        locks[0] = model_lock;
        return;
    }
}

static uint8_t seq_boundary_engine_collect_non_play_locks(seq_track_id_t target_track,
                                                          seq_track_id_t source_track,
                                                          seq_step_id_t source_step,
                                                          uint8_t linked,
                                                          seq_boundary_engine_step_lock_t *out_locks,
                                                          uint8_t *out_count)
{
    if ((out_locks == 0)
        || (out_count == 0)
        || (target_track >= SEQ_TRACK_COUNT)
        || (source_track >= SEQ_TRACK_COUNT)
        || (seq_model_is_step_editable_index(source_step) == 0U))
    {
        return 0U;
    }

    *out_count = 0U;
    track_runtime_refresh_track(target_track);

    if ((source_track >= SEQ_TRACK_COUNT)
        || (seq_model_is_step_editable_index(source_step) == 0U)
        || (seq_model_step_is_active(source_track, source_step) == 0U))
    {
        return 1U;
    }

    seq_plock_entry_t entries[SEQ_STEP_MAX_LOCKS];
    uint8_t entry_count = 0U;
    if (seq_model_step_plock_collect(source_track, source_step, entries, SEQ_STEP_MAX_LOCKS, &entry_count) == 0U)
    {
        return 0U;
    }

    uint8_t count = 0U;
    for (uint8_t i = 0U; i < entry_count; ++i)
    {
        const seq_plock_entry_t *const entry = &entries[i];
        if (seq_boundary_engine_set_is_seq_linked(entry->set_id) == 0U)
        {
            continue;
        }

        seq_param_slot_t target_slot = entry->param_slot;
        if (linked != 0U)
        {
            param_id_t source_param = PARAM_COUNT;
            if ((seq_param_iface_slot_to_param(source_track,
                                               entry->set_id,
                                               entry->param_slot,
                                               &source_param) == 0U)
                    || (seq_param_iface_param_to_slot(target_track,
                                                     entry->set_id,
                                                     source_param,
                                                     &target_slot) == 0U))
            {
                continue;
            }
        }

        if (seq_param_iface_slot_is_supported(target_track, entry->set_id, target_slot) == 0U)
        {
            continue;
        }

        if (count >= SEQ_STEP_MAX_LOCKS)
        {
            break;
        }

        out_locks[count].source_track = source_track;
        out_locks[count].source_step = source_step;
        out_locks[count].set_id = entry->set_id;
        out_locks[count].status = (linked != 0U)
            ? (uint8_t)SEQ_BOUNDARY_LOCK_LINKED
            : (uint8_t)SEQ_BOUNDARY_LOCK_LOCAL;
        out_locks[count].source_slot = entry->param_slot;
        out_locks[count].target_slot = target_slot;
        out_locks[count].value16 = entry->value16;
        out_locks[count].base_value16 = 0U;
        count++;
    }

    seq_boundary_engine_prioritize_md_model(target_track, out_locks, count);
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
                                     active[i].param_slot,
                                     active[i].base_value16);
    }

    memset(active, 0, sizeof(state->active_locks[track]));
    state->active_lock_count[track] = 0U;
}

void seq_boundary_engine_invalidate_track(seq_runtime_state_t *state,
                                          seq_track_id_t track)
{
    if ((state == 0) || (track >= SEQ_TRACK_COUNT))
    {
        return;
    }

    state->prev_step_valid[track] = 0U;
}

static void seq_boundary_engine_step_apply_restore(seq_runtime_state_t *state,
                                                   seq_track_id_t track,
                                                   uint8_t has_prev,
                                                   seq_track_id_t source_track,
                                                   seq_step_id_t source_step,
                                                   uint8_t linked)
{
    seq_boundary_engine_step_lock_t next_locks[SEQ_STEP_MAX_LOCKS];
    uint8_t next_count = 0U;
    if ((state == 0)
        || (seq_boundary_engine_collect_non_play_locks(track,
                                                       source_track,
                                                       source_step,
                                                       linked,
                                                       next_locks,
                                                       &next_count) == 0U))
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

            if (seq_boundary_engine_find_next_lock(next_locks, next_count, active[i].set_id, active[i].param_slot, 0) == 0U)
            {
                seq_param_iface_restore_base(track,
                                             active[i].set_id,
                                             active[i].param_slot,
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
                if (seq_boundary_engine_lock_equals(&active[j], next_locks[i].set_id, next_locks[i].target_slot) != 0U)
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
            if (seq_param_iface_get_base_value(track, next_locks[i].set_id, next_locks[i].target_slot, &base_value16) == 0U)
            {
                continue;
            }
            next_locks[i].base_value16 = base_value16;
        }

        seq_param_iface_apply_lock(track,
                                   next_locks[i].set_id,
                                   next_locks[i].target_slot,
                                   next_locks[i].value16);
    }

    memset(active, 0, sizeof(state->active_locks[track]));
    state->active_lock_count[track] = 0U;

    for (uint8_t i = 0U; i < next_count; ++i)
    {
        active[i].active = 1U;
        active[i].set_id = next_locks[i].set_id;
        active[i].param_slot = next_locks[i].target_slot;
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
        const uint8_t length = seq_boundary_engine_track_length(track);
        seq_step_id_t current_step = state->play_step[track];
        if (current_step >= length)
        {
            current_step = 0U;
            state->play_step[track] = 0U;
        }

        if ((state->prev_step_valid[track] == 0U)
            || (state->prev_step[track] != current_step))
        {
            if (seq_plock_route_target_is_seq_link_slave(track) != 0U)
            {
                continue;
            }

            seq_plock_route_t route;
            if (seq_plock_route_resolve(track, current_step, &route) == 0U)
            {
                continue;
            }

            if ((route.group_master != 0U) && (route.linked != 0U))
            {
                for (uint8_t i = 0U; i < route.target_count; ++i)
                {
                    const seq_track_id_t target = route.targets[i];
                    seq_boundary_engine_step_apply_restore(state,
                                                           target,
                                                           state->prev_step_valid[target],
                                                           route.source_track,
                                                           route.source_step,
                                                           route.linked);

                    state->prev_step[target] = route.source_step;
                    state->prev_step_valid[target] = 1U;
                }
            }
            else
            {
                seq_boundary_engine_step_apply_restore(state,
                                                       track,
                                                       state->prev_step_valid[track],
                                                       track,
                                                       current_step,
                                                       0U);

                state->prev_step[track] = current_step;
                state->prev_step_valid[track] = 1U;
            }

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

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        uint8_t div = 1U;
        /* Projection read: boundary stepping consumes track div as a runtime mirror. */
        (void)seq_runtime_get_track_div(track, &div);
        if ((div != 1U) && (div != 2U) && (div != 4U) && (div != 8U))
        {
            div = 1U;
        }

        uint8_t phase = state->track_div_phase[track];
        if (phase >= (uint8_t)(div - 1U))
        {
            state->track_div_phase[track] = 0U;
        }
        else
        {
            state->track_div_phase[track] = (uint8_t)(phase + 1U);
            continue;
        }

        const uint8_t length = seq_boundary_engine_track_length(track);

        uint8_t next = (uint8_t)(state->play_step[track] + 1U);
        if (next >= length)
        {
            next = 0U;
        }

        state->play_step[track] = next;
    }
}
