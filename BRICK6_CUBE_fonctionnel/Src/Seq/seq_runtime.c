/*
 * Module: seq_runtime
 * Role: Orchestrateur principal du séquenceur en exécution.
 * Responsibilities: cycle start/stop/process, gestion playhead/ticks,
 * coordination clock bridge, transport FSM, scheduler, boundary engine et live-rec.
 * Integration: point d'intégration central des modules Src/Seq avec MIDI et engine_tasklet.
 */
#include "Seq/seq_runtime.h"

#include <string.h>
#include <stdio.h>

#define SEQ_RUNTIME_INTERNAL_USE 1

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
#include "Seq/seq_clock_bridge.h"
#include "UI/ui_core.h"
#include "main.h"

#define SEQ_RUNTIME_DEFAULT_TEMPO_BPM_MILLI 120000U
#define SEQ_RUNTIME_AUDIO_SAMPLE_RATE 48000U
#define SEQ_RUNTIME_STEPS_PER_QUARTER 4U
#define SEQ_RUNTIME_MIDI_CLOCKS_PER_STEP 6U

#ifndef SEQ_DEBUG_TRACK_BINDING
#define SEQ_DEBUG_TRACK_BINDING 0
#endif

#ifndef SEQ_DEBUG_STOP
#define SEQ_DEBUG_STOP 0
#endif

#ifndef SEQ_DEBUG_SAMPLE_DOMAIN_TRACE
#define SEQ_DEBUG_SAMPLE_DOMAIN_TRACE 0
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

#if SEQ_DEBUG_SAMPLE_DOMAIN_TRACE
#define SEQ_SAMPLE_LOG(...) printf(__VA_ARGS__)
#else
#define SEQ_SAMPLE_LOG(...) do { } while (0)
#endif

SEQ_STATE_D2 static seq_runtime_state_t g_seq_runtime;
SEQ_STATE_D2 static uint32_t g_seq_midi_clock_tick_accum;
SEQ_STATE_D2 static uint8_t g_seq_rec_armed;
SEQ_STATE_D2 static uint8_t g_seq_rec_count_in_mode;
SEQ_STATE_D2 static uint8_t g_seq_rec_len_mode;
SEQ_STATE_D2 static uint8_t g_seq_pattern_rec_pending_start;
SEQ_STATE_D2 static uint8_t g_seq_pattern_rec_active;
SEQ_STATE_D2 static uint8_t g_seq_pattern_rec_track;
SEQ_STATE_D2 static uint32_t g_seq_pattern_rec_steps_remaining;
static volatile uint32_t g_seq_internal_time_tick;
SEQ_STATE_D2 static uint8_t g_seq_midi_clock_audio_enabled;
SEQ_STATE_D2 static uint32_t g_seq_midi_clock_period_q16;
SEQ_STATE_D2 static uint64_t g_seq_midi_clock_next_sample_q16;
SEQ_STATE_D2 static seq_transport_fsm_t g_seq_transport_fsm;
SEQ_STATE_D2 static seq_clock_bridge_t g_seq_clock_bridge;
static void seq_runtime_pattern_rec_start_now(void);
static void seq_runtime_process_step_boundaries(void);
static void seq_runtime_live_rec_flush_and_reset(void);
static void seq_runtime_stop_lifecycle_apply(uint8_t emit_transport_stop_and_panic);
static uint32_t seq_runtime_get_now_tick_for_source(seq_clock_src_t source);
static uint32_t seq_runtime_get_now_tick(void);
static uint64_t seq_runtime_get_now_sample(void);
static uint32_t seq_runtime_enter_critical(void);
static void seq_runtime_exit_critical(uint32_t primask);
static uint32_t seq_runtime_compute_samples_per_step_q16(uint32_t bpm_milli);
static void seq_runtime_update_samples_per_step_from_tempo(void);
static void seq_runtime_update_midi_clock_period_from_step_period(void);
static void seq_runtime_midi_clock_audio_rebase(uint64_t start_sample);
static void seq_runtime_midi_clock_audio_emit_for_block(uint64_t block_start_sample, uint16_t block_frames);
static void seq_runtime_send_transport_realtime(uint8_t status);

static void seq_runtime_send_transport_realtime(uint8_t status)
{
    const uint8_t msg[1] = { status };
    midi_send_raw(MIDI_DEST_BOTH, msg, sizeof(msg));
}

