/*
 * Module: seq_runtime
 * Role: Orchestrateur principal du s�quenceur en ex�cution.
 * Responsibilities: cycle start/stop/process, gestion playhead/ticks,
 * coordination clock bridge, transport FSM, scheduler, boundary engine
 * et facade live-rec.
 * Integration: point d'int�gration central des modules Src/Seq avec MIDI et engine_tasklet.
 */
#include "Seq/seq_runtime.h"

#include <string.h>

#define SEQ_RUNTIME_INTERNAL_USE 1

#include "Storage/memory_layout.h"
#include "Audio/metronome_runtime.h"
#include "Audio/control_audio_queue.h"
#include "NoteFx/note_fx_pipeline.h"
#include "Core/engine_tasklet.h"
#include "Core/live_clock.h"
#include "Core/track_runtime.h"
#include "Keyboard/keyboard_runtime.h"
#include "midi.h"

#include "Seq/seq_model.h"
#include "Seq/seq_edit.h"
#include "Core/entity_topology.h"
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
#define SEQ_RUNTIME_LIVE_REC_QUEUE_CAPACITY 128U

/* Shared execution state lives in seq_runtime_exec. */
#define g_seq_runtime (*seq_runtime_exec_state())
SEQ_STATE_D2 static struct
{
    seq_clock_src_t clock_src;
    uint8_t track_div[SEQ_LANE_CAPACITY];
    uint8_t track_quant[SEQ_LANE_CAPACITY];
    uint8_t track_swing[SEQ_LANE_CAPACITY];
} g_seq_runtime_control;
static volatile uint32_t g_seq_internal_time_tick;
SEQ_STATE_D2 static uint32_t g_seq_track_loop_generation[SEQ_LANE_CAPACITY];
SEQ_STATE_D2 static seq_transport_fsm_t g_seq_transport_fsm;
SEQ_STATE_D2 static seq_clock_bridge_t g_seq_clock_bridge;
typedef struct
{
    uint64_t effective_sample_time;
    uint32_t ingress_serial;
    uint32_t occurrence_id;
    uint8_t source;
    uint8_t is_note_on;
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
} seq_runtime_live_rec_event_t;
static seq_runtime_live_rec_event_t
    g_seq_runtime_live_rec_queue[SEQ_RUNTIME_LIVE_REC_QUEUE_CAPACITY];
static volatile uint8_t g_seq_runtime_live_rec_head;
static volatile uint8_t g_seq_runtime_live_rec_tail;
static volatile uint8_t g_seq_runtime_live_rec_count;
static uint64_t g_seq_runtime_control_sample_cursor;
static uint8_t g_seq_runtime_trigger_start_bypass;
static void seq_runtime_stop_lifecycle_apply(uint8_t emit_transport_stop_and_panic);
static void seq_runtime_control_apply_event(
    const seq_runtime_control_event_t *event);
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
static uint8_t seq_runtime_clamp_track_div(uint8_t div);
static uint8_t seq_runtime_clamp_percent(uint8_t value);
static void seq_runtime_copy_control_event(seq_play_scheduler_event_t *scheduler_event,
                                         const seq_runtime_control_event_t *event);
static uint8_t seq_runtime_rec_start_mode_to_roll_mode(uint8_t mode);

static void seq_runtime_send_transport_realtime(uint8_t status)
{
    const uint8_t msg[1] = { status };
    midi_send_raw(MIDI_DEST_BOTH, msg, sizeof(msg));
}

static seq_clock_src_t seq_runtime_get_clock_source_internal(void)
{
    return g_seq_runtime_control.clock_src;
}

static uint8_t seq_runtime_clamp_track_div(uint8_t div)
{
    if ((div == 1U) || (div == 2U) || (div == 4U) || (div == 8U))
    {
        return div;
    }

    return 1U;
}

static uint8_t seq_runtime_clamp_percent(uint8_t value)
{
    return (value > 100U) ? 100U : value;
}

