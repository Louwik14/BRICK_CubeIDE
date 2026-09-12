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

#include "Track/track_runtime.h"
#include "Track/entity_topology.h"
#include "NoteFx/note_fx_state.h"
#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_runtime_control.h"

typedef struct
{
    uint8_t set_id;
    seq_param_slot_t target_slot;
    param_id_t parameter_id;
    seq_value16_t value16;
} seq_boundary_engine_step_lock_t;

static uint8_t seq_boundary_engine_track_is_valid(seq_track_id_t track)
{
    return entity_topology_is_active((brick_entity_id_t)track);
}

static uint8_t seq_boundary_engine_track_length(seq_track_id_t track)
{
    return seq_model_get_track_playback_length(track);
}

static seq_runtime_active_lock_t *seq_boundary_engine_active_locks(seq_runtime_state_t *state,
                                                                   seq_track_id_t track)
{
    if ((state == NULL) || (seq_boundary_engine_track_is_valid(track) == 0U))
    {
        return NULL;
    }
    return state->active_locks[track];
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
        && (param >= PARAM_MIDI_FX_S1_MODEL)
        && (param <= PARAM_MIDI_FX_S4_MODEL)
        && ((((uint16_t)param - PARAM_MIDI_FX_S1_PARAM1)
            % NOTE_FX_PARAM_COUNT) == (NOTE_FX_PARAM_COUNT - 1U));
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
        || (seq_boundary_engine_track_is_valid(track) == 0U)
        || (seq_model_is_step_editable_index(step) == 0U))
    {
        return 0U;
    }

    *out_count = 0U;

    if (seq_model_step_is_active(track, step) == 0U)
    {
        return 1U;
    }

    const uint8_t entry_count = seq_model_step_param_plock_count(track, step);

    uint8_t count = 0U;
    for (uint8_t i = 0U; i < entry_count; ++i)
    {
        seq_plock_entry_t entry_storage;
        if (seq_model_step_param_plock_get_at(track, step, i, &entry_storage) == 0U)
        {
            return 0U;
        }
        const seq_plock_entry_t *const entry = &entry_storage;

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
        if (seq_param_iface_slot_to_param(track, entry->set_id,
                                          entry->param_slot,
                                          &out_locks[count].parameter_id) == 0U)
            continue;
        out_locks[count].value16 = entry->value16;
        count++;
    }

    seq_boundary_engine_prioritize_midi_fx_models(track, out_locks, count);
    *out_count = count;
    return 1U;
}

void seq_boundary_engine_restore_all_active_locks(seq_runtime_state_t *state,
                                                  seq_track_id_t track,
                                                  uint64_t effective_sample)
{
    if ((state == 0) || (seq_boundary_engine_track_is_valid(track) == 0U))
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

        param_id_t current = PARAM_COUNT;
        if ((seq_param_iface_slot_to_param(track, active[i].set_id,
                                           active[i].param_slot,
                                           &current) == 0U)
                || (current != active[i].parameter_id))
        {
            seq_param_iface_discard_runtime_lock(track, active[i].set_id,
                                                  active[i].param_slot);
            continue;
        }
        if (seq_param_iface_restore_base(track, active[i].set_id,
                                         active[i].param_slot,
                                         effective_sample) == 0U) return;
    }

    memset(active,
           0,
           (size_t)seq_model_get_step_lock_limit(track) * sizeof(seq_runtime_active_lock_t));
    state->active_lock_count[track] = 0U;
}

void seq_boundary_engine_invalidate_track(seq_runtime_state_t *state,
                                          seq_track_id_t track)
{
    if ((state == 0) || (seq_boundary_engine_track_is_valid(track) == 0U))
    {
        return;
    }

    state->prev_step_valid[track] = 0U;
}

static void seq_boundary_engine_step_apply_restore(seq_runtime_state_t *state,
                                                   seq_track_id_t track,
                                                   uint8_t has_prev,
                                                   seq_step_id_t step,
                                                   uint64_t effective_sample)
{
    seq_boundary_engine_step_lock_t next_locks[SEQ_STEP_MAX_LOCKS];
    uint8_t next_count = 0U;
    seq_runtime_active_lock_t *const active =
        seq_boundary_engine_active_locks(state, track);
    if ((state == 0)
        || (active == NULL)
        || (seq_boundary_engine_collect_non_play_locks(track,
                                                       step,
                                                       next_locks,
                                                       &next_count) == 0U))
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

            uint8_t next_index = 0U;
            const uint8_t continues = seq_boundary_engine_find_next_lock(
                next_locks, next_count, active[i].set_id,
                active[i].param_slot, &next_index);
            if ((continues == 0U)
                    || (next_locks[next_index].parameter_id
                        != active[i].parameter_id))
            {
                param_id_t current = PARAM_COUNT;
                if ((seq_param_iface_slot_to_param(track, active[i].set_id,
                                                   active[i].param_slot,
                                                   &current) == 0U)
                        || (current != active[i].parameter_id))
                    seq_param_iface_discard_runtime_lock(
                        track, active[i].set_id, active[i].param_slot);
                else if (seq_param_iface_restore_base(
                        track, active[i].set_id, active[i].param_slot,
                        effective_sample) == 0U)
                    return;
            }
        }
    }

    for (uint8_t i = 0U; i < next_count; ++i)
    {
        if (seq_param_iface_apply_lock(track,
                                      next_locks[i].set_id,
                                      next_locks[i].target_slot,
                                      next_locks[i].value16,
                                      effective_sample) == 0U)
            return;
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
        active[i].parameter_id = next_locks[i].parameter_id;
        state->active_lock_count[track]++;
    }
}

void seq_boundary_engine_process(seq_runtime_state_t *state,
                                 seq_boundary_hit_t *out_hits,
                                 uint8_t max_hits,
                                 uint8_t *out_hit_count,
                                 uint64_t effective_sample)
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
    for (seq_track_id_t track = 0U; track < (seq_track_id_t)SEQ_LANE_CAPACITY; ++track)
    {
        if (seq_boundary_engine_track_is_valid(track) == 0U)
        {
            continue;
        }
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
                                                   current_step,
                                                   effective_sample);
            state->prev_step[track] = current_step;
            state->prev_step_valid[track] = 1U;

            if (hit_count < max_hits)
            {
                out_hits[hit_count].track = track;
                out_hits[hit_count].step = current_step;
                out_hits[hit_count].swing_phase = state->track_swing_phase[track];
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

    for (seq_track_id_t track = 0U; track < (seq_track_id_t)SEQ_LANE_CAPACITY; ++track)
    {
        if (seq_boundary_engine_track_is_valid(track) == 0U)
        {
            continue;
        }
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
            state->track_swing_phase[track] ^= 1U;
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
