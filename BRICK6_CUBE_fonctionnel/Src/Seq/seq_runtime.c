#include "Seq/seq_runtime.h"

#include <string.h>
#include <stdio.h>

#include "Storage/memory_layout.h"
#include "Core/engine_tasklet.h"
#include "Core/track_runtime.h"
#include "Audio/microdexed_synth.h"
#include "Audio/monob_synth.h"
#include "midi.h"
#include "param_registry.h"
#include "ui_core.h"

#include "Seq/seq_model.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_param_iface.h"

#define SEQ_RUNTIME_TICKS_PER_STEP_DEFAULT 188U
#define SEQ_RUNTIME_MIDI_CLOCKS_PER_STEP 6U
#define SEQ_RUNTIME_PLAY_VOICE_COUNT 4U
#define SEQ_RUNTIME_PLAY_EVENT_CAP 64U

#ifndef SEQ_DEBUG_TRACK_BINDING
#define SEQ_DEBUG_TRACK_BINDING 0
#endif

#if SEQ_DEBUG_TRACK_BINDING
#define SEQ_BIND_LOG(...) printf(__VA_ARGS__)
#else
#define SEQ_BIND_LOG(...) do { } while (0)
#endif

typedef enum
{
    SEQ_PLAY_EVT_NOTE_ON = 0,
    SEQ_PLAY_EVT_NOTE_OFF
} seq_play_evt_type_t;

typedef struct
{
    uint32_t due_tick;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint8_t type;
} seq_play_evt_t;

typedef struct
{
    uint8_t set_id;
    seq_param8_t param8;
    seq_value16_t value16;
    seq_value16_t base_value16;
} seq_runtime_step_lock_t;

SEQ_STATE_D2 static seq_runtime_state_t g_seq_runtime;
SEQ_STATE_D2 static seq_play_evt_t g_seq_play_events[SEQ_RUNTIME_PLAY_EVENT_CAP];
SEQ_STATE_D2 static uint8_t g_seq_play_event_count;
SEQ_STATE_D2 static uint32_t g_seq_midi_clock_tick_accum;
SEQ_STATE_D2 static uint8_t g_seq_active_note_counts[SEQ_TRACK_COUNT][128];

static void seq_runtime_active_notes_clear(void)
{
    memset(g_seq_active_note_counts, 0, sizeof(g_seq_active_note_counts));
}

static void seq_runtime_mark_note_on(seq_track_id_t track, uint8_t note)
{
    if ((track >= SEQ_TRACK_COUNT) || (note >= 128U))
    {
        return;
    }

    if (g_seq_active_note_counts[track][note] < 0xFFU)
    {
        g_seq_active_note_counts[track][note]++;
    }
}

static void seq_runtime_mark_note_off(seq_track_id_t track, uint8_t note)
{
    if ((track >= SEQ_TRACK_COUNT) || (note >= 128U))
    {
        return;
    }

    if (g_seq_active_note_counts[track][note] > 0U)
    {
        g_seq_active_note_counts[track][note]--;
    }
}

static void seq_runtime_kill_all_active_notes(void)
{
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const uint8_t channel_1_16 = ui_get_track_midi_channel(track);
        const uint8_t channel = (uint8_t)((channel_1_16 > 0U) ? (channel_1_16 - 1U) : 0U);

        for (uint8_t note = 0U; note < 128U; ++note)
        {
            const uint8_t count = g_seq_active_note_counts[track][note];
            if (count == 0U)
            {
                continue;
            }

            for (uint8_t i = 0U; i < count; ++i)
            {
                midi_note_off(MIDI_DEST_BOTH, channel, note, 0U);
            }

            g_seq_active_note_counts[track][note] = 0U;
        }
    }
}

static void seq_runtime_send_transport_start(void)
{
    if (g_seq_runtime.clock_src == SEQ_CLOCK_SRC_EXTERNAL_MIDI)
    {
        return;
    }

    midi_start(MIDI_DEST_BOTH);
}