static void seq_runtime_copy_control_event(seq_play_scheduler_event_t *scheduler_event,
                                         const seq_runtime_control_event_t *event)
{
    if ((scheduler_event == NULL) || (event == NULL))
    {
        return;
    }

    scheduler_event->type = event->type;
    scheduler_event->track = event->track;
    scheduler_event->note = event->note;
    scheduler_event->velocity = event->velocity;
    scheduler_event->track_generation = event->track_generation;
    scheduler_event->reserved = 0U;
    scheduler_event->sample_offset_in_block = event->sample_offset_in_block;
    scheduler_event->sample_abs = event->sample_abs;
    scheduler_event->generation = event->generation;
    scheduler_event->event_token = event->event_token;
}

static uint8_t seq_runtime_rec_start_mode_to_roll_mode(uint8_t mode)
{
    switch (mode)
    {
        case (uint8_t)SEQ_REC_START_ROLL_1_4:
            return 1U;
        case (uint8_t)SEQ_REC_START_ROLL_1_2:
            return 2U;
        case (uint8_t)SEQ_REC_START_ROLL_1:
            return 3U;
        case (uint8_t)SEQ_REC_START_DEFAULT:
        case (uint8_t)SEQ_REC_START_TRIG:
        default:
            return 0U;
    }
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
    seq_runtime_exec_set_midi_clock_enabled(1U);
    seq_runtime_exec_rebase_midi_clock(seq_runtime_get_now_sample());
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
    uint64_t sample = 0U;
    (void)live_clock_read_audio_sample(&sample);
    return sample;
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


static uint8_t seq_runtime_track_is_valid(seq_track_id_t track)
{
    return entity_topology_is_active((brick_entity_id_t)track);
}

static void seq_runtime_stop_lifecycle_apply(uint8_t emit_transport_stop_and_panic)
{
    seq_edit_note_capture_reset();
    seq_runtime_exec_stop_lifecycle_apply(&g_seq_runtime);
    metronome_runtime_stop();
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

    /* Orchestration seam: runtime bootstrap delegates execution-state ownership to seq_runtime_exec. */
    seq_runtime_exec_init();
    memset(g_seq_track_loop_generation, 0, sizeof(g_seq_track_loop_generation));
    /* Default to internal clock at boot; runtime policy may retarget later. */
    g_seq_runtime_control.clock_src = SEQ_CLOCK_SRC_INTERNAL;
    g_seq_internal_time_tick = 0U;
    seq_runtime_exec_set_external_step_pulses_pending(0U);
    g_seq_runtime_live_rec_head = 0U;
    g_seq_runtime_live_rec_tail = 0U;
    g_seq_runtime_live_rec_count = 0U;
    memset(g_seq_runtime_live_rec_queue, 0, sizeof(g_seq_runtime_live_rec_queue));
    g_seq_runtime.last_tick_count = seq_runtime_get_now_tick();
    seq_play_scheduler_init();
    seq_output_guard_init();
    seq_live_rec_session_init();
    seq_transport_fsm_init(&g_seq_transport_fsm);
    seq_clock_bridge_init(&g_seq_clock_bridge,
                          &g_seq_runtime,
                          SEQ_RUNTIME_DEFAULT_TEMPO_BPM_MILLI);
    seq_runtime_exec_reset_sample_timeline(0U);
    g_seq_runtime.step_sample_q16 = 0U;
    seq_runtime_exec_set_midi_clock_enabled(0U);
    seq_runtime_exec_set_midi_clock_period_q16(1U);
    seq_runtime_update_samples_per_step_from_tempo();
    midi_clock_set_bpm_milli(seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge));
    midi_clock_set_mode(MIDI_CLOCK_MODE_MASTER);

    for (seq_track_id_t track = 0U; track < (seq_track_id_t)SEQ_LANE_CAPACITY; ++track)
    {
        g_seq_runtime_control.track_div[track] = 1U;
        g_seq_runtime_control.track_quant[track] = 0U;
        g_seq_runtime_control.track_swing[track] = 0U;
    }
}

