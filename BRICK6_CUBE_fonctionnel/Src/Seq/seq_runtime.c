#include "Seq/seq_runtime.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Core/engine_tasklet.h"

#include "Seq/seq_model.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_param_iface.h"

#define SEQ_RUNTIME_TICKS_PER_STEP_DEFAULT 188U

typedef struct
{
    uint8_t set_id;
    seq_param8_t param8;
    seq_value16_t value16;
    seq_value16_t base_value16;
} seq_runtime_step_lock_t;

SEQ_STATE_D2 static seq_runtime_state_t g_seq_runtime;

static uint8_t seq_runtime_track_is_valid(seq_track_id_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
}

static uint8_t seq_runtime_lock_equals(const seq_runtime_active_lock_t *active,
                                       uint8_t set_id,
                                       seq_param8_t param8)
{
    return ((active->active != 0U) && (active->set_id == set_id) && (active->param8 == param8)) ? 1U : 0U;
}

static uint8_t seq_runtime_find_next_lock(const seq_runtime_step_lock_t *locks,
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

static uint8_t seq_runtime_collect_step_locks(seq_track_id_t track,
                                              seq_step_id_t step,
                                              seq_runtime_step_lock_t *out_locks,
                                              uint8_t *out_count)
{
    if ((out_locks == 0) || (out_count == 0) || (seq_runtime_track_is_valid(track) == 0U) || (step >= SEQ_MAX_STEPS))
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

static void seq_runtime_restore_all_active_locks(seq_track_id_t track)
{
    seq_runtime_active_lock_t *const active = g_seq_runtime.active_locks[track];
    const uint8_t active_count = g_seq_runtime.active_lock_count[track];

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

    memset(active, 0, sizeof(g_seq_runtime.active_locks[track]));
    g_seq_runtime.active_lock_count[track] = 0U;
}

static void seq_runtime_step_boundary_apply_restore(seq_track_id_t track,
                                                    seq_step_id_t step_prev,
                                                    uint8_t has_prev,
                                                    seq_step_id_t step_curr)
{
    (void)step_prev;

    seq_runtime_step_lock_t next_locks[SEQ_STEP_MAX_LOCKS];
    uint8_t next_count = 0U;
    if (seq_runtime_collect_step_locks(track, step_curr, next_locks, &next_count) == 0U)
    {
        return;
    }

    seq_runtime_active_lock_t *const active = g_seq_runtime.active_locks[track];
    uint8_t active_count = g_seq_runtime.active_lock_count[track];

    if (has_prev != 0U)
    {
        for (uint8_t i = 0U; i < active_count; ++i)
        {
            if (active[i].active == 0U)
            {
                continue;
            }

            if (seq_runtime_find_next_lock(next_locks, next_count, active[i].set_id, active[i].param8, 0) == 0U)
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
                if (seq_runtime_lock_equals(&active[j], next_locks[i].set_id, next_locks[i].param8) != 0U)
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

    memset(active, 0, sizeof(g_seq_runtime.active_locks[track]));
    g_seq_runtime.active_lock_count[track] = 0U;

    for (uint8_t i = 0U; i < next_count; ++i)
    {
        active[i].active = 1U;
        active[i].set_id = next_locks[i].set_id;
        active[i].param8 = next_locks[i].param8;
        active[i].base_value16 = next_locks[i].base_value16;
        g_seq_runtime.active_lock_count[track]++;
    }
}

static void seq_runtime_process_step_boundaries(void)
{
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const seq_step_id_t current_step = g_seq_runtime.play_step[track];

        if ((g_seq_runtime.prev_step_valid[track] == 0U)
            || (g_seq_runtime.prev_step[track] != current_step))
        {
            seq_runtime_step_boundary_apply_restore(track,
                                                    g_seq_runtime.prev_step[track],
                                                    g_seq_runtime.prev_step_valid[track],
                                                    current_step);

            g_seq_runtime.prev_step[track] = current_step;
            g_seq_runtime.prev_step_valid[track] = 1U;
        }
    }
}

static void seq_runtime_advance_one_step(void)
{
    const seq_project_data_t *const project = seq_model_get_project();

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        uint8_t length = project->tracks[track].length_steps;
        if ((length == 0U) || (length > SEQ_MAX_STEPS))
        {
            length = SEQ_MAX_STEPS;
        }

        uint8_t next = (uint8_t)(g_seq_runtime.play_step[track] + 1U);
        if (next >= length)
        {
            next = 0U;
        }

        g_seq_runtime.play_step[track] = next;
    }
}

void seq_runtime_init(void)
{
    seq_model_init_defaults();
    seq_param_iface_init();

    memset(&g_seq_runtime, 0, sizeof(g_seq_runtime));
    g_seq_runtime.clock_src = SEQ_CLOCK_SRC_INTERNAL;
    g_seq_runtime.ticks_per_step = SEQ_RUNTIME_TICKS_PER_STEP_DEFAULT;
    g_seq_runtime.last_tick_count = engine_tick_count;

    seq_edit_init();
}

void seq_runtime_start(void)
{
    if (g_seq_runtime.running != 0U)
    {
        return;
    }

    g_seq_runtime.running = 1U;
    g_seq_runtime.tick_accum = 0U;
    g_seq_runtime.last_tick_count = engine_tick_count;

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        g_seq_runtime.play_step[track] = 0U;
        g_seq_runtime.prev_step_valid[track] = 0U;
        g_seq_runtime.prev_step[track] = 0U;
        seq_runtime_restore_all_active_locks(track);
    }
}

void seq_runtime_stop(void)
{
    if (g_seq_runtime.running == 0U)
    {
        return;
    }

    g_seq_runtime.running = 0U;
    g_seq_runtime.tick_accum = 0U;

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        seq_runtime_restore_all_active_locks(track);
        g_seq_runtime.prev_step_valid[track] = 0U;
    }
}

void seq_runtime_toggle_play_stop(void)
{
    if (g_seq_runtime.running == 0U)
    {
        seq_runtime_start();
    }
    else
    {
        seq_runtime_stop();
    }
}

uint8_t seq_runtime_is_running(void)
{
    return g_seq_runtime.running;
}

void seq_runtime_process(void)
{
    if (g_seq_runtime.running == 0U)
    {
        g_seq_runtime.last_tick_count = engine_tick_count;
        return;
    }

    seq_runtime_process_step_boundaries();

    const uint32_t current_tick = engine_tick_count;
    if (current_tick == g_seq_runtime.last_tick_count)
    {
        return;
    }

    const uint32_t elapsed = current_tick - g_seq_runtime.last_tick_count;
    g_seq_runtime.last_tick_count = current_tick;
    g_seq_runtime.tick_accum += elapsed;

    while (g_seq_runtime.tick_accum >= g_seq_runtime.ticks_per_step)
    {
        g_seq_runtime.tick_accum -= g_seq_runtime.ticks_per_step;
        seq_runtime_advance_one_step();
        seq_runtime_process_step_boundaries();
    }
}

uint8_t seq_runtime_set_playhead_step(seq_track_id_t track, seq_step_id_t step)
{
    if ((seq_runtime_track_is_valid(track) == 0U) || (step >= SEQ_MAX_STEPS))
    {
        return 0U;
    }

    g_seq_runtime.play_step[track] = step;
    return 1U;
}

const seq_runtime_state_t *seq_runtime_get_state(void)
{
    return &g_seq_runtime;
}

uint8_t seq_runtime_get_playhead_step(seq_track_id_t track, seq_step_id_t *out_step)
{
    if ((out_step == 0) || (track >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    *out_step = g_seq_runtime.play_step[track];
    return 1U;
}