static void seq_runtime_send_transport_start(void)
{
    if (seq_clock_bridge_is_external_source(g_seq_runtime.clock_src) != 0U)
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
    midi_clock_set_bpm_milli(seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge));
    midi_clock_set_running(false);
    seq_runtime_send_transport_realtime(0xFAU);
    g_seq_midi_clock_audio_enabled = 1U;
    seq_runtime_midi_clock_audio_rebase(g_seq_runtime.audio_timeline_sample);
}

static void seq_runtime_send_transport_stop_and_panic(void)
{
    SEQ_STOP_LOG("[SEQ][STOP] begin\r\n");
    g_seq_midi_clock_audio_enabled = 0U;
    seq_output_guard_panic((seq_clock_bridge_is_external_source(g_seq_runtime.clock_src) == 0U) ? 1U : 0U);
    SEQ_STOP_LOG("[SEQ][STOP] end\r\n");
}

static uint32_t seq_runtime_get_now_tick_for_source(seq_clock_src_t source)
{
    if (seq_clock_bridge_is_external_source(source) != 0U)
    {
        return engine_tick_count;
    }

    return g_seq_internal_time_tick;
}

static uint32_t seq_runtime_get_now_tick(void)
{
    return seq_runtime_get_now_tick_for_source(g_seq_runtime.clock_src);
}

static uint64_t seq_runtime_get_now_sample(void)
{
    const uint32_t primask = seq_runtime_enter_critical();
    const uint64_t now_sample = g_seq_runtime.audio_timeline_sample;
    seq_runtime_exit_critical(primask);
    return now_sample;
}

