#include "Seq/seq_runtime.h"

#include <string.h>
#include <stdio.h>

#include "Storage/memory_layout.h"
#include "Core/engine_tasklet.h"
#include "midi.h"

#include "Seq/seq_model.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_play_scheduler.h"
#include "Seq/seq_output_guard.h"
#include "Seq/seq_boundary_engine.h"
#include "Seq/seq_live_rec_capture.h"
#include "Seq/seq_transport_fsm.h"
#include "main.h"

#define SEQ_RUNTIME_MIDI_CLOCKS_PER_STEP 6U
#define SEQ_RUNTIME_STEPS_PER_QUARTER_NOTE 4U
#define SEQ_RUNTIME_ENGINE_TICK_HZ 1500U
#define SEQ_RUNTIME_DEFAULT_TEMPO_BPM_MILLI 120000U
#define SEQ_RUNTIME_EXT_TEMPO_TIMEOUT_TICKS (SEQ_RUNTIME_ENGINE_TICK_HZ * 2U)

#ifndef SEQ_DEBUG_TRACK_BINDING
#define SEQ_DEBUG_TRACK_BINDING 0
#endif

#ifndef SEQ_DEBUG_STOP
#define SEQ_DEBUG_STOP 0
#endif

#if SEQ_DEBUG_TRACK_BINDING
#define SEQ_BIND_LOG(...) printf(__VA_ARGS__)
#else
#define SEQ_BIND_LOG(...) do { } while (0)
#endif

#if SEQ_DEBUG_STOP
#define SEQ_STOP_LOG(...) printf(__VA_ARGS__)
#else
#define SEQ_STOP_LOG(...) do { } while (0)
#endif

SEQ_STATE_D2 static seq_runtime_state_t g_seq_runtime;
SEQ_STATE_D2 static uint32_t g_seq_midi_clock_tick_accum;
SEQ_STATE_D2 static uint8_t g_seq_rec_armed;
SEQ_STATE_D2 static uint8_t g_seq_rec_count_in_mode;
SEQ_STATE_D2 static seq_transport_fsm_t g_seq_transport_fsm;
SEQ_STATE_D2 static uint32_t g_seq_tempo_bpm_milli;
SEQ_STATE_D2 static uint32_t g_seq_ext_clock_last_tick;
SEQ_STATE_D2 static uint32_t g_seq_ext_clock_period_accum;
SEQ_STATE_D2 static uint16_t g_seq_ext_clock_period_samples;
SEQ_STATE_D2 static uint8_t g_seq_ext_clock_tempo_valid;
SEQ_STATE_D2 static uint32_t g_seq_ext_clock_bpm_milli;
SEQ_STATE_D2 static uint32_t g_seq_internal_step_ticks_base;
SEQ_STATE_D2 static uint32_t g_seq_internal_step_ticks_rem;
SEQ_STATE_D2 static uint32_t g_seq_internal_step_ticks_den;
SEQ_STATE_D2 static uint32_t g_seq_internal_step_ticks_rem_accum;
SEQ_STATE_D2 static uint32_t g_seq_internal_next_step_ticks;

static uint8_t seq_runtime_clock_source_is_external(seq_clock_src_t src);
static void seq_runtime_external_tempo_reset(void);
static void seq_runtime_internal_step_period_recompute(void);
static uint32_t seq_runtime_internal_step_next_ticks(void);

static uint8_t seq_runtime_clock_source_is_external(seq_clock_src_t src)
{
    return ((src == SEQ_CLOCK_SRC_EXTERNAL_MIDI) || (src == SEQ_CLOCK_SRC_EXTERNAL_USB)) ? 1U : 0U;
}

static void seq_runtime_external_tempo_reset(void)
{
    g_seq_ext_clock_last_tick = 0U;
    g_seq_ext_clock_period_accum = 0U;
    g_seq_ext_clock_period_samples = 0U;
    g_seq_ext_clock_tempo_valid = 0U;
    g_seq_ext_clock_bpm_milli = 0U;
}

