/*
 * Module: seq_runtime_exec
 * Role: Etat d'execution et timeline musicale du sequenceur.
 * Responsibilities: stocker l'etat runtime de progression et servir de
 * support partage a l'orchestrateur seq_runtime.
 * Integration: proprietaire unique de l'etat runtime partage; seq_runtime.c
 * continue d'orchestrer, mais ne porte plus la structure elle-meme.
 */
#define SEQ_BOUNDARY_ENGINE_IMPLEMENTATION 1
#define SEQ_PLAY_SCHEDULER_IMPLEMENTATION 1

#include "Seq/seq_runtime_exec.h"

#include "Storage/memory_layout.h"
#include "Seq/seq_boundary_engine.h"
#include "Seq/seq_clock_bridge.h"
#include "Seq/seq_live_rec_session.h"
#include "Seq/seq_model.h"
#include "Core/entity_topology.h"
#include "Seq/seq_output_guard.h"
#include "Seq/seq_play_scheduler.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_transport_fsm.h"
#include "midi.h"

#define SEQ_RUNTIME_EXEC_BOUNDARY_EVENT_CAP 32U
#define SEQ_RUNTIME_EXEC_METRO_STEPS_PER_BEAT 4U
#define SEQ_RUNTIME_EXEC_METRO_STEPS_PER_BAR  16U

typedef struct
{
    uint64_t due_sample_time;
    uint8_t track;
    uint8_t type;
    uint8_t click_type;
} seq_runtime_exec_boundary_event_t;

CONTROL_M4_SRAM2 static seq_runtime_state_t g_seq_runtime_exec_state;
static uint64_t g_seq_runtime_exec_sample_timeline;
static uint8_t g_seq_runtime_exec_midi_clock_enabled;
static uint32_t g_seq_runtime_exec_midi_clock_period_q16;
static uint64_t g_seq_runtime_exec_midi_clock_next_sample_q16;
static volatile uint32_t g_seq_runtime_exec_external_step_pulses_pending;
static seq_runtime_exec_boundary_event_t g_seq_runtime_exec_boundary_events[SEQ_RUNTIME_EXEC_BOUNDARY_EVENT_CAP];
static uint8_t g_seq_runtime_exec_boundary_event_count;
static uint32_t g_seq_runtime_exec_metronome_step;
static void seq_runtime_exec_copy_scheduler_event(seq_runtime_control_event_t *out_event,
                                                        const seq_play_scheduler_event_t *scheduler_event);
static void seq_runtime_exec_push_boundary_edge(seq_track_id_t track, uint64_t due_sample_time);
static void seq_runtime_exec_push_metronome_click(uint64_t due_sample_time, uint8_t accent);
static void seq_runtime_exec_push_metronome_for_step(uint64_t due_sample_time);
static uint16_t seq_runtime_exec_collect_boundary_commands(seq_runtime_control_event_t *out_events,
                                                         uint16_t max_events,
                                                         uint16_t block_frames,
                                                         uint64_t block_start_sample);
static void seq_runtime_exec_sort_control_events(seq_runtime_control_event_t *events, uint16_t count);
static uint64_t seq_runtime_exec_track_step_span_q16(seq_track_id_t track,
                                                     uint32_t samples_per_step_q16);
static seq_step_id_t seq_runtime_exec_next_play_step(seq_track_id_t track, seq_step_id_t step);
static void seq_runtime_exec_schedule_hit_play_and_lookahead(const seq_runtime_state_t *state,
                                                            const seq_boundary_hit_t *hit,
                                                            uint32_t now_tick);


seq_runtime_state_t *seq_runtime_exec_state(void)
{
    /* Single owner of the shared execution state. */
    return &g_seq_runtime_exec_state;
}

const seq_runtime_state_t *seq_runtime_exec_state_const(void)
{
    /* Read-only projection of the shared execution state owner. */
    return &g_seq_runtime_exec_state;
}

void seq_runtime_exec_init(void)
{
    g_seq_runtime_exec_state = (seq_runtime_state_t){0};
    g_seq_runtime_exec_sample_timeline = 0U;
    g_seq_runtime_exec_midi_clock_enabled = 0U;
    g_seq_runtime_exec_midi_clock_period_q16 = 1U;
    g_seq_runtime_exec_midi_clock_next_sample_q16 = 0U;
    g_seq_runtime_exec_external_step_pulses_pending = 0U;
    g_seq_runtime_exec_boundary_event_count = 0U;
    g_seq_runtime_exec_metronome_step = 0U;
}