static uint32_t seq_runtime_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void seq_runtime_exit_critical(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
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
    g_seq_runtime.last_tick_count = seq_runtime_get_now_tick();

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        g_seq_runtime.play_step[track] = 0U;
        g_seq_runtime.prev_step_valid[track] = 0U;
        g_seq_runtime.prev_step[track] = 0U;
        g_seq_runtime.track_div_phase[track] = 0U;
        seq_boundary_engine_restore_all_active_locks(&g_seq_runtime, track);
    }

    g_seq_runtime.ticks_per_step =
            (uint16_t)((g_seq_clock_bridge.internal_next_step_ticks == 0U)
                           ? 1U
                           : g_seq_clock_bridge.internal_next_step_ticks);
    g_seq_runtime.step_sample_q16 = (uint64_t)g_seq_runtime.audio_timeline_sample << 16;

    /*
     * Force deterministic initial boundary at RUN entry.
     * This guarantees step 0 scheduling exactly once at START/
     * START_PENDING->RUNNING transition, independent of next tick timing.
     */
    seq_runtime_process_step_boundaries();

    seq_runtime_send_transport_start();

    if ((g_seq_rec_len_mode == (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
        && (g_seq_rec_armed != 0U))
    {
        seq_runtime_pattern_rec_start_now();
    }
}

static uint8_t seq_runtime_live_rec_is_active(void)
{
    if (g_seq_rec_len_mode == (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
    {
        return g_seq_pattern_rec_active;
    }

    return seq_transport_fsm_allow_live_rec(&g_seq_transport_fsm, g_seq_rec_armed);
}

static uint32_t seq_runtime_get_track_pattern_length_steps(seq_track_id_t track)
{
    return seq_model_get_track_playback_length(track);
}

static void seq_runtime_pattern_rec_start_now(void)
{
    const uint8_t track = ui_get_active_track();
    const uint32_t length = seq_runtime_get_track_pattern_length_steps(track);

    g_seq_pattern_rec_track = track;
    g_seq_pattern_rec_steps_remaining = length;
    g_seq_pattern_rec_pending_start = 0U;
    g_seq_pattern_rec_active = 1U;

    seq_step_id_t steps[SEQ_MAX_STEPS];
    for (uint32_t i = 0U; i < length; ++i)
    {
        steps[i] = (seq_step_id_t)i;
    }
    seq_edit_clear_steps(track, steps, (uint8_t)length);
    seq_live_rec_capture_reset();
}

static void seq_runtime_pattern_rec_cancel(void)
{
    g_seq_pattern_rec_pending_start = 0U;
    g_seq_pattern_rec_active = 0U;
    g_seq_pattern_rec_steps_remaining = 0U;
}

static void seq_runtime_pattern_rec_on_step_advanced(void)
{
    if ((g_seq_rec_len_mode != (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
        || (g_seq_rec_armed == 0U))
    {
        return;
    }

    if ((g_seq_pattern_rec_pending_start != 0U)
        && (g_seq_runtime.play_step[g_seq_pattern_rec_track] == 0U))
    {
        seq_runtime_pattern_rec_start_now();
    }

    if (g_seq_pattern_rec_active == 0U)
    {
        return;
    }

    if (g_seq_pattern_rec_steps_remaining > 0U)
    {
        g_seq_pattern_rec_steps_remaining--;
    }

    if (g_seq_pattern_rec_steps_remaining == 0U)
    {
        seq_live_rec_capture_flush(seq_runtime_get_now_sample(), g_seq_runtime.samples_per_step_q16);
        seq_live_rec_capture_reset();
        seq_runtime_pattern_rec_cancel();
        g_seq_rec_armed = 0U;
    }
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
                                         seq_runtime_get_now_tick(),
                                         (g_seq_runtime.step_sample_q16 >> 16),
                                         g_seq_runtime.samples_per_step_q16);
    }
}

static void seq_runtime_live_rec_flush_and_reset(void)
{
    seq_live_rec_capture_flush(seq_runtime_get_now_sample(), g_seq_runtime.samples_per_step_q16);
    seq_live_rec_capture_reset();
}

static void seq_runtime_stop_lifecycle_apply(uint8_t emit_transport_stop_and_panic)
{
    seq_runtime_live_rec_flush_and_reset();
    seq_runtime_pattern_rec_cancel();

    g_seq_runtime.running = 0U;
    g_seq_runtime.tick_accum = 0U;
    g_seq_runtime.ext_clock_tick_accum = 0U;
    /*
     * Keep audio_timeline_sample monotonic across STOP/START cycles:
     * audio scheduling authority stays the absolute block timeline, while
     * step_sample_q16 is re-anchored explicitly at RUN entry.
     */
    g_seq_runtime.step_sample_q16 = 0U;
    seq_play_scheduler_clear();

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        seq_boundary_engine_restore_all_active_locks(&g_seq_runtime, track);
        g_seq_runtime.prev_step_valid[track] = 0U;
        g_seq_runtime.track_div_phase[track] = 0U;
    }

    g_seq_midi_clock_tick_accum = 0U;
    if (emit_transport_stop_and_panic != 0U)
    {
        SEQ_STOP_LOG("[SEQ][STOP] request\r\n");
        seq_runtime_send_transport_stop_and_panic();
    }

    seq_output_guard_reset();
}

static void seq_runtime_process_step_pulse(uint32_t now)
{
    (void)now;

    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) != 0U)
    {
        return;
    }

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
        seq_runtime_pattern_rec_on_step_advanced();
    }

    g_seq_runtime.step_sample_q16 += (uint64_t)g_seq_runtime.samples_per_step_q16;
    seq_runtime_process_step_boundaries();
}

static uint32_t seq_runtime_compute_samples_per_step_q16(uint32_t bpm_milli)
{
    if (bpm_milli == 0U)
    {
        bpm_milli = SEQ_RUNTIME_DEFAULT_TEMPO_BPM_MILLI;
    }

    const uint64_t num = ((uint64_t)SEQ_RUNTIME_AUDIO_SAMPLE_RATE * 60ULL * 1000ULL) << 16;
    const uint64_t den = (uint64_t)bpm_milli * (uint64_t)SEQ_RUNTIME_STEPS_PER_QUARTER;
    uint32_t q16 = (uint32_t)(num / den);
    if (q16 == 0U)
    {
        q16 = 1U;
    }
    return q16;
}

static void seq_runtime_update_samples_per_step_from_tempo(void)
{
    uint32_t bpm_milli = seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge);
    if ((seq_clock_bridge_is_external_source(g_seq_runtime.clock_src) != 0U)
        && (seq_clock_bridge_is_external_tempo_valid(&g_seq_clock_bridge) != 0U))
    {
        bpm_milli = seq_clock_bridge_get_external_tempo_bpm_milli(&g_seq_clock_bridge);
    }
    g_seq_runtime.samples_per_step_q16 = seq_runtime_compute_samples_per_step_q16(bpm_milli);
    seq_runtime_update_midi_clock_period_from_step_period();
}