static void seq_runtime_send_transport_stop_and_panic(void)
{
    if (g_seq_runtime.clock_src != SEQ_CLOCK_SRC_EXTERNAL_MIDI)
    {
        midi_stop(MIDI_DEST_BOTH);
    }

    seq_runtime_kill_all_active_notes();
    for (uint8_t ch = 0U; ch < 16U; ++ch)
    {
        midi_all_notes_off(MIDI_DEST_BOTH, ch);
    }
}

static void seq_runtime_send_internal_clock(uint32_t elapsed_ticks)
{
    if ((g_seq_runtime.clock_src == SEQ_CLOCK_SRC_EXTERNAL_MIDI) || (g_seq_runtime.running == 0U))
    {
        return;
    }

    uint32_t clock_period_ticks = g_seq_runtime.ticks_per_step / SEQ_RUNTIME_MIDI_CLOCKS_PER_STEP;
    if (clock_period_ticks == 0U)
    {
        clock_period_ticks = 1U;
    }

    g_seq_midi_clock_tick_accum += elapsed_ticks;
    while (g_seq_midi_clock_tick_accum >= clock_period_ticks)
    {
        g_seq_midi_clock_tick_accum -= clock_period_ticks;
        midi_clock(MIDI_DEST_BOTH);
    }
}

static uint8_t seq_runtime_track_is_valid(seq_track_id_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
}

static void seq_runtime_play_events_clear(void)
{
    g_seq_play_event_count = 0U;
}

static void seq_runtime_play_event_push(uint32_t due_tick,
                                        uint8_t type,
                                        seq_track_id_t track,
                                        uint8_t note,
                                        uint8_t velocity)
{
    if (g_seq_play_event_count >= SEQ_RUNTIME_PLAY_EVENT_CAP)
    {
        return;
    }

    seq_play_evt_t *const evt = &g_seq_play_events[g_seq_play_event_count++];
    evt->due_tick = due_tick;
    evt->type = type;
    evt->track = track;
    evt->note = note;
    evt->velocity = velocity;
}

static void seq_runtime_play_events_service(void)
{
    const uint32_t now = engine_tick_count;
    uint8_t i = 0U;
    while (i < g_seq_play_event_count)
    {
        seq_play_evt_t *const evt = &g_seq_play_events[i];
        if ((int32_t)(now - evt->due_tick) < 0)
        {
            i++;
            continue;
        }

        const uint8_t channel_1_16 = ui_get_track_midi_channel(evt->track);
        const uint8_t channel = (uint8_t)((channel_1_16 > 0U) ? (channel_1_16 - 1U) : 0U);
        if (evt->type == (uint8_t)SEQ_PLAY_EVT_NOTE_ON)
        {
            midi_note_on(MIDI_DEST_BOTH, channel, evt->note, evt->velocity);
            seq_runtime_mark_note_on(evt->track, evt->note);
        }
        else
        {
            midi_note_off(MIDI_DEST_BOTH, channel, evt->note, 0U);
            seq_runtime_mark_note_off(evt->track, evt->note);
        }

        track_runtime_refresh_track(evt->track);
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(evt->track);
        if ((ctx != NULL) && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND))
        {
            if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_MONOB)
            {
                if (evt->type == (uint8_t)SEQ_PLAY_EVT_NOTE_ON)
                {
                    monob_synth_note_on_for_instance(ctx->instance_id, evt->note, evt->velocity);
                }
                else
                {
                    monob_synth_note_off_for_instance(ctx->instance_id, evt->note);
                }
            }
            else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7)
            {
                if (evt->type == (uint8_t)SEQ_PLAY_EVT_NOTE_ON)
                {
                    microdexed_synth_note_on(evt->note, evt->velocity);
                }
                else
                {
                    microdexed_synth_note_off(evt->note);
                }
            }
        }

        track_runtime_refresh_track(evt->track);
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(evt->track);
        if ((ctx != NULL) && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND))
        {
            if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_MONOB)
            {
                if (evt->type == (uint8_t)SEQ_PLAY_EVT_NOTE_ON)
                {
                    monob_synth_note_on_for_instance(ctx->instance_id, evt->note, evt->velocity);
                }
                else
                {
                    monob_synth_note_off_for_instance(ctx->instance_id, evt->note);
                }
            }
            else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7)
            {
                if (evt->type == (uint8_t)SEQ_PLAY_EVT_NOTE_ON)
                {
                    microdexed_synth_note_on(evt->note, evt->velocity);
                }
                else
                {
                    microdexed_synth_note_off(evt->note);
                }
            }
        }

        for (uint8_t j = i + 1U; j < g_seq_play_event_count; ++j)
        {
            g_seq_play_events[j - 1U] = g_seq_play_events[j];
        }
        g_seq_play_event_count--;
    }
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