void seq_runtime_start(void)
{
    uint8_t begin_running_now = 0U;
    if (g_seq_runtime_trigger_start_bypass == 0U)
    {
        if (seq_live_rec_session_rec_should_wait_trigger_start() != 0U)
        {
            return;
        }
    }
    g_seq_runtime_trigger_start_bypass = 0U;
    const uint32_t primask = seq_runtime_enter_critical();
    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) == 0U)
    {
        seq_runtime_exit_critical(primask);
        return;
    }

    /* Orchestration seam: runtime asks clock policy to prepare cadence, then asks transport FSM for START. */
    seq_runtime_exec_prepare_start_lifecycle(&g_seq_runtime,
                                             &g_seq_clock_bridge,
                                             seq_runtime_get_now_tick());
    seq_runtime_update_samples_per_step_from_tempo();

    /* Orchestration seam: transport FSM owns the start transition and count-in state. */
    if (seq_transport_fsm_request_start(&g_seq_transport_fsm,
                                        seq_live_rec_session_rec_is_armed(),
                                        seq_runtime_rec_start_mode_to_roll_mode(seq_live_rec_session_get_rec_start_mode())) == 0U)
    {
        seq_runtime_exit_critical(primask);
        return;
    }

    begin_running_now = (seq_transport_fsm_is_running(&g_seq_transport_fsm) != 0U) ? 1U : 0U;
    if (begin_running_now != 0U)
    {
        seq_runtime_exec_begin_running_at_sample_q16(&g_seq_runtime,
                                                     &g_seq_transport_fsm,
                                                     &g_seq_clock_bridge,
                                                     seq_runtime_get_now_tick(),
                                                     (uint64_t)seq_runtime_get_now_sample() << 16);
    }
    seq_runtime_exit_critical(primask);

    if (begin_running_now != 0U)
    {
        seq_runtime_send_transport_start();
    }
}

void seq_runtime_stop(void)
{
    uint8_t apply_stop_lifecycle = 0U;
    uint8_t emit_transport_stop_and_panic = 0U;
    const uint32_t primask = seq_runtime_enter_critical();
    seq_live_rec_session_clear_trigger_start_wait();
    if (seq_transport_fsm_is_stopped(&g_seq_transport_fsm) != 0U)
    {
        seq_runtime_exit_critical(primask);
        return;
    }

    /* Orchestration seam: STOP resolves through transport FSM, then runtime applies the lifecycle. */
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
    /* Orchestration seam: clock bridge only supervises cadence policy here; transport state is checked separately. */
    seq_clock_bridge_on_process(&g_seq_clock_bridge, seq_runtime_get_clock_source_internal(), now_tick);

    const uint64_t audio_sample = seq_runtime_get_now_sample();
    if (g_seq_runtime_control_sample_cursor > audio_sample)
        g_seq_runtime_control_sample_cursor = audio_sample;
    while (g_seq_runtime_control_sample_cursor < audio_sample)
    {
        const uint64_t remaining = audio_sample - g_seq_runtime_control_sample_cursor;
        const uint16_t frames = (remaining > UINT16_MAX)
            ? UINT16_MAX : (uint16_t)remaining;
        seq_runtime_control_event_t events[128];
        note_fx_pipeline_begin_control_window(frames);
        seq_play_scheduler_control_begin_window(SEQ_PLAY_SCHEDULER_HALF_EVENT_QUOTA);
        const uint16_t count = seq_runtime_exec_collect_block_events(
            &g_seq_runtime, &g_seq_transport_fsm, &g_seq_clock_bridge,
            g_seq_track_loop_generation,
            events, 128U, frames, seq_runtime_get_clock_source_internal(),
            g_seq_runtime.running);
        g_seq_runtime_control_sample_cursor += frames;

        for (uint16_t i = 0U; i < count; ++i)
        {
            const seq_runtime_control_event_t *const event = &events[i];
            if ((event->type == SEQ_RUNTIME_AUDIO_EVENT_BOUNDARY_EDGE)
                    || (event->type == SEQ_RUNTIME_AUDIO_EVENT_METRO_CLICK))
            {
                const control_audio_event_t audio_event = {
                    .due_sample = event->sample_abs,
                    .entity_id = (brick_entity_id_t)event->track,
                    .kind = (event->type == SEQ_RUNTIME_AUDIO_EVENT_BOUNDARY_EDGE)
                        ? (uint8_t)CONTROL_AUDIO_EVENT_BOUNDARY_EDGE
                        : (uint8_t)CONTROL_AUDIO_EVENT_METRONOME_CLICK,
                    .flags = event->velocity
                };
                (void)control_audio_queue_publish(&audio_event);
            }
            else
            {
                seq_runtime_control_apply_event(event);
            }
        }
        note_fx_pipeline_process(g_seq_runtime_control_sample_cursor - frames,
                                 frames, g_seq_runtime.samples_per_step_q16);
        note_fx_pipeline_end_control_window();
        seq_play_scheduler_control_end_window();
    }

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
    /* Orchestration seam: superloop services clock policy and transport supervision only. */
    seq_runtime_process_core();
}