static void seq_runtime_update_midi_clock_period_from_step_period(void)
{
    uint32_t period_q16 = g_seq_runtime.samples_per_step_q16 / SEQ_RUNTIME_MIDI_CLOCKS_PER_STEP;
    if (period_q16 == 0U)
    {
        period_q16 = 1U;
    }
    g_seq_midi_clock_period_q16 = period_q16;
}

static void seq_runtime_midi_clock_audio_rebase(uint64_t start_sample)
{
    g_seq_midi_clock_next_sample_q16 = (start_sample << 16) + (uint64_t)g_seq_midi_clock_period_q16;
}

static void seq_runtime_midi_clock_audio_emit_for_block(uint64_t block_start_sample, uint16_t block_frames)
{
    if ((g_seq_midi_clock_audio_enabled == 0U)
        || (seq_clock_bridge_is_external_source(g_seq_runtime.clock_src) != 0U)
        || (g_seq_runtime.running == 0U)
        || (g_seq_midi_clock_period_q16 == 0U))
    {
        return;
    }

    const uint64_t block_start_q16 = block_start_sample << 16;
    const uint64_t block_end_q16 = (block_start_sample + (uint64_t)block_frames) << 16;

    while (g_seq_midi_clock_next_sample_q16 < block_end_q16)
    {
        if (g_seq_midi_clock_next_sample_q16 >= block_start_q16)
        {
            midi_clock(MIDI_DEST_BOTH);
        }

        g_seq_midi_clock_next_sample_q16 += (uint64_t)g_seq_midi_clock_period_q16;
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
    g_seq_internal_time_tick = 0U;
    g_seq_runtime.last_tick_count = seq_runtime_get_now_tick();
    seq_play_scheduler_init();
    seq_output_guard_init();
    seq_live_rec_capture_init();
    seq_transport_fsm_init(&g_seq_transport_fsm);
    g_seq_rec_armed = 0U;
    g_seq_rec_count_in_mode = 0U;
    g_seq_rec_len_mode = (uint8_t)SEQ_REC_LEN_MODE_OVERDUB;
    g_seq_pattern_rec_pending_start = 0U;
    g_seq_pattern_rec_active = 0U;
    g_seq_pattern_rec_track = 0U;
    g_seq_pattern_rec_steps_remaining = 0U;
    seq_clock_bridge_init(&g_seq_clock_bridge,
                          &g_seq_runtime,
                          SEQ_RUNTIME_DEFAULT_TEMPO_BPM_MILLI);
    g_seq_runtime.audio_block_start_sample = 0U;
    g_seq_runtime.audio_timeline_sample = 0U;
    g_seq_runtime.step_sample_q16 = 0U;
    g_seq_midi_clock_audio_enabled = 0U;
    g_seq_midi_clock_period_q16 = 1U;
    g_seq_midi_clock_next_sample_q16 = 0U;
    seq_runtime_update_samples_per_step_from_tempo();
    midi_clock_set_bpm_milli(seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge));
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
    const uint32_t primask = seq_runtime_enter_critical();
    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) == 0U)
    {
        seq_runtime_exit_critical(primask);
        return;
    }

    g_seq_runtime.tick_accum = 0U;
    seq_clock_bridge_prepare_internal_run(&g_seq_clock_bridge);
    g_seq_runtime.last_tick_count = seq_runtime_get_now_tick();
    g_seq_runtime.ext_clock_tick_accum = 0U;
    seq_play_scheduler_clear();
    seq_output_guard_reset();
    seq_live_rec_capture_reset();
    /*
     * Do not rewind audio_timeline_sample here (audio IRQ keeps advancing it
     * continuously). START will rebase step_sample_q16 in begin_running_now().
     */
    g_seq_runtime.step_sample_q16 = 0U;
    g_seq_midi_clock_audio_enabled = 0U;
    seq_runtime_update_samples_per_step_from_tempo();

    if (seq_transport_fsm_request_start(&g_seq_transport_fsm,
                                        g_seq_rec_armed,
                                        g_seq_rec_count_in_mode) == 0U)
    {
        seq_runtime_exit_critical(primask);
        return;
    }

    g_seq_runtime.running = (seq_transport_fsm_is_running(&g_seq_transport_fsm) != 0U) ? 1U : 0U;
    if (g_seq_runtime.running != 0U)
    {
        seq_runtime_begin_running_now();
    }
    seq_runtime_exit_critical(primask);
}