static param_id_t seq_runtime_play_param_note(uint8_t voice)
{
    static const param_id_t k_note[SEQ_RUNTIME_PLAY_VOICE_COUNT] = {
        PARAM_SEQ_PLAY_V1_NOTE, PARAM_SEQ_PLAY_V2_NOTE, PARAM_SEQ_PLAY_V3_NOTE, PARAM_SEQ_PLAY_V4_NOTE
    };
    return (voice < SEQ_RUNTIME_PLAY_VOICE_COUNT) ? k_note[voice] : PARAM_SEQ_PLAY_V1_NOTE;
}

static param_id_t seq_runtime_play_param_vel(uint8_t voice)
{
    static const param_id_t k_vel[SEQ_RUNTIME_PLAY_VOICE_COUNT] = {
        PARAM_SEQ_PLAY_V1_VEL, PARAM_SEQ_PLAY_V2_VEL, PARAM_SEQ_PLAY_V3_VEL, PARAM_SEQ_PLAY_V4_VEL
    };
    return (voice < SEQ_RUNTIME_PLAY_VOICE_COUNT) ? k_vel[voice] : PARAM_SEQ_PLAY_V1_VEL;
}

static param_id_t seq_runtime_play_param_len(uint8_t voice)
{
    static const param_id_t k_len[SEQ_RUNTIME_PLAY_VOICE_COUNT] = {
        PARAM_SEQ_PLAY_V1_LEN, PARAM_SEQ_PLAY_V2_LEN, PARAM_SEQ_PLAY_V3_LEN, PARAM_SEQ_PLAY_V4_LEN
    };
    return (voice < SEQ_RUNTIME_PLAY_VOICE_COUNT) ? k_len[voice] : PARAM_SEQ_PLAY_V1_LEN;
}

static param_id_t seq_runtime_play_param_mictim(uint8_t voice)
{
    static const param_id_t k_mictim[SEQ_RUNTIME_PLAY_VOICE_COUNT] = {
        PARAM_SEQ_PLAY_V1_MICTIM, PARAM_SEQ_PLAY_V2_MICTIM, PARAM_SEQ_PLAY_V3_MICTIM, PARAM_SEQ_PLAY_V4_MICTIM
    };
    return (voice < SEQ_RUNTIME_PLAY_VOICE_COUNT) ? k_mictim[voice] : PARAM_SEQ_PLAY_V1_MICTIM;
}

static seq_value16_t seq_runtime_play_get_locked_or_default(seq_track_id_t track,
                                                            seq_step_id_t step,
                                                            param_id_t param_id)
{
    uint8_t set_id = 0U;
    seq_param8_t param8 = 0U;
    if (seq_param_iface_map_param(param_id, &set_id, &param8) == 0U)
    {
        return seq_param_iface_encode_param_value(param_id, param_registry[param_id].default_value);
    }

    seq_plock_entry_t entry;
    if (seq_model_step_plock_find(track, step, set_id, param8, &entry) != 0U)
    {
        return entry.value16;
    }

    return seq_param_iface_encode_param_value(param_id, param_registry[param_id].default_value);
}