static void seq_runtime_exec_copy_scheduler_event(seq_runtime_control_event_t *out_event,
                                                        const seq_play_scheduler_event_t *scheduler_event)
{
    if ((out_event == NULL) || (scheduler_event == NULL))
    {
        return;
    }

    out_event->type = scheduler_event->type;
    out_event->track = scheduler_event->track;
    out_event->note = scheduler_event->note;
    out_event->velocity = scheduler_event->velocity;
    out_event->track_generation = scheduler_event->track_generation;
    out_event->reserved = 0U;
    out_event->sample_offset_in_block = scheduler_event->sample_offset_in_block;
    out_event->sample_abs = scheduler_event->sample_abs;
    out_event->generation = scheduler_event->generation;
    out_event->event_token = scheduler_event->event_token;
}

static void seq_runtime_exec_push_boundary_edge(seq_track_id_t track, uint64_t due_sample_time)
{
    if (g_seq_runtime_exec_boundary_event_count >= SEQ_RUNTIME_EXEC_BOUNDARY_EVENT_CAP)
    {
        return;
    }

    seq_runtime_exec_boundary_event_t *const event =
            &g_seq_runtime_exec_boundary_events[g_seq_runtime_exec_boundary_event_count++];
    event->due_sample_time = due_sample_time;
    event->track = track;
    event->type = SEQ_RUNTIME_AUDIO_EVENT_BOUNDARY_EDGE;
    event->click_type = 0U;
}

static void seq_runtime_exec_push_metronome_click(uint64_t due_sample_time, uint8_t accent)
{
    if (g_seq_runtime_exec_boundary_event_count >= SEQ_RUNTIME_EXEC_BOUNDARY_EVENT_CAP)
    {
        return;
    }

    seq_runtime_exec_boundary_event_t *const event =
            &g_seq_runtime_exec_boundary_events[g_seq_runtime_exec_boundary_event_count++];
    event->due_sample_time = due_sample_time;
    event->track = 0U;
    event->type = SEQ_RUNTIME_AUDIO_EVENT_METRO_CLICK;
    event->click_type = (accent != 0U) ? 1U : 0U;
}

static void seq_runtime_exec_push_metronome_for_step(uint64_t due_sample_time)
{
    if ((g_seq_runtime_exec_metronome_step % SEQ_RUNTIME_EXEC_METRO_STEPS_PER_BEAT) != 0U)
    {
        return;
    }

    const uint8_t accent =
            ((g_seq_runtime_exec_metronome_step % SEQ_RUNTIME_EXEC_METRO_STEPS_PER_BAR) == 0U) ? 1U : 0U;
    seq_runtime_exec_push_metronome_click(due_sample_time, accent);
}

static uint16_t seq_runtime_exec_collect_boundary_commands(seq_runtime_control_event_t *out_events,
                                                         uint16_t max_events,
                                                         uint16_t block_frames,
                                                         uint64_t block_start_sample)
{
    if ((out_events == 0) || (max_events == 0U))
    {
        return 0U;
    }

    if (block_frames == 0U)
    {
        block_frames = 1U;
    }

    const uint64_t block_end_sample = block_start_sample + (uint64_t)block_frames;
    uint16_t count = 0U;
    for (uint8_t i = 0U; (i < g_seq_runtime_exec_boundary_event_count) && (count < max_events); ++i)
    {
        const seq_runtime_exec_boundary_event_t *const marker = &g_seq_runtime_exec_boundary_events[i];
        if ((marker->due_sample_time < block_start_sample) || (marker->due_sample_time >= block_end_sample))
        {
            continue;
        }

        seq_runtime_control_event_t *const out = &out_events[count++];
        out->type = marker->type;
        out->track = marker->track;
        out->note = 0U;
        out->velocity = marker->click_type;
        out->track_generation = 0U;
        out->reserved = 0U;
        out->sample_abs = marker->due_sample_time;
        out->generation = 0U;
        out->sample_offset_in_block = (uint16_t)(marker->due_sample_time - block_start_sample);
        out->event_token = 0U;
    }

    return count;
}