void seq_runtime_stop(void)
{
    const uint32_t primask = seq_runtime_enter_critical();
    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) != 0U)
    {
        seq_runtime_exit_critical(primask);
        return;
    }

    if (seq_transport_fsm_is_start_pending(&g_seq_transport_fsm) != 0U)
    {
        seq_transport_fsm_abort_pending(&g_seq_transport_fsm);
        seq_runtime_stop_lifecycle_apply(0U);
        seq_runtime_exit_critical(primask);
        return;
    }

    (void)seq_transport_fsm_request_stop(&g_seq_transport_fsm);
    seq_runtime_stop_lifecycle_apply(1U);
    seq_runtime_exit_critical(primask);
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

static void seq_runtime_process_core(void)
{
    const uint32_t now_tick = seq_runtime_get_now_tick();
    seq_clock_bridge_on_process(&g_seq_clock_bridge, g_seq_runtime.clock_src, now_tick);

    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) != 0U)
    {
        g_seq_runtime.last_tick_count = now_tick;
        return;
    }

    if (seq_transport_fsm_is_start_pending(&g_seq_transport_fsm) != 0U)
    {
        const uint32_t current_tick = now_tick;
        if (current_tick == g_seq_runtime.last_tick_count)
        {
            return;
        }

        const uint32_t elapsed = current_tick - g_seq_runtime.last_tick_count;
        g_seq_runtime.last_tick_count = current_tick;
        g_seq_runtime.tick_accum += elapsed;

        while ((seq_clock_bridge_consume_internal_step_due(&g_seq_clock_bridge, &g_seq_runtime.tick_accum) != 0U)
               && (seq_transport_fsm_is_start_pending(&g_seq_transport_fsm) != 0U))
        {
            g_seq_runtime.ticks_per_step =
                    (uint16_t)((g_seq_clock_bridge.internal_next_step_ticks == 0U)
                                   ? 1U
                                   : g_seq_clock_bridge.internal_next_step_ticks);

            seq_runtime_process_step_pulse(current_tick);
        }

        return;
    }

    if (seq_clock_bridge_is_external_source(g_seq_runtime.clock_src) != 0U)
    {
        seq_runtime_process_step_boundaries();
        return;
    }

    const uint32_t current_tick = now_tick;
    if (current_tick == g_seq_runtime.last_tick_count)
    {
        return;
    }

    const uint32_t elapsed = current_tick - g_seq_runtime.last_tick_count;
    g_seq_runtime.last_tick_count = current_tick;
    g_seq_runtime.tick_accum += elapsed;
    while (seq_clock_bridge_consume_internal_step_due(&g_seq_clock_bridge, &g_seq_runtime.tick_accum) != 0U)
    {
        g_seq_runtime.ticks_per_step =
                (uint16_t)((g_seq_clock_bridge.internal_next_step_ticks == 0U)
                               ? 1U
                               : g_seq_clock_bridge.internal_next_step_ticks);

        seq_runtime_process_step_pulse(current_tick);
    }

}

void seq_runtime_time_adapter_process(void)
{
    /*
     * Superloop adapter kept for external clock path only.
     * Internal clock path is invoked directly from TIM5 cadence IRQ
     * through seq_runtime_time_adapter_process_internal_from_irq().
     */
    if (seq_clock_bridge_is_external_source(g_seq_runtime.clock_src) != 0U)
    {
        seq_runtime_process_core();
    }
}

void seq_runtime_time_adapter_process_internal_from_irq(void)
{
    if (seq_clock_bridge_is_external_source(g_seq_runtime.clock_src) == 0U)
    {
        g_seq_internal_time_tick++;
        seq_runtime_process_core();
    }
}