static void seq_runtime_schedule_play_step(seq_track_id_t track, seq_step_id_t step)
{
    if (seq_model_get_trig(track, step) == 0U)
    {
        return;
    }

    const track_runtime_param_status_t play_status =
            track_runtime_get_effective_param_status(track, PARAM_SEQ_PLAY_V1_NOTE);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == 0) || (play_status == TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL))
    {
        SEQ_BIND_LOG("[SEQ][RT] skip PLAY tr=%u bind_state=%u reason=%u engine=%u inst=%u ui_active=%u\r\n",
                     (unsigned)track,
                     (unsigned)((ctx != 0) ? ctx->bind_state : 0xFFU),
                     (unsigned)((ctx != 0) ? ctx->bind_reason : 0xFFU),
                     (unsigned)((ctx != 0) ? ctx->engine : 0xFFU),
                     (unsigned)((ctx != 0) ? ctx->instance_id : 0xFFU),
                     (unsigned)ui_get_active_track());
        return;
    }

    const uint32_t step_tick = engine_tick_count;

    for (uint8_t voice = 0U; voice < SEQ_RUNTIME_PLAY_VOICE_COUNT; ++voice)
    {
        const param_id_t note_id = seq_runtime_play_param_note(voice);
        const param_id_t vel_id = seq_runtime_play_param_vel(voice);
        const param_id_t len_id = seq_runtime_play_param_len(voice);
        const param_id_t mictim_id = seq_runtime_play_param_mictim(voice);

        const float vel_f = seq_param_iface_decode_param_value(vel_id, seq_runtime_play_get_locked_or_default(track, step, vel_id));
        const uint8_t vel = (uint8_t)(vel_f + 0.5f);
        if (vel == 0U)
        {
            continue;
        }

        const float note_f = seq_param_iface_decode_param_value(note_id, seq_runtime_play_get_locked_or_default(track, step, note_id));
        const uint8_t note = (uint8_t)(note_f + 0.5f);
        if (note >= 128U)
        {
            continue;
        }

        const float len_f = seq_param_iface_decode_param_value(len_id, seq_runtime_play_get_locked_or_default(track, step, len_id));
        const uint32_t len_ticks = (uint32_t)(((len_f < 1.0f ? 1.0f : len_f) * (float)g_seq_runtime.ticks_per_step) / 100.0f);

        const float mictim_f = seq_param_iface_decode_param_value(mictim_id, seq_runtime_play_get_locked_or_default(track, step, mictim_id));
        int32_t on_tick = (int32_t)step_tick + (int32_t)((mictim_f * (float)g_seq_runtime.ticks_per_step) / 96.0f);
        if (on_tick < (int32_t)step_tick)
        {
            on_tick = (int32_t)step_tick;
        }

        seq_runtime_play_event_push((uint32_t)on_tick, (uint8_t)SEQ_PLAY_EVT_NOTE_ON, track, note, vel);
        seq_runtime_play_event_push((uint32_t)on_tick + ((len_ticks == 0U) ? 1U : len_ticks), (uint8_t)SEQ_PLAY_EVT_NOTE_OFF, track, note, 0U);
    }
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

    SEQ_BIND_LOG("[SEQ][RT] tr=%u step=%u locks=%u ui_active=%u\r\n",
                 (unsigned)track,
                 (unsigned)step_curr,
                 (unsigned)next_count,
                 (unsigned)ui_get_active_track());

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
        SEQ_BIND_LOG("[SEQ][RT] apply tr=%u set=%u p=%u v16=%u\r\n",
                     (unsigned)track,
                     (unsigned)next_locks[i].set_id,
                     (unsigned)next_locks[i].param8,
                     (unsigned)next_locks[i].value16);
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
            seq_runtime_schedule_play_step(track, current_step);

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
    /* TODO(clock-source): wire this to a global runtime/menu setting (INT/EXT).
     * For now, keep forced to internal clock to preserve current UX. */
    g_seq_runtime.clock_src = SEQ_CLOCK_SRC_INTERNAL;
    g_seq_runtime.ticks_per_step = SEQ_RUNTIME_TICKS_PER_STEP_DEFAULT;
    g_seq_runtime.last_tick_count = engine_tick_count;
    seq_runtime_play_events_clear();
    seq_runtime_active_notes_clear();

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        g_seq_runtime.track_div[track] = 1U;
        g_seq_runtime.track_quant[track] = 1U;
        g_seq_runtime.track_swing[track] = 0U;
    }

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
    g_seq_runtime.ext_clock_tick_accum = 0U;
    seq_runtime_play_events_clear();
    seq_runtime_active_notes_clear();

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        g_seq_runtime.play_step[track] = 0U;
        g_seq_runtime.prev_step_valid[track] = 0U;
        g_seq_runtime.prev_step[track] = 0U;
        seq_runtime_restore_all_active_locks(track);
    }

    g_seq_midi_clock_tick_accum = 0U;
    seq_runtime_send_transport_start();
}