static uint64_t seq_runtime_exec_track_step_span_q16(seq_track_id_t track,
                                                     uint32_t samples_per_step_q16)
{
    uint8_t div = 1U;
    (void)seq_runtime_get_track_div(track, &div);
    if ((div != 1U) && (div != 2U) && (div != 4U) && (div != 8U))
    {
        div = 1U;
    }

    const uint32_t sps_q16 = (samples_per_step_q16 == 0U) ? 1U : samples_per_step_q16;
    return (uint64_t)sps_q16 * (uint64_t)div;
}

static seq_step_id_t seq_runtime_exec_next_play_step(seq_track_id_t track, seq_step_id_t step)
{
    uint8_t length = seq_model_get_track_playback_length(track);
    if (length == 0U)
    {
        length = 1U;
    }

    seq_step_id_t next = (seq_step_id_t)(step + 1U);
    if (next >= length)
    {
        next = 0U;
    }
    return next;
}

static void seq_runtime_exec_schedule_hit_play_and_lookahead(const seq_runtime_state_t *state,
                                                            const seq_boundary_hit_t *hit,
                                                            uint32_t now_tick)
{
    if ((state == 0) || (hit == 0))
    {
        return;
    }

    entity_topology_descriptor_t entity;
    if ((entity_topology_get((brick_entity_id_t)hit->track, &entity) == 0U)
            || (entity_topology_can_emit_notes(&entity) == 0U))
    {
        return;
    }

    const uint64_t scheduled_sample_time = (state->step_sample_q16 >> 16);
    seq_play_scheduler_schedule_step(hit->track,
                                     hit->step,
                                     state->ticks_per_step,
                                     now_tick,
                                     scheduled_sample_time,
                                     state->samples_per_step_q16);

    const seq_step_id_t next_step = seq_runtime_exec_next_play_step(hit->track, hit->step);
    const uint64_t next_step_sample_q16 = state->step_sample_q16
                                          + seq_runtime_exec_track_step_span_q16(hit->track,
                                                                                state->samples_per_step_q16);
    seq_play_scheduler_schedule_step_lookahead_negative(hit->track,
                                                        next_step,
                                                        (next_step_sample_q16 >> 16),
                                                        state->samples_per_step_q16);
}

static void seq_runtime_exec_sort_control_events(seq_runtime_control_event_t *events, uint16_t count)
{
    for (uint16_t i = 1U; i < count; ++i)
    {
        const seq_runtime_control_event_t key = events[i];
        uint16_t j = i;
        while ((j > 0U) && (events[j - 1U].sample_offset_in_block > key.sample_offset_in_block))
        {
            events[j] = events[j - 1U];
            j--;
        }
        events[j] = key;
    }
}

void seq_runtime_exec_reset_sample_timeline(uint64_t start_sample)
{
    g_seq_runtime_exec_sample_timeline = start_sample;
}

uint64_t seq_runtime_exec_get_sample_timeline(void)
{
    /* Timeline projection only: callers read the execution clock, they do not own it. */
    return g_seq_runtime_exec_sample_timeline;
}

uint64_t seq_runtime_exec_begin_control_window(uint16_t block_frames)
{
    const uint64_t block_start_sample = g_seq_runtime_exec_sample_timeline;
    g_seq_runtime_exec_sample_timeline = block_start_sample + (uint64_t)block_frames;
    return block_start_sample;
}

void seq_runtime_exec_prepare_start_lifecycle(seq_runtime_state_t *state,
                                              seq_clock_bridge_t *clock_bridge,
                                              uint32_t now_tick)
{
    if ((state == 0) || (clock_bridge == 0))
    {
        return;
    }

    seq_clock_bridge_prepare_internal_run(clock_bridge);
    state->tick_accum = 0U;
    state->last_tick_count = now_tick;
    state->ext_clock_tick_accum = 0U;
    state->running = 0U;
    state->step_sample_q16 = seq_runtime_exec_get_sample_timeline() << 16;
    seq_runtime_exec_set_external_step_pulses_pending(0U);
    seq_play_scheduler_clear();
    seq_output_guard_reset();
    seq_live_rec_session_reset_capture();
    seq_runtime_exec_set_midi_clock_enabled(0U);
    g_seq_runtime_exec_metronome_step = 0U;
}