uint16_t seq_runtime_audio_collect_block_events(seq_runtime_audio_event_t *out_events,
                                                uint16_t max_events,
                                                uint16_t block_frames)
{
    seq_play_scheduler_audio_event_t scheduler_events[16];
    if ((out_events == NULL) || (max_events == 0U))
    {
        return 0U;
    }

    uint16_t total = 0U;
    const uint64_t block_start_sample = g_seq_runtime.audio_timeline_sample;
    g_seq_runtime.audio_block_start_sample = block_start_sample;
    g_seq_runtime.audio_timeline_sample = block_start_sample + (uint64_t)block_frames;
    SEQ_SAMPLE_LOG("[SEQ][AUD][COLLECT] b0=%llu frames=%u max=%u\r\n",
                   (unsigned long long)block_start_sample,
                   (unsigned)block_frames,
                   (unsigned)max_events);
    seq_runtime_midi_clock_audio_emit_for_block(block_start_sample, block_frames);
    while (total < max_events)
    {
        const uint16_t request = (uint16_t)(((max_events - total) > 16U) ? 16U : (max_events - total));
        const uint16_t count = seq_play_scheduler_audio_collect_block_events(scheduler_events,
                                                                             request,
                                                                             block_frames,
                                                                             block_start_sample);
        if (count == 0U)
        {
            break;
        }

        for (uint16_t i = 0U; i < count; ++i)
        {
            out_events[total + i].type = scheduler_events[i].type;
            out_events[total + i].track = scheduler_events[i].track;
            out_events[total + i].note = scheduler_events[i].note;
            out_events[total + i].velocity = scheduler_events[i].velocity;
            out_events[total + i].sample_offset_in_block = scheduler_events[i].sample_offset_in_block;
            SEQ_SAMPLE_LOG("[SEQ][AUD][EVT] i=%u ty=%u tr=%u n=%u vel=%u off=%u abs=%llu\r\n",
                           (unsigned)(total + i),
                           (unsigned)scheduler_events[i].type,
                           (unsigned)scheduler_events[i].track,
                           (unsigned)scheduler_events[i].note,
                           (unsigned)scheduler_events[i].velocity,
                           (unsigned)scheduler_events[i].sample_offset_in_block,
                           (unsigned long long)(block_start_sample + scheduler_events[i].sample_offset_in_block));
        }
        total = (uint16_t)(total + count);

        if (count < request)
        {
            break;
        }
    }

    return total;
}

void seq_runtime_audio_apply_event(const seq_runtime_audio_event_t *event)
{
    if (event == NULL)
    {
        return;
    }

    SEQ_SAMPLE_LOG("[SEQ][AUD][APPLY] ty=%u tr=%u n=%u vel=%u off=%u abs=%llu\r\n",
                   (unsigned)event->type,
                   (unsigned)event->track,
                   (unsigned)event->note,
                   (unsigned)event->velocity,
                   (unsigned)event->sample_offset_in_block,
                   (unsigned long long)(g_seq_runtime.audio_block_start_sample + event->sample_offset_in_block));

    seq_play_scheduler_audio_event_t scheduler_event;
    scheduler_event.type = event->type;
    scheduler_event.track = event->track;
    scheduler_event.note = event->note;
    scheduler_event.velocity = event->velocity;
    scheduler_event.sample_offset_in_block = event->sample_offset_in_block;
    seq_play_scheduler_audio_apply_event(&scheduler_event);
}

void seq_runtime_set_clock_source(seq_clock_src_t src)
{
    const uint32_t primask = seq_runtime_enter_critical();
    if ((uint8_t)src >= (uint8_t)SEQ_CLOCK_SRC_COUNT)
    {
        seq_runtime_exit_critical(primask);
        return;
    }

    seq_clock_bridge_set_source(&g_seq_clock_bridge, &g_seq_runtime, src);
    if (seq_clock_bridge_is_external_source(src) == 0U)
    {
        g_seq_internal_time_tick = 0U;
    }
    g_seq_midi_clock_tick_accum = 0U;
    seq_play_scheduler_clear();
    seq_runtime_update_samples_per_step_from_tempo();

    if (seq_clock_bridge_is_external_source(src) != 0U)
    {
        g_seq_midi_clock_audio_enabled = 0U;
        midi_clock_set_running(false);
        midi_clock_set_mode(MIDI_CLOCK_MODE_SLAVE);
    }
    else
    {
        midi_clock_set_running(false);
        midi_clock_set_mode(MIDI_CLOCK_MODE_MASTER);
        midi_clock_set_bpm_milli(seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge));
        seq_runtime_midi_clock_audio_rebase(g_seq_runtime.audio_timeline_sample);
    }
    seq_runtime_exit_critical(primask);
}

seq_clock_src_t seq_runtime_get_clock_source(void)
{
    return g_seq_runtime.clock_src;
}