void seq_runtime_stop(void)
{
    if (g_seq_runtime.running == 0U)
    {
        return;
    }

    g_seq_runtime.running = 0U;
    g_seq_runtime.tick_accum = 0U;
    seq_runtime_play_events_clear();

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        seq_runtime_restore_all_active_locks(track);
        g_seq_runtime.prev_step_valid[track] = 0U;
    }

    g_seq_midi_clock_tick_accum = 0U;
    seq_runtime_send_transport_stop_and_panic();
    seq_runtime_active_notes_clear();
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
        seq_runtime_play_events_service();
        return;
    }

    if (g_seq_runtime.clock_src == SEQ_CLOCK_SRC_EXTERNAL_MIDI)
    {
        seq_runtime_process_step_boundaries();
        seq_runtime_play_events_service();
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
    seq_runtime_send_internal_clock(elapsed);

    while (g_seq_runtime.tick_accum >= g_seq_runtime.ticks_per_step)
    {
        g_seq_runtime.tick_accum -= g_seq_runtime.ticks_per_step;
        seq_runtime_advance_one_step();
        seq_runtime_process_step_boundaries();
    }

    seq_runtime_play_events_service();
}

void seq_runtime_set_clock_source(seq_clock_src_t src)
{
    if ((uint8_t)src >= (uint8_t)SEQ_CLOCK_SRC_COUNT)
    {
        return;
    }

    g_seq_runtime.clock_src = src;
    g_seq_runtime.ext_clock_tick_accum = 0U;
    g_seq_runtime.tick_accum = 0U;
    g_seq_midi_clock_tick_accum = 0U;
    seq_runtime_play_events_clear();
}

seq_clock_src_t seq_runtime_get_clock_source(void)
{
    return g_seq_runtime.clock_src;
}

void seq_runtime_midi_clock(void)
{
    if ((g_seq_runtime.clock_src != SEQ_CLOCK_SRC_EXTERNAL_MIDI) || (g_seq_runtime.running == 0U))
    {
        return;
    }

    g_seq_runtime.ext_clock_tick_accum++;
    if (g_seq_runtime.ext_clock_tick_accum < SEQ_RUNTIME_MIDI_CLOCKS_PER_STEP)
    {
        return;
    }

    g_seq_runtime.ext_clock_tick_accum = 0U;
    seq_runtime_advance_one_step();
    seq_runtime_process_step_boundaries();
    seq_runtime_play_events_service();
}

void seq_runtime_midi_start(void)
{
    if (g_seq_runtime.clock_src != SEQ_CLOCK_SRC_EXTERNAL_MIDI)
    {
        return;
    }

    seq_runtime_start();
}

void seq_runtime_midi_continue(void)
{
    if (g_seq_runtime.clock_src != SEQ_CLOCK_SRC_EXTERNAL_MIDI)
    {
        return;
    }

    if (g_seq_runtime.running != 0U)
    {
        return;
    }

    g_seq_runtime.running = 1U;
    g_seq_runtime.tick_accum = 0U;
    g_seq_runtime.ext_clock_tick_accum = 0U;
    g_seq_runtime.last_tick_count = engine_tick_count;
}

void seq_runtime_midi_stop(void)
{
    if (g_seq_runtime.clock_src != SEQ_CLOCK_SRC_EXTERNAL_MIDI)
    {
        return;
    }

    seq_runtime_stop();
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

void seq_runtime_set_track_div(seq_track_id_t track, uint8_t div)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (div == 0U)
    {
        div = 1U;
    }
    g_seq_runtime.track_div[track] = div;
}

void seq_runtime_set_track_quant(seq_track_id_t track, uint8_t quant)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_seq_runtime.track_quant[track] = quant;
}

void seq_runtime_set_track_swing(seq_track_id_t track, uint8_t swing)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (swing > 100U)
    {
        swing = 100U;
    }

    g_seq_runtime.track_swing[track] = swing;
}