void seq_runtime_exec_set_midi_clock_enabled(uint8_t enabled)
{
    g_seq_runtime_exec_midi_clock_enabled = enabled;
}

void seq_runtime_exec_set_midi_clock_period_q16(uint32_t period_q16)
{
    g_seq_runtime_exec_midi_clock_period_q16 = (period_q16 == 0U) ? 1U : period_q16;
}

uint32_t seq_runtime_exec_get_midi_clock_period_q16(void)
{
    return g_seq_runtime_exec_midi_clock_period_q16;
}

void seq_runtime_exec_rebase_midi_clock(uint64_t start_sample)
{
    g_seq_runtime_exec_midi_clock_next_sample_q16 =
            (start_sample << 16) + (uint64_t)g_seq_runtime_exec_midi_clock_period_q16;
}

void seq_runtime_exec_emit_midi_clock_for_block(uint64_t block_start_sample,
                                                uint16_t block_frames,
                                                seq_clock_src_t clock_src,
                                                uint8_t running)
{
    if ((g_seq_runtime_exec_midi_clock_enabled == 0U)
        || (clock_src == SEQ_CLOCK_SRC_EXTERNAL_MIDI)
        || (clock_src == SEQ_CLOCK_SRC_EXTERNAL_USB)
        || (running == 0U)
        || (g_seq_runtime_exec_midi_clock_period_q16 == 0U))
    {
        return;
    }

    const uint64_t block_start_q16 = block_start_sample << 16;
    const uint64_t block_end_q16 = (block_start_sample + (uint64_t)block_frames) << 16;

    while (g_seq_runtime_exec_midi_clock_next_sample_q16 < block_end_q16)
    {
        if (g_seq_runtime_exec_midi_clock_next_sample_q16 >= block_start_q16)
        {
            midi_clock(MIDI_DEST_BOTH);
        }

        g_seq_runtime_exec_midi_clock_next_sample_q16 += (uint64_t)g_seq_runtime_exec_midi_clock_period_q16;
    }
}

void seq_runtime_exec_begin_running_at_sample_q16(seq_runtime_state_t *state,
                                                  seq_transport_fsm_t *transport_fsm,
                                                  seq_clock_bridge_t *clock_bridge,
                                                  uint32_t now_tick,
                                                  uint64_t start_sample_q16)
{
    /* Seam boundary: lifecycle start seeds runtime state, then hands boundaries to scheduler. */
    if ((state == 0) || (transport_fsm == 0) || (clock_bridge == 0))
    {
        return;
    }

    if (seq_transport_fsm_is_running(transport_fsm) == 0U)
    {
        return;
    }

    state->running = 0U;
    state->tick_accum = 0U;
    state->ext_clock_tick_accum = 0U;
    state->last_tick_count = now_tick;

    for (seq_track_id_t track = 0U; track < (seq_track_id_t)SEQ_LANE_CAPACITY; ++track)
    {
        state->play_step[track] = 0U;
        state->prev_step_valid[track] = 0U;
        state->prev_step[track] = 0U;
        state->track_div_phase[track] = 0U;
        seq_boundary_engine_restore_all_active_locks(state, track);
    }

    state->ticks_per_step = (uint16_t)((clock_bridge->internal_next_step_ticks == 0U)
                                           ? 1U
                                           : clock_bridge->internal_next_step_ticks);
    state->step_sample_q16 = start_sample_q16;
    state->running = 1U;
    g_seq_runtime_exec_metronome_step = 0U;
    seq_runtime_exec_push_metronome_for_step(state->step_sample_q16 >> 16);

    if (seq_transport_fsm_allow_schedule_play(transport_fsm) != 0U)
    {
        seq_boundary_hit_t hits[SEQ_LANE_CAPACITY];
        uint8_t hit_count = 0U;
        seq_boundary_engine_process(state, hits, SEQ_LANE_CAPACITY, &hit_count);

        for (uint8_t i = 0U; i < hit_count; ++i)
        {
            if (hits[i].step == 0U)
            {
                seq_runtime_exec_push_boundary_edge(hits[i].track, (state->step_sample_q16 >> 16));
            }
            seq_runtime_exec_schedule_hit_play_and_lookahead(state, &hits[i], now_tick);
        }
    }

    seq_play_scheduler_emit_midi_program_on_transport_start();
    seq_live_rec_session_on_transport_start();
}