void seq_runtime_time_adapter_process_internal_from_irq(void)
{
    if (seq_clock_bridge_is_external_source(seq_runtime_get_clock_source_internal()) == 0U)
    {
        g_seq_internal_time_tick++;
    }
}

static void seq_runtime_control_apply_event(const seq_runtime_control_event_t *event)
{
    if (event == NULL)
    {
        return;
    }
    /* Audio apply seam: runtime forwards collected events to scheduler/engines only. */
    seq_play_scheduler_event_t scheduler_event;
    seq_runtime_copy_control_event(&scheduler_event, event);
    seq_play_scheduler_control_apply_event(&scheduler_event);
}

void seq_runtime_set_clock_source(seq_clock_src_t src)
{
    if ((uint8_t)src >= (uint8_t)SEQ_CLOCK_SRC_COUNT)
        return;
    if (src == seq_runtime_get_clock_source_internal())
        return;
    if (seq_play_scheduler_transition_all(
            SEQ_PLAY_TRANSITION_SOURCE_SWITCH) == 0U)
        return;

    const uint32_t primask = seq_runtime_enter_critical();
    g_seq_runtime_control.clock_src = src;
    seq_clock_bridge_set_source(&g_seq_clock_bridge, &g_seq_runtime, src);
    if (seq_clock_bridge_is_external_source(src) == 0U)
    {
        g_seq_internal_time_tick = 0U;
    }
    seq_runtime_exec_set_external_step_pulses_pending(0U);
    seq_runtime_update_samples_per_step_from_tempo();

    if (seq_clock_bridge_is_external_source(src) != 0U)
    {
        /* Execution seam: external clock disables audio clock TX and pending step pulses. */
        seq_runtime_exec_set_midi_clock_enabled(0U);
        midi_clock_set_running(false);
        midi_clock_set_mode(MIDI_CLOCK_MODE_SLAVE);
    }
    else
    {
        midi_clock_set_running(false);
        midi_clock_set_mode(MIDI_CLOCK_MODE_MASTER);
        midi_clock_set_bpm_milli(seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge));
        /* Execution seam: rebase audio clock timeline after clock-source policy changes. */
        seq_runtime_exec_rebase_midi_clock(seq_runtime_get_now_sample());
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
    /* Orchestration seam: external MIDI clock updates cadence policy first, then transport gets the step request. */
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
    /* Execution seam: external MIDI clock pulses are converted to pending step work by seq_runtime_exec. */
    seq_runtime_exec_increment_external_step_pulses_pending();
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

    /* Orchestration seam: transport FSM owns CONTINUE; runtime only re-anchors shared execution state. */
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
        g_seq_runtime.step_sample_q16 = (uint64_t)seq_runtime_get_now_sample() << 16;
        /* Boundary advance is driven from the execution block path. */
    }

    if (seq_clock_bridge_is_external_source(source) != 0U)
    {
        seq_runtime_exec_set_midi_clock_enabled(0U);
        midi_clock_set_running(false);
        return;
    }

    midi_clock_set_running(false);
    seq_runtime_send_transport_realtime(0xFBU);
    seq_runtime_exec_set_midi_clock_enabled(1U);
    seq_runtime_exec_rebase_midi_clock(seq_runtime_get_now_sample());
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

uint32_t seq_runtime_get_samples_per_step_q16(void)
{
    return g_seq_runtime.samples_per_step_q16;
}

uint8_t seq_runtime_get_playhead_step(seq_track_id_t track, seq_step_id_t *out_step)
{
    if ((out_step == 0) || (seq_runtime_track_is_valid(track) == 0U))
    {
        return 0U;
    }

    *out_step = g_seq_runtime.play_step[track];
    return 1U;
}

