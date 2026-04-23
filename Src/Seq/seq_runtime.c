/*
 * Module: seq_runtime
 * Role: Orchestrateur principal du séquenceur en exécution.
 * Responsibilities: cycle start/stop/process, gestion playhead/ticks,
 * coordination clock bridge, transport FSM, scheduler, boundary engine
 * et facade live-rec.
 * Integration: point d'intégration central des modules Src/Seq avec MIDI et engine_tasklet.
 */
#include "Seq/seq_runtime.h"

#include <string.h>

#define SEQ_RUNTIME_INTERNAL_USE 1

#include "Storage/memory_layout.h"
#include "Core/engine_tasklet.h"
#include "midi.h"

#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_play_scheduler.h"
#include "Seq/seq_output_guard.h"
#include "Seq/seq_boundary_engine.h"
#include "Seq/seq_runtime_exec.h"
#include "Seq/seq_live_rec_session.h"
#include "Seq/seq_transport_fsm.h"
#include "Seq/seq_clock_bridge.h"
#include "main.h"

#define SEQ_RUNTIME_DEFAULT_TEMPO_BPM_MILLI 120000U
#define SEQ_RUNTIME_AUDIO_SAMPLE_RATE 48000U
#define SEQ_RUNTIME_STEPS_PER_QUARTER 4U
#define SEQ_RUNTIME_MIDI_CLOCKS_PER_STEP 6U

#define g_seq_runtime (*seq_runtime_exec_state())
SEQ_STATE_D2 static struct
{
    seq_clock_src_t clock_src;
    uint8_t track_div[SEQ_TRACK_COUNT];
    uint8_t track_quant[SEQ_TRACK_COUNT];
    uint8_t track_swing[SEQ_TRACK_COUNT];
} g_seq_runtime_control;
static volatile uint32_t g_seq_internal_time_tick;
SEQ_STATE_D2 static uint32_t g_seq_track_loop_generation[SEQ_TRACK_COUNT];
SEQ_STATE_D2 static seq_runtime_diag_t g_seq_runtime_diag;
SEQ_STATE_D2 static seq_transport_fsm_t g_seq_transport_fsm;
SEQ_STATE_D2 static seq_clock_bridge_t g_seq_clock_bridge;
static void seq_runtime_stop_lifecycle_apply(uint8_t emit_transport_stop_and_panic);
static void seq_runtime_begin_running_at_sample_q16(uint64_t start_sample_q16);
static uint32_t seq_runtime_get_now_tick_for_source(seq_clock_src_t source);
static uint32_t seq_runtime_get_now_tick(void);
static uint64_t seq_runtime_get_now_sample(void);
static uint32_t seq_runtime_enter_critical(void);
static void seq_runtime_exit_critical(uint32_t primask);
static uint32_t seq_runtime_compute_samples_per_step_q16(uint32_t bpm_milli);
static void seq_runtime_update_samples_per_step_from_tempo(void);
static void seq_runtime_update_midi_clock_period_from_step_period(void);
static void seq_runtime_send_transport_realtime(uint8_t status);
static seq_clock_src_t seq_runtime_get_clock_source_internal(void);

static void seq_runtime_send_transport_realtime(uint8_t status)
{
    const uint8_t msg[1] = { status };
    midi_send_raw(MIDI_DEST_BOTH, msg, sizeof(msg));
}

static seq_clock_src_t seq_runtime_get_clock_source_internal(void)
{
    return g_seq_runtime_control.clock_src;
}

static void seq_runtime_send_transport_start(void)
{
    if (seq_clock_bridge_is_external_source(seq_runtime_get_clock_source_internal()) != 0U)
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
    seq_runtime_exec_set_midi_clock_audio_enabled(1U);
    seq_runtime_exec_rebase_midi_clock_audio(seq_runtime_exec_get_audio_timeline_sample());
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
    return seq_runtime_get_now_tick_for_source(seq_runtime_get_clock_source_internal());
}

static uint64_t seq_runtime_get_now_sample(void)
{
    return seq_runtime_exec_get_audio_timeline_sample();
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
    seq_runtime_begin_running_at_sample_q16((uint64_t)seq_runtime_exec_get_audio_timeline_sample() << 16);
}