void seq_runtime_exec_stop_lifecycle_apply(seq_runtime_state_t *state)
{
    /* Seam boundary: lifecycle stop flushes runtime execution and clears scheduler state. */
    if (state == 0)
    {
        return;
    }

    seq_live_rec_session_on_transport_stop(seq_runtime_exec_get_sample_timeline(), state->samples_per_step_q16);

    state->running = 0U;
    state->tick_accum = 0U;
    state->ext_clock_tick_accum = 0U;
    state->step_sample_q16 = 0U;
    seq_runtime_exec_set_external_step_pulses_pending(0U);
    seq_play_scheduler_clear();
    g_seq_runtime_exec_metronome_step = 0U;

    for (seq_track_id_t track = 0U; track < (seq_track_id_t)SEQ_LANE_CAPACITY; ++track)
    {
        seq_boundary_engine_restore_all_active_locks(state, track);
        state->prev_step_valid[track] = 0U;
        state->track_div_phase[track] = 0U;
    }
}

void seq_runtime_exec_set_external_step_pulses_pending(uint32_t pending)
{
    g_seq_runtime_exec_external_step_pulses_pending = pending;
}

void seq_runtime_exec_increment_external_step_pulses_pending(void)
{
    if (g_seq_runtime_exec_external_step_pulses_pending < UINT32_MAX)
    {
        g_seq_runtime_exec_external_step_pulses_pending++;
    }
}

uint32_t seq_runtime_exec_consume_external_step_pulses_pending(void)
{
    const uint32_t pending = g_seq_runtime_exec_external_step_pulses_pending;
    g_seq_runtime_exec_external_step_pulses_pending = 0U;
    return pending;
}

void seq_runtime_exec_process_step_pulse_at_sample_q16(seq_runtime_state_t *state,
                                                       seq_transport_fsm_t *transport_fsm,
                                                       seq_clock_bridge_t *clock_bridge,
                                                       uint32_t *track_loop_generation,
                                                       uint64_t pulse_sample_q16,
                                                       uint32_t now_tick,
                                                       uint64_t now_sample)
{
    (void)now_tick;
    /* Seam boundary: pulse processing owns runtime progression, not scheduler queue storage. */
    if ((state == 0) || (transport_fsm == 0) || (clock_bridge == 0) || (track_loop_generation == 0))
    {
        return;
    }

    if (seq_transport_fsm_is_stopped(transport_fsm) != 0U)
    {
        return;
    }

    if (seq_transport_fsm_is_start_pending(transport_fsm) != 0U)
    {
        /* Progression guard: a pending-start pulse owns the transition into RUNNING and anchors step zero. */
        state->step_sample_q16 = pulse_sample_q16;
        if (seq_transport_fsm_on_step_pulse(transport_fsm) != 0U)
        {
            seq_runtime_exec_begin_running_at_sample_q16(state,
                                                         transport_fsm,
                                                         clock_bridge,
                                                         now_tick,
                                                         pulse_sample_q16);
        }
        return;
    }

    if (seq_transport_fsm_allow_advance(transport_fsm) != 0U)
    {
        /* Progression guard: only running transport may advance musical step position. */
        uint8_t previous_step[SEQ_LANE_CAPACITY];
        for (seq_track_id_t track = 0U; track < (seq_track_id_t)SEQ_LANE_CAPACITY; ++track)
        {
            previous_step[track] = state->play_step[track];
        }

        seq_boundary_engine_advance_one_step(state);
        for (seq_track_id_t track = 0U; track < (seq_track_id_t)SEQ_LANE_CAPACITY; ++track)
        {
            if ((state->play_step[track] == 0U) && (previous_step[track] != 0U))
            {
                track_loop_generation[track]++;
            }
        }
        seq_live_rec_session_on_step_advanced(state, now_sample);
    }

    state->step_sample_q16 = pulse_sample_q16;
    g_seq_runtime_exec_metronome_step++;
    seq_runtime_exec_push_metronome_for_step(state->step_sample_q16 >> 16);
    if (seq_transport_fsm_allow_schedule_play(transport_fsm) != 0U)
    {
        /* Progression guard: scheduling follows the same transport running state as advancement. */
        seq_boundary_hit_t hits[SEQ_LANE_CAPACITY];
        uint8_t hit_count = 0U;
        seq_boundary_engine_process(state, hits, SEQ_LANE_CAPACITY, &hit_count);

        for (uint8_t i = 0U; i < hit_count; ++i)
        {
            if (hits[i].step == 0U)
            {
                seq_runtime_exec_push_boundary_edge(hits[i].track, (state->step_sample_q16 >> 16));
            }
            seq_runtime_exec_schedule_hit_play_and_lookahead(state, &hits[i], now_tick);
        }
    }

}