static void seq_runtime_internal_step_period_recompute(void)
{
    uint32_t bpm_milli = g_seq_tempo_bpm_milli;
    if (bpm_milli < 40000U)
    {
        bpm_milli = 40000U;
    }
    else if (bpm_milli > 300000U)
    {
        bpm_milli = 300000U;
    }

    const uint64_t num = ((uint64_t)SEQ_RUNTIME_ENGINE_TICK_HZ * 60ULL * 1000ULL);
    const uint32_t den = (uint32_t)((uint64_t)bpm_milli * (uint64_t)SEQ_RUNTIME_STEPS_PER_QUARTER_NOTE);
    uint32_t base = (uint32_t)(num / den);
    if (base == 0U)
    {
        base = 1U;
    }

    g_seq_internal_step_ticks_base = base;
    g_seq_internal_step_ticks_rem = (uint32_t)(num % den);
    g_seq_internal_step_ticks_den = den;
    g_seq_internal_step_ticks_rem_accum = 0U;
    g_seq_internal_next_step_ticks = base;

    /* Keep an integer representative value for existing logic using ticks_per_step. */
    const uint32_t rounded_tps = (uint32_t)((num + ((uint64_t)den / 2ULL)) / (uint64_t)den);
    g_seq_runtime.ticks_per_step = (uint16_t)((rounded_tps == 0U) ? 1U : rounded_tps);
}

static uint32_t seq_runtime_internal_step_next_ticks(void)
{
    uint32_t ticks = g_seq_internal_step_ticks_base;
    if (g_seq_internal_step_ticks_den == 0U)
    {
        return (ticks == 0U) ? 1U : ticks;
    }

    uint32_t rem_accum = g_seq_internal_step_ticks_rem_accum + g_seq_internal_step_ticks_rem;
    if (rem_accum >= g_seq_internal_step_ticks_den)
    {
        rem_accum -= g_seq_internal_step_ticks_den;
        ticks += 1U;
    }
    g_seq_internal_step_ticks_rem_accum = rem_accum;
    return (ticks == 0U) ? 1U : ticks;
}

static void seq_runtime_send_transport_start(void)
{
    if (seq_runtime_clock_source_is_external(g_seq_runtime.clock_src) != 0U)
    {
        return;
    }

    /*
     * MIDI clock TX must follow the requested BPM domain directly.
     * The previous conversion from internal scheduler ticks_per_step introduced
     * a fixed absolute scaling error on clock TX (e.g. 120 BPM request was not
     * forwarded as 120000 milli-BPM).
     *
     * Keep transport start aligned on the explicit 120 BPM baseline until
     * sequencer tempo is sourced from a dedicated BPM parameter.
     */
    midi_clock_set_bpm_milli(g_seq_tempo_bpm_milli);
    midi_start(MIDI_DEST_BOTH);
}

static void seq_runtime_send_transport_stop_and_panic(void)
{
    SEQ_STOP_LOG("[SEQ][STOP] begin\r\n");
    seq_output_guard_panic((seq_runtime_clock_source_is_external(g_seq_runtime.clock_src) == 0U) ? 1U : 0U);
    SEQ_STOP_LOG("[SEQ][STOP] end\r\n");
}

static void seq_runtime_send_internal_clock(uint32_t elapsed_ticks)
{
    (void)elapsed_ticks;
}

static void seq_runtime_begin_running_now(void)
{
    if (seq_transport_fsm_is_running(&g_seq_transport_fsm) == 0U)
    {
        return;
    }

    g_seq_runtime.running = 1U;
    g_seq_runtime.tick_accum = 0U;
    g_seq_runtime.ext_clock_tick_accum = 0U;
    g_seq_runtime.last_tick_count = engine_tick_count;

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        g_seq_runtime.play_step[track] = 0U;
        g_seq_runtime.prev_step_valid[track] = 0U;
        g_seq_runtime.prev_step[track] = 0U;
        seq_boundary_engine_restore_all_active_locks(&g_seq_runtime, track);
    }

    g_seq_midi_clock_tick_accum = 0U;
    seq_runtime_send_transport_start();
}

static uint8_t seq_runtime_live_rec_is_active(void)
{
    return seq_transport_fsm_allow_live_rec(&g_seq_transport_fsm, g_seq_rec_armed);
}

static uint8_t seq_runtime_track_is_valid(seq_track_id_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
}

