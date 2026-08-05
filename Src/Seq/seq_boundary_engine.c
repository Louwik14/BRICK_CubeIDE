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
#include "Seq/seq_runtime_control.h"

typedef struct
{
    uint8_t set_id;
    seq_param_slot_t target_slot;
    seq_value16_t value16;
    seq_value16_t base_value16;
} seq_boundary_engine_step_lock_t;

static uint8_t seq_boundary_engine_track_length(seq_track_id_t track)
{
    return seq_model_get_track_playback_length(track);
}

static seq_runtime_active_lock_t *seq_boundary_engine_active_locks(seq_runtime_state_t *state,
                                                                   seq_track_id_t track)
{
    if ((state == NULL) || (track_topology_is_active(track) == 0U))
    {
        return NULL;
    }
    return state->active_locks[track];
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

static uint8_t seq_boundary_engine_lock_is_midi_fx_model(
    seq_track_id_t target_track,
    const seq_boundary_engine_step_lock_t *lock)
{
    param_id_t param = PARAM_COUNT;
    return (lock != 0)
        && (lock->set_id == (uint8_t)SEQ_PLOCK_SET_MIDI_FX)
        && (seq_param_iface_slot_to_param(target_track,
                                          lock->set_id,
                                          lock->target_slot,
                                          &param) != 0U)
        && (param == PARAM_MIDI_FX_S1_MODEL
            || param == PARAM_MIDI_FX_S2_MODEL
            || param == PARAM_MIDI_FX_S3_MODEL);
}

static void seq_boundary_engine_prioritize_midi_fx_models(
    seq_track_id_t target_track,
    seq_boundary_engine_step_lock_t *locks,
    uint8_t count)
{
    if (locks == 0)
    {
        return;
    }

    uint8_t write = 0U;
    for (uint8_t read = 0U; read < count; ++read)
    {
        if (seq_boundary_engine_lock_is_midi_fx_model(target_track, &locks[read]) == 0U)
        {
            continue;
        }

        const seq_boundary_engine_step_lock_t model_lock = locks[read];
        for (uint8_t move = read; move > write; --move)
        {
            locks[move] = locks[move - 1U];
        }
        locks[write] = model_lock;
        ++write;
    }
}

static uint8_t seq_boundary_engine_collect_non_play_locks(seq_track_id_t track,
                                                          seq_step_id_t step,
                                                          seq_boundary_engine_step_lock_t *out_locks,
                                                          uint8_t *out_count)
{
    if ((out_locks == 0)
        || (out_count == 0)
        || (track >= SEQ_TRACK_COUNT)
        || (seq_model_is_step_editable_index(step) == 0U))
    {
        return 0U;
    }

    *out_count = 0U;
    track_runtime_refresh_track(track);

    if (seq_model_step_is_active(track, step) == 0U)
    {
        return 1U;
    }

    seq_plock_entry_t entries[SEQ_STEP_MAX_LOCKS];
    uint8_t entry_count = 0U;
    if (seq_model_step_plock_collect(track, step, entries, SEQ_STEP_MAX_LOCKS, &entry_count) == 0U)
    {
        return 0U;
    }

    uint8_t count = 0U;
    for (uint8_t i = 0U; i < entry_count; ++i)
    {
        const seq_plock_entry_t *const entry = &entries[i];
        if (entry->set_id == (uint8_t)SEQ_PLOCK_SET_PLAY)
        {
            continue;
        }

        if (seq_param_iface_slot_is_supported(track, entry->set_id, entry->param_slot) == 0U)
        {
            continue;
        }

        if (count >= seq_model_get_step_lock_limit(track))
        {
            break;
        }

        out_locks[count].set_id = entry->set_id;
        out_locks[count].target_slot = entry->param_slot;
        out_locks[count].value16 = entry->value16;
        out_locks[count].base_value16 = 0U;
        count++;
    }

    seq_boundary_engine_prioritize_md_model(track, out_locks, count);
    seq_boundary_engine_prioritize_midi_fx_models(track, out_locks, count);
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

    seq_runtime_active_lock_t *const active = seq_boundary_engine_active_locks(state, track);
    if (active == NULL)
    {
        return;
    }
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

    memset(active,
           0,
           (size_t)seq_model_get_step_lock_limit(track) * sizeof(seq_runtime_active_lock_t));
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
                                                   seq_step_id_t step)
{
    seq_boundary_engine_step_lock_t next_locks[SEQ_STEP_MAX_LOCKS];
    uint8_t next_count = 0U;
    if ((state == 0)
        || (seq_boundary_engine_collect_non_play_locks(track,
                                                       step,
                                                       next_locks,
                                                       &next_count) == 0U))
    {
        return;
    }

    seq_runtime_active_lock_t *const active = seq_boundary_engine_active_locks(state, track);
    if (active == NULL)
    {
        return;
    }
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

    memset(active,
           0,
           (size_t)seq_model_get_step_lock_limit(track) * sizeof(seq_runtime_active_lock_t));
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