uint8_t seq_runtime_get_track_loop_generation(seq_track_id_t track, uint32_t *out_generation)
{
    if ((out_generation == 0) || (seq_runtime_track_is_valid(track) == 0U))
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
    if (g_seq_runtime.running != 0U)
    {
        /*
         * Do not rebase the phase while transport is running: length is model
         * authority, play_step remains the current musical cursor until the
         * next scheduled pulse wraps it through the new playback window.
         */
        return;
    }

    if (g_seq_runtime.play_step[track] >= length)
    {
        g_seq_runtime.play_step[track] = 0U;
    }
    g_seq_runtime.prev_step[track] = g_seq_runtime.play_step[track];
    g_seq_runtime.prev_step_valid[track] = 0U;
}

void seq_runtime_set_track_div(seq_track_id_t track, uint8_t div)
{
    if (seq_runtime_track_is_valid(track) == 0U)
    {
        return;
    }

    g_seq_runtime_control.track_div[track] = seq_runtime_clamp_track_div(div);
    g_seq_runtime.track_div_phase[track] = 0U;
}

void seq_runtime_restore_track_div(seq_track_id_t track, uint8_t div)
{
    if (seq_runtime_track_is_valid(track) == 0U)
    {
        return;
    }

    g_seq_runtime_control.track_div[track] = seq_runtime_clamp_track_div(div);
}

uint8_t seq_runtime_get_track_div(seq_track_id_t track, uint8_t *out_div)
{
    if ((out_div == NULL) || (seq_runtime_track_is_valid(track) == 0U))
    {
        return 0U;
    }

    *out_div = g_seq_runtime_control.track_div[track];
    return 1U;
}

void seq_runtime_set_track_quant(seq_track_id_t track, uint8_t quant)
{
    if (seq_runtime_track_is_valid(track) == 0U)
    {
        return;
    }

    g_seq_runtime_control.track_quant[track] = seq_runtime_clamp_percent(quant);
}

uint8_t seq_runtime_get_track_quant(seq_track_id_t track, uint8_t *out_quant)
{
    if ((out_quant == NULL) || (seq_runtime_track_is_valid(track) == 0U))
    {
        return 0U;
    }

    *out_quant = g_seq_runtime_control.track_quant[track];
    return 1U;
}

void seq_runtime_set_track_swing(seq_track_id_t track, uint8_t swing)
{
    if (seq_runtime_track_is_valid(track) == 0U)
    {
        return;
    }

    g_seq_runtime_control.track_swing[track] = seq_runtime_clamp_percent(swing);
}