static void seq_runtime_begin_running_at_sample_q16(uint64_t start_sample_q16)
{
    seq_runtime_exec_begin_running_at_sample_q16(&g_seq_runtime,
                                                 &g_seq_transport_fsm,
                                                 &g_seq_clock_bridge,
                                                 seq_runtime_get_now_tick(),
                                                 start_sample_q16);
    seq_runtime_send_transport_start();
}

static uint8_t seq_runtime_track_is_valid(seq_track_id_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
}

static void seq_runtime_stop_lifecycle_apply(uint8_t emit_transport_stop_and_panic)
{
    seq_runtime_exec_stop_lifecycle_apply(&g_seq_runtime);
    if (emit_transport_stop_and_panic != 0U)
    {
        seq_output_guard_panic((seq_clock_bridge_is_external_source(seq_runtime_get_clock_source_internal()) == 0U) ? 1U : 0U);
    }
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
    if ((seq_clock_bridge_is_external_source(seq_runtime_get_clock_source_internal()) != 0U)
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
    seq_runtime_exec_set_midi_clock_period_q16(period_q16);
}

void seq_runtime_init(void)
{
    seq_model_init_defaults();
    seq_param_iface_init();

    seq_runtime_exec_init();
    memset(g_seq_track_loop_generation, 0, sizeof(g_seq_track_loop_generation));
    /* TODO(clock-source): wire this to a global runtime/menu setting (INT/EXT).
     * For now, keep forced to internal clock to preserve current UX. */
    g_seq_runtime_control.clock_src = SEQ_CLOCK_SRC_INTERNAL;
    g_seq_internal_time_tick = 0U;
    seq_runtime_exec_set_external_step_pulses_pending(0U);
    g_seq_runtime_diag = (seq_runtime_diag_t){0};
    g_seq_runtime.last_tick_count = seq_runtime_get_now_tick();
    seq_play_scheduler_init();
    seq_output_guard_init();
    seq_live_rec_session_init();
    seq_transport_fsm_init(&g_seq_transport_fsm);
    seq_clock_bridge_init(&g_seq_clock_bridge,
                          &g_seq_runtime,
                          SEQ_RUNTIME_DEFAULT_TEMPO_BPM_MILLI);
    seq_runtime_exec_reset_audio_timeline(0U);
    g_seq_runtime.step_sample_q16 = 0U;
    seq_runtime_exec_set_midi_clock_audio_enabled(0U);
    seq_runtime_exec_set_midi_clock_period_q16(1U);
    seq_runtime_update_samples_per_step_from_tempo();
    midi_clock_set_bpm_milli(seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge));
    midi_clock_set_mode(MIDI_CLOCK_MODE_MASTER);

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        g_seq_runtime_control.track_div[track] = 1U;
        g_seq_runtime_control.track_quant[track] = 0U;
        g_seq_runtime_control.track_swing[track] = 0U;
    }
}

void seq_runtime_start(void)
{
    uint8_t begin_running_now = 0U;
    const uint32_t primask = seq_runtime_enter_critical();
    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) == 0U)
    {
        seq_runtime_exit_critical(primask);
        return;
    }

    seq_runtime_exec_prepare_start_lifecycle(&g_seq_runtime,
                                             &g_seq_clock_bridge,
                                             seq_runtime_get_now_tick());
    seq_runtime_update_samples_per_step_from_tempo();

    if (seq_transport_fsm_request_start(&g_seq_transport_fsm,
                                        seq_live_rec_session_rec_is_armed(),
                                        seq_live_rec_session_get_rec_count_in_mode()) == 0U)
    {
        seq_runtime_exit_critical(primask);
        return;
    }

    begin_running_now = (seq_transport_fsm_is_running(&g_seq_transport_fsm) != 0U) ? 1U : 0U;
    seq_runtime_exit_critical(primask);

    if (begin_running_now != 0U)
    {
        seq_runtime_begin_running_now();
    }
}