static void seq_runtime_process_step_boundaries(void)
{
    if (seq_transport_fsm_allow_schedule_play(&g_seq_transport_fsm) == 0U)
    {
        return;
    }

    seq_boundary_hit_t hits[SEQ_TRACK_COUNT];
    uint8_t hit_count = 0U;
    seq_boundary_engine_process(&g_seq_runtime,
                                hits,
                                SEQ_TRACK_COUNT,
                                &hit_count);

    for (uint8_t i = 0U; i < hit_count; ++i)
    {
        seq_play_scheduler_schedule_step(hits[i].track,
                                         hits[i].step,
                                         g_seq_runtime.ticks_per_step,
                                         engine_tick_count);
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
    g_seq_runtime.last_tick_count = engine_tick_count;
    seq_play_scheduler_init();
    seq_output_guard_init();
    seq_live_rec_capture_init();
    seq_transport_fsm_init(&g_seq_transport_fsm);
    g_seq_rec_armed = 0U;
    g_seq_rec_count_in_mode = 0U;
    g_seq_tempo_bpm_milli = SEQ_RUNTIME_DEFAULT_TEMPO_BPM_MILLI;
    seq_runtime_external_tempo_reset();
    seq_runtime_internal_step_period_recompute();
    midi_clock_set_bpm_milli(g_seq_tempo_bpm_milli);
    midi_clock_set_mode(MIDI_CLOCK_MODE_MASTER);

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
    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) == 0U)
    {
        return;
    }

    g_seq_runtime.tick_accum = 0U;
    g_seq_internal_next_step_ticks = seq_runtime_internal_step_next_ticks();
    g_seq_runtime.last_tick_count = engine_tick_count;
    g_seq_runtime.ext_clock_tick_accum = 0U;
    seq_play_scheduler_clear();
    seq_output_guard_reset();
    seq_live_rec_capture_reset();

    if (seq_transport_fsm_request_start(&g_seq_transport_fsm,
                                        g_seq_rec_armed,
                                        g_seq_rec_count_in_mode) == 0U)
    {
        return;
    }

    g_seq_runtime.running = (seq_transport_fsm_is_running(&g_seq_transport_fsm) != 0U) ? 1U : 0U;
    if (g_seq_runtime.running != 0U)
    {
        seq_runtime_begin_running_now();
    }
}

void seq_runtime_stop(void)
{
    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) != 0U)
    {
        return;
    }

    if (seq_transport_fsm_is_start_pending(&g_seq_transport_fsm) != 0U)
    {
        seq_transport_fsm_abort_pending(&g_seq_transport_fsm);
        g_seq_runtime.running = 0U;
        g_seq_runtime.tick_accum = 0U;
        g_seq_runtime.ext_clock_tick_accum = 0U;
        seq_play_scheduler_clear();
        seq_output_guard_reset();
        seq_live_rec_capture_reset();
        return;
    }

    (void)seq_transport_fsm_request_stop(&g_seq_transport_fsm);
    seq_live_rec_capture_flush(engine_tick_count, g_seq_runtime.ticks_per_step);
    g_seq_runtime.running = 0U;
    g_seq_runtime.tick_accum = 0U;
    SEQ_STOP_LOG("[SEQ][STOP] request\r\n");
    seq_play_scheduler_clear();

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        seq_boundary_engine_restore_all_active_locks(&g_seq_runtime, track);
        g_seq_runtime.prev_step_valid[track] = 0U;
    }

    g_seq_midi_clock_tick_accum = 0U;
    seq_runtime_send_transport_stop_and_panic();
    seq_output_guard_reset();
    seq_live_rec_capture_reset();
}