uint8_t seq_runtime_get_track_swing(seq_track_id_t track, uint8_t *out_swing)
{
    if ((out_swing == NULL) || (seq_runtime_track_is_valid(track) == 0U))
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

void seq_runtime_set_rec_start_mode(uint8_t mode)
{
    seq_live_rec_session_set_rec_start_mode(mode);
}

uint8_t seq_runtime_get_rec_start_mode(void)
{
    return seq_live_rec_session_get_rec_start_mode();
}

uint8_t seq_runtime_rec_is_waiting_trigger_start(void)
{
    return seq_live_rec_session_rec_is_waiting_trigger_start();
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
                                         seq_param_slot_t param_slot,
                                         seq_value16_t value16)
{
    return seq_live_rec_session_live_rec_param_write(&g_seq_runtime,
                                                     track,
                                                     set_id,
                                                     param_slot,
                                                     value16);
}

void seq_runtime_set_pattern_rec_target_track(seq_track_id_t track)
{
    seq_live_rec_session_set_pattern_rec_target_track(track);
}

uint8_t seq_runtime_live_rec_param_can_write(seq_track_id_t track,
                                             uint8_t set_id,
                                             seq_param_slot_t param_slot)
{
    return seq_live_rec_session_live_rec_param_can_write(track, set_id, param_slot);
}

static uint8_t seq_runtime_try_held_step_note_capture(seq_live_rec_source_t source,
                                                             uint8_t note,
                                                             uint8_t velocity)
{
    if (source != SEQ_LIVE_REC_SRC_INTERNAL)
    {
        return 0U;
    }

    return seq_edit_capture_held_note_on(note, velocity);
}

void seq_runtime_live_rec_note_on(seq_live_rec_source_t source,
                                  uint8_t channel_zero_based,
                                  uint8_t note,
                                  uint8_t velocity)
{
    seq_runtime_live_rec_note_on_at_sample(source,
                                           channel_zero_based,
                                           note,
                                           velocity,
                                           seq_runtime_get_now_sample());
}

void seq_runtime_live_rec_note_on_at_sample(seq_live_rec_source_t source,
                                            uint8_t channel_zero_based,
                                            uint8_t note,
                                            uint8_t velocity,
                                            uint64_t sample_time)
{
    if (seq_runtime_try_held_step_note_capture(source, note, velocity) != 0U)
    {
        return;
    }

    if (seq_live_rec_session_consume_trigger_start_note_on() != 0U)
    {
        g_seq_runtime_trigger_start_bypass = 1U;
        seq_runtime_start();
    }

    seq_live_rec_session_live_rec_note_on(source,
                                          channel_zero_based,
                                          note,
                                          velocity,
                                          &g_seq_runtime,
                                          sample_time,
                                          0U);
}

void seq_runtime_live_rec_note_off(seq_live_rec_source_t source,
                                   uint8_t channel_zero_based,
                                   uint8_t note)
{
    seq_runtime_live_rec_note_off_at_sample(source,
                                            channel_zero_based,
                                            note,
                                            seq_runtime_get_now_sample());
}

void seq_runtime_live_rec_note_off_at_sample(seq_live_rec_source_t source,
                                             uint8_t channel_zero_based,
                                             uint8_t note,
                                             uint64_t sample_time)
{
    if ((source == SEQ_LIVE_REC_SRC_INTERNAL)
            && (seq_edit_note_capture_note_off(note) != 0U))
    {
        return;
    }

    seq_live_rec_session_live_rec_note_off(source,
                                           channel_zero_based,
                                           note,
                                           &g_seq_runtime,
                                           sample_time,
                                           0U);
}

static void seq_runtime_live_rec_note_on_at_sample_occurrence(seq_live_rec_source_t source,
                                                               uint8_t channel_zero_based,
                                                               uint8_t note,
                                                               uint8_t velocity,
                                                               uint64_t sample_time,
                                                               uint32_t occurrence_id)
{
    if (seq_runtime_try_held_step_note_capture(source, note, velocity) != 0U)
    {
        return;
    }

    if (seq_live_rec_session_consume_trigger_start_note_on() != 0U)
    {
        g_seq_runtime_trigger_start_bypass = 1U;
        seq_runtime_start();
    }

    seq_live_rec_session_live_rec_note_on(source,
                                          channel_zero_based,
                                          note,
                                          velocity,
                                          &g_seq_runtime,
                                          sample_time,
                                          occurrence_id);
}

static void seq_runtime_live_rec_note_off_at_sample_occurrence(seq_live_rec_source_t source,
                                                                uint8_t channel_zero_based,
                                                                uint8_t note,
                                                                uint64_t sample_time,
                                                                uint32_t occurrence_id)
{
    if ((source == SEQ_LIVE_REC_SRC_INTERNAL)
            && (seq_edit_note_capture_note_off(note) != 0U))
    {
        return;
    }

    seq_live_rec_session_live_rec_note_off(source,
                                           channel_zero_based,
                                           note,
                                           &g_seq_runtime,
                                           sample_time,
                                           occurrence_id);
}
uint8_t seq_runtime_live_rec_submit_effective(seq_live_rec_source_t source,
                                              uint8_t is_note_on,
                                              uint8_t channel_zero_based,
                                              uint8_t note,
                                              uint8_t velocity,
                                              uint64_t effective_sample_time,
                                              uint32_t ingress_serial,
                                              uint32_t occurrence_id)
{
    if ((source > SEQ_LIVE_REC_SRC_EXTERNAL) || (note >= 128U)
        || ((is_note_on != 0U) && (velocity == 0U)))
    {
        return 0U;
    }

    const uint32_t primask = seq_runtime_enter_critical();
    for (uint8_t i = 0U; i < g_seq_runtime_live_rec_count; ++i)
    {
        const uint8_t index = (uint8_t)((g_seq_runtime_live_rec_tail + i)
                                        % SEQ_RUNTIME_LIVE_REC_QUEUE_CAPACITY);
        const seq_runtime_live_rec_event_t *const queued =
            &g_seq_runtime_live_rec_queue[index];
        if ((ingress_serial != 0U)
            && (queued->ingress_serial == ingress_serial)
            && (queued->source == (uint8_t)source)
            && (queued->is_note_on == ((is_note_on != 0U) ? 1U : 0U))
            && (queued->channel == channel_zero_based)
            && (queued->note == note))
        {
            seq_runtime_exit_critical(primask);
            return 1U;
        }
    }

    if (g_seq_runtime_live_rec_count >= SEQ_RUNTIME_LIVE_REC_QUEUE_CAPACITY)
    {
        seq_runtime_exit_critical(primask);
        return 0U;
    }

    seq_runtime_live_rec_event_t *const event =
        &g_seq_runtime_live_rec_queue[g_seq_runtime_live_rec_head];
    *event = (seq_runtime_live_rec_event_t){
        .effective_sample_time = effective_sample_time,
        .ingress_serial = ingress_serial,
        .occurrence_id = occurrence_id,
        .source = (uint8_t)source,
        .is_note_on = (is_note_on != 0U) ? 1U : 0U,
        .channel = channel_zero_based,
        .note = note,
        .velocity = velocity
    };
    g_seq_runtime_live_rec_head = (uint8_t)((g_seq_runtime_live_rec_head + 1U)
                                            % SEQ_RUNTIME_LIVE_REC_QUEUE_CAPACITY);
    ++g_seq_runtime_live_rec_count;
    seq_runtime_exit_critical(primask);
    return 1U;
}

void seq_runtime_live_rec_drain_effective(void)
{
    for (;;)
    {
        seq_runtime_live_rec_event_t event;
        const uint32_t primask = seq_runtime_enter_critical();
        if (g_seq_runtime_live_rec_count == 0U)
        {
            seq_runtime_exit_critical(primask);
            return;
        }
        event = g_seq_runtime_live_rec_queue[g_seq_runtime_live_rec_tail];
        g_seq_runtime_live_rec_tail = (uint8_t)((g_seq_runtime_live_rec_tail + 1U)
                                                 % SEQ_RUNTIME_LIVE_REC_QUEUE_CAPACITY);
        --g_seq_runtime_live_rec_count;
        seq_runtime_exit_critical(primask);

        if (event.is_note_on != 0U)
        {
            seq_runtime_live_rec_note_on_at_sample_occurrence((seq_live_rec_source_t)event.source,
                                                               event.channel,
                                                               event.note,
                                                               event.velocity,
                                                               event.effective_sample_time,
                                                               event.occurrence_id);
        }
        else
        {
            seq_runtime_live_rec_note_off_at_sample_occurrence((seq_live_rec_source_t)event.source,
                                                                event.channel,
                                                                event.note,
                                                                event.effective_sample_time,
                                                                event.occurrence_id);
        }
    }
}
void seq_runtime_on_midi_program_live_change(uint8_t track, float program_value)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    /* Post-commit notification: runtime relays a committed program change to the scheduler. */
    seq_play_scheduler_live_midi_program_changed(track, program_value);
}