void seq_runtime_exec_drive_internal_steps_for_block(seq_runtime_state_t *state,
                                                     seq_transport_fsm_t *transport_fsm,
                                                     seq_clock_bridge_t *clock_bridge,
                                                     uint32_t *track_loop_generation,
                                                     seq_clock_src_t clock_src,
                                                     uint32_t now_tick,
                                                     uint64_t block_start_sample,
                                                     uint16_t block_frames)
{
    if ((state == 0) || (transport_fsm == 0) || (clock_bridge == 0) || (track_loop_generation == 0))
    {
        return;
    }

    if (seq_clock_bridge_is_external_source(clock_src) != 0U)
    {
        return;
    }

    if (seq_transport_fsm_is_stopped(transport_fsm) != 0U)
    {
        return;
    }
    if ((seq_transport_fsm_is_running(transport_fsm) != 0U) && (state->running == 0U))
    {
        return;
    }

    if (state->samples_per_step_q16 == 0U)
    {
        state->samples_per_step_q16 = 1U;
    }

    /* Progression guard: internal cadence only advances from the audio block timeline. */
    const uint64_t block_end_q16 = (block_start_sample + (uint64_t)block_frames) << 16;
    uint64_t next_pulse_sample_q16 = state->step_sample_q16 + (uint64_t)state->samples_per_step_q16;
    while (next_pulse_sample_q16 < block_end_q16)
    {
        seq_runtime_exec_process_step_pulse_at_sample_q16(state,
                                                          transport_fsm,
                                                          clock_bridge,
                                                          track_loop_generation,
                                                          next_pulse_sample_q16,
                                                          now_tick,
                                                          next_pulse_sample_q16 >> 16);
        next_pulse_sample_q16 = state->step_sample_q16 + (uint64_t)state->samples_per_step_q16;
    }
}

void seq_runtime_exec_drive_external_steps_for_block(seq_runtime_state_t *state,
                                                     seq_transport_fsm_t *transport_fsm,
                                                     seq_clock_bridge_t *clock_bridge,
                                                     uint32_t *track_loop_generation,
                                                     seq_clock_src_t clock_src,
                                                     uint32_t now_tick,
                                                     uint64_t block_start_sample,
                                                     uint16_t block_frames)
{
    (void)block_frames;

    /* Seam boundary: external cadence is consumed inside runtime-exec, not in the scheduler. */
    if ((state == 0) || (transport_fsm == 0) || (clock_bridge == 0)
        || (track_loop_generation == 0))
    {
        return;
    }

    if (seq_clock_bridge_is_external_source(clock_src) == 0U)
    {
        return;
    }

    if (seq_transport_fsm_is_stopped(transport_fsm) != 0U)
    {
        return;
    }
    if ((seq_transport_fsm_is_running(transport_fsm) != 0U) && (state->running == 0U))
    {
        return;
    }

    /* Progression guard: external cadence consumes pending pulses only inside the audio block domain. */
    const uint32_t pending_steps = seq_runtime_exec_consume_external_step_pulses_pending();
    if (pending_steps == 0U)
    {
        return;
    }

    const uint64_t pulse_sample_q16 = block_start_sample << 16;
    uint32_t pulses_to_process = pending_steps;
    if (pulses_to_process > SEQ_RUNTIME_EXEC_MAX_EXTERNAL_PULSES_PER_BLOCK)
    {
        pulses_to_process = SEQ_RUNTIME_EXEC_MAX_EXTERNAL_PULSES_PER_BLOCK;
    }

    while (pulses_to_process > 0U)
    {
        seq_runtime_exec_process_step_pulse_at_sample_q16(state,
                                                          transport_fsm,
                                                          clock_bridge,
                                                          track_loop_generation,
                                                          pulse_sample_q16,
                                                          now_tick,
                                                          block_start_sample);
        pulses_to_process--;
    }

    if (pending_steps > SEQ_RUNTIME_EXEC_MAX_EXTERNAL_PULSES_PER_BLOCK)
    {
        const uint32_t skipped = pending_steps
                               - SEQ_RUNTIME_EXEC_MAX_EXTERNAL_PULSES_PER_BLOCK;
        for (seq_track_id_t track = 0U; track < (seq_track_id_t)SEQ_LANE_CAPACITY; ++track)
        {
            uint8_t div = 1U;
            uint8_t length = seq_model_get_track_playback_length(track);
            (void)seq_runtime_get_track_div(track, &div);
            if ((div != 1U) && (div != 2U) && (div != 4U) && (div != 8U))
            {
                div = 1U;
            }
            if (length == 0U)
            {
                length = 1U;
            }

            const uint32_t first_advance = (uint32_t)div
                                           - (uint32_t)state->track_div_phase[track];
            uint32_t advances = 0U;
            if (skipped >= first_advance)
            {
                advances = 1U + ((skipped - first_advance) / (uint32_t)div);
            }
            track_loop_generation[track] +=
                ((uint32_t)state->play_step[track] + advances) / (uint32_t)length;
            state->track_div_phase[track] = (uint8_t)(((uint32_t)state->track_div_phase[track]
                                                       + skipped) % (uint32_t)div);
            state->play_step[track] = (uint8_t)(((uint32_t)state->play_step[track]
                                                 + (advances % (uint32_t)length))
                                                % (uint32_t)length);
            state->prev_step_valid[track] = 0U;
        }
        /* The skipped pulses advance logical phase only; no intermediate boundary is replayed. */
        state->step_sample_q16 = pulse_sample_q16;
    }
}