void seq_runtime_toggle_play_stop(void)
{
    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) != 0U)
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
    if ((seq_runtime_clock_source_is_external(g_seq_runtime.clock_src) != 0U)
            && (g_seq_ext_clock_last_tick != 0U))
    {
        const uint32_t silent_ticks = engine_tick_count - g_seq_ext_clock_last_tick;
        if (silent_ticks > SEQ_RUNTIME_EXT_TEMPO_TIMEOUT_TICKS)
        {
            g_seq_ext_clock_tempo_valid = 0U;
            g_seq_ext_clock_bpm_milli = 0U;
            g_seq_ext_clock_period_accum = 0U;
            g_seq_ext_clock_period_samples = 0U;
        }
    }

    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) != 0U)
    {
        g_seq_runtime.last_tick_count = engine_tick_count;
        seq_play_scheduler_service(engine_tick_count, g_seq_runtime.running);
        return;
    }

    if (seq_transport_fsm_is_start_pending(&g_seq_transport_fsm) != 0U)
    {
        const uint32_t current_tick = engine_tick_count;
        if (current_tick == g_seq_runtime.last_tick_count)
        {
            return;
        }

        const uint32_t elapsed = current_tick - g_seq_runtime.last_tick_count;
        g_seq_runtime.last_tick_count = current_tick;
        g_seq_runtime.tick_accum += elapsed;

        while ((g_seq_runtime.tick_accum >= g_seq_internal_next_step_ticks)
               && (seq_transport_fsm_is_start_pending(&g_seq_transport_fsm) != 0U))
        {
            const uint32_t step_ticks = g_seq_internal_next_step_ticks;
            g_seq_runtime.tick_accum -= step_ticks;
            if (seq_transport_fsm_on_step_pulse(&g_seq_transport_fsm) != 0U)
            {
                g_seq_runtime.running = 1U;
            }
            g_seq_internal_next_step_ticks = seq_runtime_internal_step_next_ticks();
        }

        if (seq_transport_fsm_is_running(&g_seq_transport_fsm) != 0U)
        {
            seq_runtime_begin_running_now();
        }

        return;
    }

    if (seq_runtime_clock_source_is_external(g_seq_runtime.clock_src) != 0U)
    {
        seq_runtime_process_step_boundaries();
        seq_play_scheduler_service(engine_tick_count, g_seq_runtime.running);
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

    while (g_seq_runtime.tick_accum >= g_seq_internal_next_step_ticks)
    {
        g_seq_runtime.tick_accum -= g_seq_internal_next_step_ticks;
        if (seq_transport_fsm_allow_advance(&g_seq_transport_fsm) != 0U)
        {
            seq_boundary_engine_advance_one_step(&g_seq_runtime);
        }
        seq_runtime_process_step_boundaries();
        g_seq_internal_next_step_ticks = seq_runtime_internal_step_next_ticks();
    }

    seq_play_scheduler_service(engine_tick_count, g_seq_runtime.running);
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
    seq_transport_fsm_on_clock_source_change(&g_seq_transport_fsm);
    seq_play_scheduler_clear();
    seq_runtime_external_tempo_reset();

    if (seq_runtime_clock_source_is_external(src) != 0U)
    {
        midi_clock_set_mode(MIDI_CLOCK_MODE_SLAVE);
    }
    else
    {
        seq_runtime_internal_step_period_recompute();
        g_seq_internal_next_step_ticks = seq_runtime_internal_step_next_ticks();
        midi_clock_set_mode(MIDI_CLOCK_MODE_MASTER);
        midi_clock_set_bpm_milli(g_seq_tempo_bpm_milli);
    }
}

seq_clock_src_t seq_runtime_get_clock_source(void)
{
    return g_seq_runtime.clock_src;
}

void seq_runtime_midi_clock(void)
{
    seq_runtime_midi_clock_from_source(SEQ_CLOCK_SRC_EXTERNAL_MIDI);
}

void seq_runtime_midi_clock_from_source(seq_clock_src_t source)
{
    if (g_seq_runtime.clock_src != source)
    {
        return;
    }

    const uint32_t now = engine_tick_count;
    if (g_seq_ext_clock_last_tick != 0U)
    {
        const uint32_t delta = now - g_seq_ext_clock_last_tick;
        if ((delta > 0U) && (delta < SEQ_RUNTIME_EXT_TEMPO_TIMEOUT_TICKS))
        {
            g_seq_ext_clock_period_accum += delta;
            if (g_seq_ext_clock_period_samples < 0xFFFFU)
            {
                g_seq_ext_clock_period_samples++;
            }
            if (g_seq_ext_clock_period_samples >= 24U)
            {
                const uint32_t avg_delta = g_seq_ext_clock_period_accum / (uint32_t)g_seq_ext_clock_period_samples;
                if (avg_delta > 0U)
                {
                    g_seq_ext_clock_bpm_milli =
                            (uint32_t)(((uint64_t)SEQ_RUNTIME_ENGINE_TICK_HZ * 60ULL * 1000ULL)
                                       / ((uint64_t)avg_delta * 24ULL));
                    g_seq_ext_clock_tempo_valid = 1U;
                }
            }
        }
        else
        {
            g_seq_ext_clock_period_accum = 0U;
            g_seq_ext_clock_period_samples = 0U;
            g_seq_ext_clock_tempo_valid = 0U;
            g_seq_ext_clock_bpm_milli = 0U;
        }
    }
    g_seq_ext_clock_last_tick = now;

    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) != 0U)
    {
        return;
    }

    g_seq_runtime.ext_clock_tick_accum++;
    if (g_seq_runtime.ext_clock_tick_accum < SEQ_RUNTIME_MIDI_CLOCKS_PER_STEP)
    {
        return;
    }

    g_seq_runtime.ext_clock_tick_accum = 0U;
    if (seq_transport_fsm_is_start_pending(&g_seq_transport_fsm) != 0U)
    {
        if (seq_transport_fsm_on_step_pulse(&g_seq_transport_fsm) != 0U)
        {
            g_seq_runtime.running = 1U;
            seq_runtime_begin_running_now();
        }
        return;
    }

    if (seq_transport_fsm_allow_advance(&g_seq_transport_fsm) != 0U)
    {
        seq_boundary_engine_advance_one_step(&g_seq_runtime);
    }
    seq_runtime_process_step_boundaries();
    seq_play_scheduler_service(engine_tick_count, g_seq_runtime.running);
}