void seq_runtime_stop(void)
{
    uint8_t apply_stop_lifecycle = 0U;
    uint8_t emit_transport_stop_and_panic = 0U;
    const uint32_t primask = seq_runtime_enter_critical();
    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) != 0U)
    {
        seq_runtime_exit_critical(primask);
        return;
    }

    if (seq_transport_fsm_is_start_pending(&g_seq_transport_fsm) != 0U)
    {
        seq_transport_fsm_abort_pending(&g_seq_transport_fsm);
        apply_stop_lifecycle = 1U;
        emit_transport_stop_and_panic = 0U;
    }
    else
    {
        (void)seq_transport_fsm_request_stop(&g_seq_transport_fsm);
        apply_stop_lifecycle = 1U;
        emit_transport_stop_and_panic = 1U;
    }

    seq_runtime_exit_critical(primask);

    if (apply_stop_lifecycle != 0U)
    {
        seq_runtime_stop_lifecycle_apply(emit_transport_stop_and_panic);
    }
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

uint8_t seq_runtime_is_start_pending(void)
{
    return seq_transport_fsm_is_start_pending(&g_seq_transport_fsm);
}

static void seq_runtime_process_core(void)
{
    const uint32_t now_tick = seq_runtime_get_now_tick();
    seq_clock_bridge_on_process(&g_seq_clock_bridge, seq_runtime_get_clock_source_internal(), now_tick);

    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) != 0U)
    {
        g_seq_runtime.last_tick_count = now_tick;
        return;
    }

    if (seq_transport_fsm_is_start_pending(&g_seq_transport_fsm) != 0U)
    {
        g_seq_runtime.last_tick_count = now_tick;
        return;
    }

    if (seq_clock_bridge_is_external_source(seq_runtime_get_clock_source_internal()) != 0U)
    {
        /* Boundary advance is driven from the execution block path. */
        return;
    }

    g_seq_runtime.last_tick_count = now_tick;
}

void seq_runtime_time_adapter_process(void)
{
    /*
     * Runtime core is serviced from superloop for transport/external-clock
     * state work. Internal step progression is driven from audio block domain.
     */
    seq_runtime_process_core();
}

void seq_runtime_time_adapter_process_internal_from_irq(void)
{
    if (seq_clock_bridge_is_external_source(seq_runtime_get_clock_source_internal()) == 0U)
    {
        g_seq_internal_time_tick++;
        g_seq_runtime_diag.internal_irq_tick_count++;
    }
}

uint16_t seq_runtime_audio_collect_block_events(seq_runtime_audio_event_t *out_events,
                                                uint16_t max_events,
                                                uint16_t block_frames)
{
    if ((out_events == NULL) || (max_events == 0U))
    {
        return 0U;
    }

    return seq_runtime_exec_collect_block_events(&g_seq_runtime,
                                                 &g_seq_transport_fsm,
                                                 &g_seq_clock_bridge,
                                                 &g_seq_runtime_diag,
                                                 g_seq_track_loop_generation,
                                                 out_events,
                                                 max_events,
                                                 block_frames,
                                                 seq_runtime_get_clock_source_internal(),
                                                 g_seq_runtime.running);
}

void seq_runtime_audio_apply_event(const seq_runtime_audio_event_t *event)
{
    if (event == NULL)
    {
        return;
    }
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

    g_seq_runtime_control.clock_src = src;
    seq_clock_bridge_set_source(&g_seq_clock_bridge, &g_seq_runtime, src);
    if (seq_clock_bridge_is_external_source(src) == 0U)
    {
        g_seq_internal_time_tick = 0U;
    }
    seq_runtime_exec_set_external_step_pulses_pending(0U);
    seq_play_scheduler_clear();
    seq_runtime_update_samples_per_step_from_tempo();

    if (seq_clock_bridge_is_external_source(src) != 0U)
    {
        seq_runtime_exec_set_midi_clock_audio_enabled(0U);
        midi_clock_set_running(false);
        midi_clock_set_mode(MIDI_CLOCK_MODE_SLAVE);
    }
    else
    {
        midi_clock_set_running(false);
        midi_clock_set_mode(MIDI_CLOCK_MODE_MASTER);
        midi_clock_set_bpm_milli(seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge));
        seq_runtime_exec_rebase_midi_clock_audio(seq_runtime_exec_get_audio_timeline_sample());
    }
    seq_runtime_exit_critical(primask);
}

seq_clock_src_t seq_runtime_get_clock_source(void)
{
    return seq_runtime_get_clock_source_internal();
}