uint16_t seq_runtime_exec_collect_block_events(seq_runtime_state_t *state,
                                               seq_transport_fsm_t *transport_fsm,
                                               seq_clock_bridge_t *clock_bridge,
                                               uint32_t *track_loop_generation,
                                               seq_runtime_control_event_t *out_events,
                                               uint16_t max_events,
                                               uint16_t block_frames,
                                               seq_clock_src_t clock_src,
                                               uint8_t running)
{
    /* Seam boundary: runtime-exec advances block timeline and drains scheduler output. */
    if ((state == 0) || (transport_fsm == 0) || (clock_bridge == 0) || (track_loop_generation == 0)
        || (out_events == 0) || (max_events == 0U))
    {
        return 0U;
    }

    const uint64_t block_start_sample = seq_runtime_exec_begin_control_window(block_frames);
    const uint32_t now_tick = 0U;
    /* Progression guard: audio block collection drives cadence first, then exports due events. */
    seq_runtime_exec_drive_external_steps_for_block(state,
                                                    transport_fsm,
                                                    clock_bridge,
                                                    track_loop_generation,
                                                    clock_src,
                                                    now_tick,
                                                    block_start_sample,
                                                    block_frames);
    seq_runtime_exec_drive_internal_steps_for_block(state,
                                                    transport_fsm,
                                                    clock_bridge,
                                                    track_loop_generation,
                                                    clock_src,
                                                    now_tick,
                                                    block_start_sample,
                                                    block_frames);
    seq_runtime_exec_emit_midi_clock_for_block(block_start_sample, block_frames, clock_src, running);

    uint16_t total = seq_runtime_exec_collect_boundary_commands(out_events,
                                                              max_events,
                                                              block_frames,
                                                              block_start_sample);
    g_seq_runtime_exec_boundary_event_count = 0U;
    seq_play_scheduler_event_t scheduler_events[16];
    while (total < max_events)
    {
        const uint16_t request = (uint16_t)(((max_events - total) > 16U) ? 16U : (max_events - total));
        const uint16_t count = seq_play_scheduler_collect_due_events(scheduler_events,
                                                                             request,
                                                                             block_frames,
                                                                             block_start_sample);
        if (count == 0U)
        {
            break;
        }

        for (uint16_t i = 0U; i < count; ++i)
        {
            seq_runtime_exec_copy_scheduler_event(&out_events[total + i], &scheduler_events[i]);
        }
        total = (uint16_t)(total + count);
        if (count < request)
        {
            break;
        }
    }

    seq_runtime_exec_sort_control_events(out_events, total);

    return total;
}