void seq_runtime_clear_tracks(const seq_track_id_t *tracks, uint8_t track_count)
{
    seq_play_scheduler_clear_tracks(tracks, track_count);
}

void seq_runtime_begin_track_restore(const seq_track_id_t *tracks, uint8_t track_count)
{
    seq_play_scheduler_suspend_tracks(tracks, track_count);
    for (uint8_t i = 0U; i < track_count; ++i)
    {
        if (tracks[i] >= SEQ_TRACK_COUNT)
        {
            continue;
        }
        seq_boundary_engine_restore_all_active_locks(&g_seq_runtime, tracks[i]);
    }
}

void seq_runtime_end_track_restore(const seq_track_id_t *tracks, uint8_t track_count)
{
    for (uint8_t i = 0U; i < track_count; ++i)
    {
        if (tracks[i] >= SEQ_TRACK_COUNT)
        {
            continue;
        }
        seq_boundary_engine_restore_all_active_locks(&g_seq_runtime, tracks[i]);
    }
    seq_play_scheduler_resume_tracks(tracks, track_count);
}

void seq_runtime_on_track_pattern_change(uint8_t track)
{
    seq_edit_note_capture_reset();
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (g_seq_runtime.running == 0U)
    {
        return;
    }

    /* Post-commit notification: pattern changes are forwarded to the scheduler only when running. */
    seq_play_scheduler_notify_track_pattern_change(track);
}