void seq_runtime_midi_clock_from_source(seq_clock_src_t source)
{
    if (seq_runtime_get_clock_source_internal() != source)
    {
        return;
    }

    const uint32_t now = seq_runtime_get_now_tick_for_source(source);
    uint8_t step_pulse = 0U;
    if (seq_clock_bridge_on_external_clock_pulse(&g_seq_clock_bridge,
                                                 &g_seq_runtime,
                                                 seq_runtime_get_clock_source_internal(),
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
    const uint32_t primask = seq_runtime_enter_critical();
    seq_runtime_exec_increment_external_step_pulses_pending();
    seq_runtime_exit_critical(primask);
}

void seq_runtime_diag_reset(void)
{
    const uint32_t primask = seq_runtime_enter_critical();
    g_seq_runtime_diag = (seq_runtime_diag_t){0};
    seq_runtime_exit_critical(primask);
}

void seq_runtime_diag_snapshot(seq_runtime_diag_t *out_diag)
{
    if (out_diag == NULL)
    {
        return;
    }

    const uint32_t primask = seq_runtime_enter_critical();
    *out_diag = g_seq_runtime_diag;
    seq_runtime_exit_critical(primask);
}

void seq_runtime_midi_start_from_source(seq_clock_src_t source)
{
    if (seq_runtime_get_clock_source_internal() != source)
    {
        return;
    }

    seq_runtime_start();
}

void seq_runtime_midi_continue_from_source(seq_clock_src_t source)
{
    const uint8_t was_stopped = seq_transport_fsm_is_stopped(&g_seq_transport_fsm);

    if (seq_runtime_get_clock_source_internal() != source)
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
    seq_runtime_exec_set_external_step_pulses_pending(0U);
    g_seq_runtime.last_tick_count = seq_runtime_get_now_tick();
    if (was_stopped != 0U)
    {
        /*
         * CONTINUE after STOP must re-anchor the musical timeline to the
         * absolute audio sample timeline, exactly like START path does.
         * Without this rebase, step_sample_q16 can remain at 0 while
         * audio_timeline_sample is monotonic, causing boundary misalignment.
         */
        g_seq_runtime.step_sample_q16 = (uint64_t)seq_runtime_exec_get_audio_timeline_sample() << 16;
        /* Boundary advance is driven from the execution block path. */
    }

    if (seq_clock_bridge_is_external_source(source) != 0U)
    {
        seq_runtime_exec_set_midi_clock_audio_enabled(0U);
        midi_clock_set_running(false);
        return;
    }

    midi_clock_set_running(false);
    seq_runtime_send_transport_realtime(0xFBU);
    seq_runtime_exec_set_midi_clock_audio_enabled(1U);
    seq_runtime_exec_rebase_midi_clock_audio(seq_runtime_exec_get_audio_timeline_sample());
}

void seq_runtime_midi_stop_from_source(seq_clock_src_t source)
{
    if (seq_runtime_get_clock_source_internal() != source)
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
    if ((g_seq_runtime.running != 0U) && (step == 0U))
    {
        seq_play_scheduler_notify_track_pattern_change(track);
    }
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

uint8_t seq_runtime_get_track_loop_generation(seq_track_id_t track, uint32_t *out_generation)
{
    if ((out_generation == 0) || (track >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    *out_generation = g_seq_track_loop_generation[track];
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
    g_seq_runtime_control.track_div[track] = div;
    g_seq_runtime.track_div_phase[track] = 0U;
}

uint8_t seq_runtime_get_track_div(seq_track_id_t track, uint8_t *out_div)
{
    if ((out_div == NULL) || (track >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    *out_div = g_seq_runtime_control.track_div[track];
    return 1U;
}

void seq_runtime_set_track_quant(seq_track_id_t track, uint8_t quant)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (quant > 100U)
    {
        quant = 100U;
    }
    g_seq_runtime_control.track_quant[track] = quant;
}

uint8_t seq_runtime_get_track_quant(seq_track_id_t track, uint8_t *out_quant)
{
    if ((out_quant == NULL) || (track >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    *out_quant = g_seq_runtime_control.track_quant[track];
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
    g_seq_runtime_control.track_swing[track] = swing;
}

uint8_t seq_runtime_get_track_swing(seq_track_id_t track, uint8_t *out_swing)
{
    if ((out_swing == NULL) || (track >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    *out_swing = g_seq_runtime_control.track_swing[track];
    return 1U;
}

void seq_runtime_rec_toggle_arm(void)
{
    const uint8_t pending_before = seq_live_rec_session_rec_is_pattern_pending_start();
    const uint8_t armed_before = seq_live_rec_session_rec_is_armed();
    seq_live_rec_session_toggle_arm(seq_runtime_get_now_sample(), g_seq_runtime.samples_per_step_q16);

    if ((armed_before != 0U)
        && (pending_before == 0U)
        && (seq_live_rec_session_rec_is_armed() == 0U))
    {
        seq_transport_fsm_abort_pending(&g_seq_transport_fsm);
        g_seq_runtime.running = (seq_transport_fsm_is_running(&g_seq_transport_fsm) != 0U) ? 1U : 0U;
    }
}

uint8_t seq_runtime_rec_is_armed(void)
{
    return seq_live_rec_session_rec_is_armed();
}

void seq_runtime_set_rec_count_in_mode(uint8_t mode)
{
    seq_live_rec_session_set_rec_count_in_mode(mode);
}

uint8_t seq_runtime_get_rec_count_in_mode(void)
{
    return seq_live_rec_session_get_rec_count_in_mode();
}

void seq_runtime_set_rec_len_mode(uint8_t mode)
{
    seq_live_rec_session_set_rec_len_mode(mode);
}

uint8_t seq_runtime_get_rec_len_mode(void)
{
    return seq_live_rec_session_get_rec_len_mode();
}

uint32_t seq_runtime_get_rec_count_in_remaining_steps(void)
{
    return seq_transport_fsm_get_rec_count_in_remaining_steps(&g_seq_transport_fsm);
}

uint8_t seq_runtime_rec_is_pattern_pending_start(void)
{
    return seq_live_rec_session_rec_is_pattern_pending_start();
}

uint32_t seq_runtime_get_tempo_bpm_milli(void)
{
    return seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge);
}

void seq_runtime_set_tempo_bpm_milli(uint32_t bpm_milli)
{
    seq_clock_bridge_set_internal_tempo(&g_seq_clock_bridge, &g_seq_runtime, bpm_milli);
    seq_runtime_update_samples_per_step_from_tempo();
    if (seq_clock_bridge_is_external_source(seq_runtime_get_clock_source_internal()) == 0U)
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

uint8_t seq_runtime_live_rec_param_write(seq_track_id_t track,
                                         uint8_t set_id,
                                         seq_param8_t param8,
                                         seq_value16_t value16)
{
    return seq_live_rec_session_live_rec_param_write(&g_seq_runtime,
                                                     track,
                                                     set_id,
                                                     param8,
                                                     value16);
}

void seq_runtime_set_pattern_rec_target_track(seq_track_id_t track)
{
    seq_live_rec_session_set_pattern_rec_target_track(track);
}

uint8_t seq_runtime_live_rec_param_can_write(seq_track_id_t track,
                                             uint8_t set_id,
                                             seq_param8_t param8)
{
    return seq_live_rec_session_live_rec_param_can_write(track, set_id, param8);
}

void seq_runtime_live_rec_note_on(seq_live_rec_source_t source,
                                  uint8_t channel_zero_based,
                                  uint8_t note,
                                  uint8_t velocity)
{
    seq_live_rec_session_live_rec_note_on(source,
                                          channel_zero_based,
                                          note,
                                          velocity,
                                          &g_seq_runtime,
                                          seq_runtime_get_now_sample());
}

void seq_runtime_live_rec_note_off(seq_live_rec_source_t source,
                                   uint8_t channel_zero_based,
                                   uint8_t note)
{
    seq_live_rec_session_live_rec_note_off(source,
                                           channel_zero_based,
                                           note,
                                           &g_seq_runtime,
                                           seq_runtime_get_now_sample());
}

void seq_runtime_on_midi_program_live_change(uint8_t track, float program_value)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    seq_play_scheduler_live_midi_program_changed(track, program_value);
}

void seq_runtime_on_track_pattern_change(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (g_seq_runtime.running == 0U)
    {
        return;
    }

    seq_play_scheduler_notify_track_pattern_change(track);
}