void seq_runtime_midi_clock_from_source(seq_clock_src_t source)
{
    if (g_seq_runtime.clock_src != source)
    {
        return;
    }

    const uint32_t now = seq_runtime_get_now_tick_for_source(source);
    uint8_t step_pulse = 0U;
    if (seq_clock_bridge_on_external_clock_pulse(&g_seq_clock_bridge,
                                                 &g_seq_runtime,
                                                 g_seq_runtime.clock_src,
                                                 source,
                                                 now,
                                                 &step_pulse) == 0U)
    {
        return;
    }

    if (step_pulse == 0U)
    {
        return;
    }

    seq_runtime_update_samples_per_step_from_tempo();
    seq_runtime_process_step_pulse(now);
}

void seq_runtime_midi_start_from_source(seq_clock_src_t source)
{
    if (g_seq_runtime.clock_src != source)
    {
        return;
    }

    seq_runtime_start();
}

void seq_runtime_midi_continue_from_source(seq_clock_src_t source)
{
    const uint8_t was_stopped = seq_transport_fsm_is_stopped(&g_seq_transport_fsm);

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
    g_seq_runtime.last_tick_count = seq_runtime_get_now_tick();
    if (was_stopped != 0U)
    {
        /*
         * CONTINUE after STOP must re-anchor the musical timeline to the
         * absolute audio sample timeline, exactly like START path does.
         * Without this rebase, step_sample_q16 can remain at 0 while
         * audio_timeline_sample is monotonic, causing boundary misalignment.
         */
        g_seq_runtime.step_sample_q16 = (uint64_t)g_seq_runtime.audio_timeline_sample << 16;
        seq_runtime_process_step_boundaries();
    }

    if (seq_clock_bridge_is_external_source(source) != 0U)
    {
        g_seq_midi_clock_audio_enabled = 0U;
        midi_clock_set_running(false);
        return;
    }

    midi_clock_set_running(false);
    seq_runtime_send_transport_realtime(0xFBU);
    g_seq_midi_clock_audio_enabled = 1U;
    seq_runtime_midi_clock_audio_rebase(g_seq_runtime.audio_timeline_sample);
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
    if (seq_runtime_track_is_valid(track) == 0U)
    {
        return 0U;
    }

    const uint8_t length = seq_model_get_track_playback_length(track);
    if (step >= length)
    {
        step = 0U;
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

void seq_runtime_on_track_length_changed(seq_track_id_t track)
{
    if (seq_runtime_track_is_valid(track) == 0U)
    {
        return;
    }

    const uint8_t length = seq_model_get_track_playback_length(track);
    const uint8_t play_step_out_of_range = (g_seq_runtime.play_step[track] >= length) ? 1U : 0U;

    if (play_step_out_of_range != 0U)
    {
        /*
         * Runtime shrink while playing:
         * keep step 0 as the *next* boundary hit (not immediately skipped).
         * If we clamp to step 0 here, the next step pulse advances to step 1
         * and step 0 never retriggers on the first loop after the shrink.
         */
        g_seq_runtime.play_step[track] = (uint8_t)(length - 1U);
    }
    if (g_seq_runtime.prev_step[track] >= length)
    {
        g_seq_runtime.prev_step[track] = g_seq_runtime.play_step[track];
    }
}

void seq_runtime_set_track_div(seq_track_id_t track, uint8_t div)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if ((div != 1U) && (div != 2U) && (div != 4U) && (div != 8U))
    {
        div = 1U;
    }
    g_seq_runtime.track_div[track] = div;
    g_seq_runtime.track_div_phase[track] = 0U;
}

uint8_t seq_runtime_get_track_div(seq_track_id_t track, uint8_t *out_div)
{
    if ((out_div == NULL) || (track >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    *out_div = g_seq_runtime.track_div[track];
    return 1U;
}

void seq_runtime_set_track_quant(seq_track_id_t track, uint8_t quant)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_seq_runtime.track_quant[track] = quant;
}

uint8_t seq_runtime_get_track_quant(seq_track_id_t track, uint8_t *out_quant)
{
    if ((out_quant == NULL) || (track >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    *out_quant = g_seq_runtime.track_quant[track];
    return 1U;
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

uint8_t seq_runtime_get_track_swing(seq_track_id_t track, uint8_t *out_swing)
{
    if ((out_swing == NULL) || (track >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    *out_swing = g_seq_runtime.track_swing[track];
    return 1U;
}

void seq_runtime_rec_toggle_arm(void)
{
    if (g_seq_rec_len_mode == (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
    {
        if (g_seq_pattern_rec_pending_start != 0U)
        {
            g_seq_rec_armed = 0U;
            seq_runtime_pattern_rec_cancel();
            seq_runtime_live_rec_flush_and_reset();
            return;
        }

        if ((g_seq_rec_armed == 0U) && (seq_runtime_is_running() != 0U))
        {
            g_seq_rec_armed = 1U;
            g_seq_pattern_rec_track = ui_get_active_track();
            g_seq_pattern_rec_pending_start = 1U;
            g_seq_pattern_rec_active = 0U;
            g_seq_pattern_rec_steps_remaining = 0U;
            seq_runtime_live_rec_flush_and_reset();
            return;
        }
    }

    g_seq_rec_armed = (g_seq_rec_armed == 0U) ? 1U : 0U;
    if (g_seq_rec_armed == 0U)
    {
        seq_runtime_live_rec_flush_and_reset();
        seq_transport_fsm_abort_pending(&g_seq_transport_fsm);
        seq_runtime_pattern_rec_cancel();
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

void seq_runtime_set_rec_len_mode(uint8_t mode)
{
    if (mode > (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
    {
        mode = (uint8_t)SEQ_REC_LEN_MODE_PATTERN;
    }

    g_seq_rec_len_mode = mode;
    if (g_seq_rec_len_mode == (uint8_t)SEQ_REC_LEN_MODE_OVERDUB)
    {
        seq_runtime_pattern_rec_cancel();
    }
}

uint8_t seq_runtime_get_rec_len_mode(void)
{
    return g_seq_rec_len_mode;
}

uint32_t seq_runtime_get_rec_count_in_remaining_steps(void)
{
    return seq_transport_fsm_get_rec_count_in_remaining_steps(&g_seq_transport_fsm);
}

uint8_t seq_runtime_rec_is_pattern_pending_start(void)
{
    return ((g_seq_rec_len_mode == (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
            && (g_seq_pattern_rec_pending_start != 0U)) ? 1U : 0U;
}

uint32_t seq_runtime_get_tempo_bpm_milli(void)
{
    return seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge);
}

void seq_runtime_set_tempo_bpm_milli(uint32_t bpm_milli)
{
    seq_clock_bridge_set_internal_tempo(&g_seq_clock_bridge, &g_seq_runtime, bpm_milli);
    seq_runtime_update_samples_per_step_from_tempo();
    if (seq_clock_bridge_is_external_source(g_seq_runtime.clock_src) == 0U)
    {
        midi_clock_set_bpm_milli(seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge));
    }
}

uint8_t seq_runtime_is_external_tempo_valid(void)
{
    return seq_clock_bridge_is_external_tempo_valid(&g_seq_clock_bridge);
}

uint32_t seq_runtime_get_external_tempo_bpm_milli(void)
{
    return seq_clock_bridge_get_external_tempo_bpm_milli(&g_seq_clock_bridge);
}

void seq_runtime_live_rec_note_on(seq_live_rec_source_t source,
                                  uint8_t channel_zero_based,
                                  uint8_t note,
                                  uint8_t velocity)
{
    SEQ_SAMPLE_LOG("[SEQ][LREC][RT-ON] src=%u ch=%u n=%u vel=%u now=%llu act=%u\r\n",
                   (unsigned)source,
                   (unsigned)channel_zero_based,
                   (unsigned)note,
                   (unsigned)velocity,
                   (unsigned long long)seq_runtime_get_now_sample(),
                   (unsigned)seq_runtime_live_rec_is_active());
    seq_live_rec_capture_note_on(seq_runtime_live_rec_is_active(),
                                 &g_seq_runtime,
                                 source,
                                 channel_zero_based,
                                 note,
                                 velocity,
                                 seq_runtime_get_now_sample());
}

void seq_runtime_live_rec_note_off(seq_live_rec_source_t source,
                                   uint8_t channel_zero_based,
                                   uint8_t note)
{
    SEQ_SAMPLE_LOG("[SEQ][LREC][RT-OFF] src=%u ch=%u n=%u now=%llu act=%u\r\n",
                   (unsigned)source,
                   (unsigned)channel_zero_based,
                   (unsigned)note,
                   (unsigned long long)seq_runtime_get_now_sample(),
                   (unsigned)seq_runtime_live_rec_is_active());
    seq_live_rec_capture_note_off(seq_runtime_live_rec_is_active(),
                                  &g_seq_runtime,
                                  source,
                                  channel_zero_based,
                                  note,
                                  seq_runtime_get_now_sample());
}