void seq_runtime_midi_start(void)
{
    seq_runtime_midi_start_from_source(SEQ_CLOCK_SRC_EXTERNAL_MIDI);
}

void seq_runtime_midi_start_from_source(seq_clock_src_t source)
{
    if (g_seq_runtime.clock_src != source)
    {
        return;
    }

    seq_runtime_start();
}

void seq_runtime_midi_continue(void)
{
    seq_runtime_midi_continue_from_source(SEQ_CLOCK_SRC_EXTERNAL_MIDI);
}

void seq_runtime_midi_continue_from_source(seq_clock_src_t source)
{
    if (g_seq_runtime.clock_src != source)
    {
        return;
    }

    if (seq_transport_fsm_is_running(&g_seq_transport_fsm) != 0U)
    {
        return;
    }

    if (seq_transport_fsm_request_continue(&g_seq_transport_fsm) == 0U)
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
    seq_runtime_midi_stop_from_source(SEQ_CLOCK_SRC_EXTERNAL_MIDI);
}

void seq_runtime_midi_stop_from_source(seq_clock_src_t source)
{
    if (g_seq_runtime.clock_src != source)
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

void seq_runtime_rec_toggle_arm(void)
{
    g_seq_rec_armed = (g_seq_rec_armed == 0U) ? 1U : 0U;
    if (g_seq_rec_armed == 0U)
    {
        seq_live_rec_capture_flush(engine_tick_count, g_seq_runtime.ticks_per_step);
        seq_live_rec_capture_reset();
        seq_transport_fsm_abort_pending(&g_seq_transport_fsm);
        g_seq_runtime.running = (seq_transport_fsm_is_running(&g_seq_transport_fsm) != 0U) ? 1U : 0U;
    }
}

uint8_t seq_runtime_rec_is_armed(void)
{
    return g_seq_rec_armed;
}

void seq_runtime_set_rec_count_in_mode(uint8_t mode)
{
    if (mode > 3U)
    {
        mode = 3U;
    }

    g_seq_rec_count_in_mode = mode;
}

uint8_t seq_runtime_get_rec_count_in_mode(void)
{
    return g_seq_rec_count_in_mode;
}

uint32_t seq_runtime_get_rec_count_in_remaining_steps(void)
{
    return seq_transport_fsm_get_rec_count_in_remaining_steps(&g_seq_transport_fsm);
}

uint32_t seq_runtime_get_tempo_bpm_milli(void)
{
    return g_seq_tempo_bpm_milli;
}

void seq_runtime_set_tempo_bpm_milli(uint32_t bpm_milli)
{
    if (bpm_milli < 40000U)
    {
        bpm_milli = 40000U;
    }
    else if (bpm_milli > 300000U)
    {
        bpm_milli = 300000U;
    }

    g_seq_tempo_bpm_milli = bpm_milli;
    if (seq_runtime_clock_source_is_external(g_seq_runtime.clock_src) == 0U)
    {
        seq_runtime_internal_step_period_recompute();
        g_seq_internal_next_step_ticks = seq_runtime_internal_step_next_ticks();
        midi_clock_set_bpm_milli(g_seq_tempo_bpm_milli);
    }
}

uint8_t seq_runtime_is_external_tempo_valid(void)
{
    return g_seq_ext_clock_tempo_valid;
}

uint32_t seq_runtime_get_external_tempo_bpm_milli(void)
{
    return g_seq_ext_clock_bpm_milli;
}

void seq_runtime_live_rec_note_on(seq_live_rec_source_t source,
                                  uint8_t channel_zero_based,
                                  uint8_t note,
                                  uint8_t velocity)
{
    seq_live_rec_capture_note_on(seq_runtime_live_rec_is_active(),
                                 &g_seq_runtime,
                                 source,
                                 channel_zero_based,
                                 note,
                                 velocity,
                                 engine_tick_count);
}

void seq_runtime_live_rec_note_off(seq_live_rec_source_t source,
                                   uint8_t channel_zero_based,
                                   uint8_t note)
{
    seq_live_rec_capture_note_off(seq_runtime_live_rec_is_active(),
                                  &g_seq_runtime,
                                  source,
                                  channel_zero_based,
                                  note,
                                  engine_tick_count);
}
