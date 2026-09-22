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

#include "Platform/memory_layout.h"
#include "IPC/control_audio_command.h"
#include "IPC/control_audio_fifo_layout.h"
#include "Seq/seq_engine.h"
#include "IPC/control_music_publication.h"
#include "ControlRT/control_rt_publication.h"
#include "App/engine_tasklet.h"
#include "IPC/control_audio_transport.h"
#include "Track/track_runtime.h"
#include "Track/control_music_output.h"
#include "Storage/audio_recorder.h"
#include "Storage/sample_capture.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/project_load_quiesce.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Sampler/multi_sample_loader.h"
#include "Keyboard/keyboard_runtime.h"
#include "midi.h"

#include "Seq/seq_model.h"
#include "Seq/seq_edit.h"
#include "Track/entity_topology.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_transport_owner.h"
#include "Seq/seq_musical_time.h"
#include "Seq/seq_live_rec_session.h"
#include "Seq/seq_transport_fsm.h"
#include "Seq/seq_clock_bridge.h"
#include "Seq/metronome_control.h"
#include "main.h"
#include "Platform/brick_fatal.h"
#include "SD/sd_scheduler_runtime.h"

#define SEQ_RUNTIME_DEFAULT_TEMPO_BPM_MILLI 120000U
#define SEQ_RUNTIME_AUDIO_SAMPLE_RATE 48000U
#define SEQ_RUNTIME_STEPS_PER_QUARTER 4U
#define SEQ_RUNTIME_MIDI_CLOCKS_PER_STEP 6U
#define SEQ_RUNTIME_LIVE_REC_QUEUE_CAPACITY 128U

/* Shared execution state lives in seq_transport_owner. */
#define g_seq_runtime (*seq_transport_owner_state())
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
CONTROL_M4_SRAM2 static seq_runtime_live_rec_event_t
    g_seq_runtime_live_rec_queue[SEQ_RUNTIME_LIVE_REC_QUEUE_CAPACITY];
static volatile uint8_t g_seq_runtime_live_rec_head;
static volatile uint8_t g_seq_runtime_live_rec_tail;
static volatile uint8_t g_seq_runtime_live_rec_count;
static uint8_t g_seq_runtime_trigger_start_bypass;
static void seq_runtime_stop_lifecycle_apply(uint8_t emit_transport_stop_and_panic);
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
    seq_transport_owner_set_midi_clock_enabled(1U);
    seq_transport_owner_rebase_midi_clock(seq_runtime_get_now_sample());
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
    (void)control_rt_now_sample(&sample);
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
    const uint64_t stop_sample =
        control_music_output_first_unpublished_sample(
            seq_runtime_get_now_sample());
    sample_capture_control_on_transport_stop(stop_sample);
    seq_edit_note_capture_reset();
    seq_transport_owner_stop_lifecycle_apply(&g_seq_runtime, stop_sample);
    if (emit_transport_stop_and_panic != 0U)
    {
        const uint8_t send_stop = (uint8_t)(
            seq_clock_bridge_is_external_source(
                seq_runtime_get_clock_source_internal()) == 0U);
        (void)control_music_output_panic_all(send_stop);
        seq_ingress_panic();
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
    g_seq_runtime.samples_per_step_q16 = seq_runtime_compute_samples_per_step_q16(
        seq_runtime_get_effective_tempo_bpm_milli());
    seq_runtime_update_midi_clock_period_from_step_period();
}

static void seq_runtime_update_midi_clock_period_from_step_period(void)
{
    uint32_t period_q16 = g_seq_runtime.samples_per_step_q16 / SEQ_RUNTIME_MIDI_CLOCKS_PER_STEP;
    if (period_q16 == 0U)
    {
        period_q16 = 1U;
    }
    seq_transport_owner_set_midi_clock_period_q16(period_q16);
}

void seq_runtime_init(void)
{
    seq_model_init_defaults();
    metronome_control_init();
    seq_param_iface_init();

    /* Orchestration seam: runtime bootstrap delegates execution-state ownership to seq_transport_owner. */
    seq_transport_owner_init();
    memset(g_seq_track_loop_generation, 0, sizeof(g_seq_track_loop_generation));
    /* Default to internal clock at boot; runtime policy may retarget later. */
    g_seq_runtime_control.clock_src = SEQ_CLOCK_SRC_INTERNAL;
    g_seq_internal_time_tick = 0U;
    seq_transport_owner_set_external_step_pulses_pending(0U);
    g_seq_runtime_live_rec_head = 0U;
    g_seq_runtime_live_rec_tail = 0U;
    g_seq_runtime_live_rec_count = 0U;
    memset(g_seq_runtime_live_rec_queue, 0, sizeof(g_seq_runtime_live_rec_queue));
    g_seq_runtime.last_tick_count = seq_runtime_get_now_tick();
    seq_live_rec_session_init();
    seq_transport_fsm_init(&g_seq_transport_fsm);
    seq_clock_bridge_init(&g_seq_clock_bridge,
                          &g_seq_runtime,
                          SEQ_RUNTIME_DEFAULT_TEMPO_BPM_MILLI);
    /* The FIFO may already contain boot projections dated from TIM5.  Seed
     * the execution timeline from that same absolute sample authority so
     * Transport scheduled publication cannot move behind the FIFO floor. */
    uint64_t boot_sample = 0U;
    if (control_rt_now_sample(&boot_sample) == 0U)
    {
        Error_Handler();
        return;
    }
    seq_transport_owner_reset_sample_timeline(boot_sample);
    g_seq_runtime.step_sample_q16 = 0U;
    seq_transport_owner_set_midi_clock_enabled(0U);
    seq_transport_owner_set_midi_clock_period_q16(1U);
    seq_runtime_update_samples_per_step_from_tempo();
    control_audio_transport_init();
    control_audio_transport_publish_changes();
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
    if (project_replacement_is_active() != 0U) return;
    if ((sampler_ram_pool_load_async_busy() != 0U)
        || (wavetable_pool_load_async_busy() != 0U)
        || (multi_sample_load_has_pending() != 0U)) return;
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
    seq_transport_owner_prepare_start_lifecycle(&g_seq_runtime,
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
        const uint64_t start_sample =
            control_music_output_first_unpublished_sample(
                seq_runtime_get_now_sample());
        seq_transport_owner_begin_running_at_sample_q16(&g_seq_runtime,
                                                     &g_seq_transport_fsm,
                                                     &g_seq_clock_bridge,
                                                     seq_runtime_get_now_tick(),
                                                     start_sample << 16);
    }
    control_audio_transport_publish_changes();
    seq_runtime_exit_critical(primask);

    if (begin_running_now != 0U)
    {
        seq_runtime_send_transport_start();
    }
    seq_engine_control_mark_dirty();
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
        pattern_live_on_transport_stopped();
        seq_runtime_stop_lifecycle_apply(emit_transport_stop_and_panic);
    }
    control_audio_transport_publish_changes();
    seq_engine_control_mark_dirty();
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



void seq_runtime_time_adapter_process(void)
{
    /* CONTROL advances autonomously. TIM12 owns the internal musical tick;
     * TIM5 owns the common absolute sample projection. */
    /* Musical execution belongs exclusively to the periodic SEQ IRQ. */
}

void seq_runtime_time_adapter_process_internal_from_irq(void)
{
    if (seq_clock_bridge_is_external_source(seq_runtime_get_clock_source_internal()) == 0U)
    {
        g_seq_internal_time_tick++;
    }
}

void seq_runtime_set_clock_source(seq_clock_src_t src)
{
    if ((uint8_t)src >= (uint8_t)SEQ_CLOCK_SRC_COUNT)
        return;
    if (src == seq_runtime_get_clock_source_internal())
        return;
    const uint32_t primask = seq_runtime_enter_critical();
    g_seq_runtime_control.clock_src = src;
    seq_clock_bridge_set_source(&g_seq_clock_bridge, &g_seq_runtime, src);
    if (seq_clock_bridge_is_external_source(src) == 0U)
    {
        g_seq_internal_time_tick = 0U;
    }
    seq_transport_owner_set_external_step_pulses_pending(0U);
    seq_runtime_update_samples_per_step_from_tempo();
    control_audio_transport_publish_changes();

    if (seq_clock_bridge_is_external_source(src) != 0U)
    {
        /* Execution seam: external clock disables audio clock TX and pending step pulses. */
        seq_transport_owner_set_midi_clock_enabled(0U);
        midi_clock_set_running(false);
        midi_clock_set_mode(MIDI_CLOCK_MODE_SLAVE);
    }
    else
    {
        midi_clock_set_running(false);
        midi_clock_set_mode(MIDI_CLOCK_MODE_MASTER);
        midi_clock_set_bpm_milli(seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge));
        /* Execution seam: rebase audio clock timeline after clock-source policy changes. */
        seq_transport_owner_rebase_midi_clock(seq_runtime_get_now_sample());
    }
    seq_runtime_exit_critical(primask);
    seq_engine_control_mark_dirty();
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
    const uint32_t previous_effective_tempo =
        seq_runtime_get_effective_tempo_bpm_milli();
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

    if (seq_runtime_get_effective_tempo_bpm_milli()
            != previous_effective_tempo)
    {
        seq_runtime_update_samples_per_step_from_tempo();
        control_audio_transport_publish_changes();
    }

    if (step_pulse == 0U)
    {
        return;
    }

    seq_runtime_update_samples_per_step_from_tempo();
    const uint32_t primask = seq_runtime_enter_critical();
    /* Execution seam: external MIDI clock pulses are converted to pending step work by seq_transport_owner. */
    seq_transport_owner_increment_external_step_pulses_pending();
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

    const uint64_t transition_sample =
        control_music_output_first_unpublished_sample(
            seq_runtime_get_now_sample());
    g_seq_runtime.running = 1U;
    g_seq_runtime.tick_accum = 0U;
    g_seq_runtime.ext_clock_tick_accum = 0U;
    seq_transport_owner_set_external_step_pulses_pending(0U);
    g_seq_runtime.last_tick_count = seq_runtime_get_now_tick();
    if (was_stopped != 0U)
    {
        /*
         * CONTINUE after STOP must re-anchor the musical timeline to the
         * absolute audio sample timeline, exactly like START path does.
         * Without this rebase, step_sample_q16 can remain at 0 while
         * audio_timeline_sample is monotonic, causing boundary misalignment.
         */
        g_seq_runtime.step_sample_q16 = transition_sample << 16;
        /* Boundary advance is driven from the execution block path. */
    }
    seq_transport_owner_enqueue_transport_start(transition_sample);
    seq_engine_control_mark_dirty();

    if (seq_clock_bridge_is_external_source(source) != 0U)
    {
        seq_transport_owner_set_midi_clock_enabled(0U);
        midi_clock_set_running(false);
        return;
    }

    midi_clock_set_running(false);
    seq_runtime_send_transport_realtime(0xFBU);
    seq_transport_owner_set_midi_clock_enabled(1U);
    seq_transport_owner_rebase_midi_clock(seq_runtime_get_now_sample());
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
        seq_engine_control_mark_dirty();
    }
    return 1U;
}

uint32_t seq_runtime_get_samples_per_step_q16(void)
{
    return g_seq_runtime.samples_per_step_q16;
}

void seq_runtime_capture_shadow_seed(seq_runtime_shadow_seed_t *out_seed)
{
    if (out_seed == NULL) return;
    const uint32_t primask = seq_runtime_enter_critical();
    out_seed->running = g_seq_runtime.running;
    out_seed->step_sample_q16 = g_seq_runtime.step_sample_q16;
    out_seed->samples_per_step_q16 = g_seq_runtime.samples_per_step_q16;
    for (seq_track_id_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        out_seed->play_step[track] = g_seq_runtime.play_step[track];
        out_seed->track_div_phase[track] = g_seq_runtime.track_div_phase[track];
        out_seed->track_swing_phase[track] = g_seq_runtime.track_swing_phase[track];
    }
    seq_runtime_exit_critical(primask);
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

static uint64_t seq_runtime_sample_delta_to_step_q16(uint64_t samples,
                                                      uint32_t step_q16)
{
    if (step_q16 == 0U) return 0U;
    const uint64_t sample_q16 = (samples > (UINT64_MAX >> 16U))
        ? UINT64_MAX : (samples << 16U);
    const uint64_t whole = sample_q16 / step_q16;
    const uint64_t remainder = sample_q16 % step_q16;
    if (whole > (UINT64_MAX >> 16U)) return UINT64_MAX;
    return (whole << 16U) + ((remainder << 16U) / step_q16);
}

uint8_t seq_runtime_get_musical_time(seq_track_id_t track,
                                     uint64_t sample_time,
                                     seq_musical_time_t *out_time)
{
    if ((out_time == NULL) || (seq_runtime_track_is_valid(track) == 0U)
            || (g_seq_runtime.samples_per_step_q16 == 0U))
        return 0U;

    const uint64_t anchor_sample = g_seq_runtime.step_sample_q16 >> 16U;
    const uint8_t forward = (sample_time >= anchor_sample) ? 1U : 0U;
    const uint64_t sample_delta = forward
        ? sample_time - anchor_sample : anchor_sample - sample_time;
    const uint64_t transport_delta = seq_runtime_sample_delta_to_step_q16(
        sample_delta, g_seq_runtime.samples_per_step_q16);
    const uint64_t anchor_transport =
        (uint64_t)seq_transport_owner_get_transport_step() << 16U;
    const uint64_t transport_position = forward
        ? ((UINT64_MAX - anchor_transport < transport_delta)
            ? UINT64_MAX : anchor_transport + transport_delta)
        : ((anchor_transport > transport_delta)
            ? anchor_transport - transport_delta : 0U);

    const uint8_t track_div = seq_runtime_clamp_track_div(
        g_seq_runtime_control.track_div[track]);
    const int64_t track_delta = (int64_t)(transport_delta / track_div);
    int64_t pattern_unwrapped = (int64_t)(
        (uint64_t)g_seq_runtime.play_step[track] << 16U);
    pattern_unwrapped += (forward != 0U) ? track_delta : -track_delta;
    const uint8_t length = seq_model_get_track_playback_length(track);
    const int64_t modulus = (int64_t)((uint64_t)length << 16U);
    int64_t loop_delta = pattern_unwrapped / modulus;
    int64_t pattern_position = pattern_unwrapped % modulus;
    if (pattern_position < 0)
    {
        pattern_position += modulus;
        --loop_delta;
    }
    int64_t epoch = (int64_t)g_seq_track_loop_generation[track] + loop_delta;
    if (epoch < 0) epoch = 0;
    if (epoch > UINT32_MAX) epoch = UINT32_MAX;

    *out_time = (seq_musical_time_t)
    {
        .sample_time = sample_time,
        .transport_position_q16 = transport_position,
        .pattern_position_q16 = (uint32_t)pattern_position,
        .loop_epoch = (uint32_t)epoch,
        .samples_per_step_q16 = g_seq_runtime.samples_per_step_q16,
        .track_div = track_div,
        .running = g_seq_runtime.running,
        .reserved = { 0U, 0U },
    };
    return 1U;
}

uint8_t seq_runtime_get_track_next_loop_sample(seq_track_id_t track,
                                               uint64_t *out_sample)
{
    if ((out_sample == NULL) || (seq_runtime_track_is_valid(track) == 0U)
            || (g_seq_runtime.running == 0U)
            || (g_seq_runtime.samples_per_step_q16 == 0U))
        return 0U;
    const uint8_t div = seq_runtime_clamp_track_div(
        g_seq_runtime_control.track_div[track]);
    uint8_t length = seq_model_get_track_playback_length(track);
    if (length == 0U) length = 1U;
    const uint8_t step = (g_seq_runtime.play_step[track] < length)
        ? g_seq_runtime.play_step[track] : 0U;
    const uint32_t advances = (uint32_t)length - step;
    const uint32_t first_pulses = (uint32_t)div
        - g_seq_runtime.track_div_phase[track];
    const uint32_t pulses = first_pulses
        + ((advances - 1U) * (uint32_t)div);
    if (seq_clock_bridge_is_external_source(
            seq_runtime_get_clock_source_internal()) != 0U)
    {
        if (seq_transport_owner_external_step_pulses_pending() < pulses)
            return 0U;
        *out_sample = control_music_output_first_unpublished_sample(
            seq_runtime_get_now_sample());
        return 1U;
    }
    const uint64_t boundary_q16 = g_seq_runtime.step_sample_q16
        + ((uint64_t)pulses * g_seq_runtime.samples_per_step_q16);
    *out_sample = boundary_q16 >> 16;
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

void seq_runtime_on_step_play_changed(seq_track_id_t track,
                                      seq_step_id_t step,
                                      uint8_t voice,
                                      seq_step_play_field_t field)
{
    (void)track;(void)step;(void)voice;(void)field;
    seq_engine_control_mark_dirty();
}

void seq_runtime_on_step_play_removed(seq_track_id_t track,
                                      seq_step_id_t step,
                                      int16_t voice)
{
    (void)track;(void)step;(void)voice;
    seq_engine_control_mark_dirty();
}

void seq_runtime_on_step_roll_changed(seq_track_id_t track,
                                      seq_step_id_t step)
{
    (void)track;(void)step;
    seq_engine_control_mark_dirty();
}

void seq_runtime_set_track_div(seq_track_id_t track, uint8_t div)
{
    if (seq_runtime_track_is_valid(track) == 0U)
    {
        return;
    }

    g_seq_runtime_control.track_div[track] = seq_runtime_clamp_track_div(div);
    g_seq_runtime.track_div_phase[track] = 0U;
    seq_engine_control_mark_dirty();
}

void seq_runtime_restore_track_div(seq_track_id_t track, uint8_t div)
{
    if (seq_runtime_track_is_valid(track) == 0U)
    {
        return;
    }

    g_seq_runtime_control.track_div[track] = seq_runtime_clamp_track_div(div);
    seq_engine_control_mark_dirty();
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
    seq_engine_control_mark_dirty();
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
    seq_engine_control_mark_dirty();
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

uint8_t seq_runtime_rec_toggle_arm(seq_track_id_t target_track)
{
    const uint8_t pending_before = seq_live_rec_session_rec_is_pattern_pending_start();
    const uint8_t armed_before = seq_live_rec_session_rec_is_armed();
    seq_live_rec_session_toggle_arm(seq_runtime_get_now_sample(), g_seq_runtime.samples_per_step_q16);
    (void)target_track;
    sample_capture_control_on_global_rec_arm(
        seq_live_rec_session_rec_is_armed());

    if ((armed_before != 0U)
        && (pending_before == 0U)
        && (seq_live_rec_session_rec_is_armed() == 0U))
    {
        seq_transport_fsm_abort_pending(&g_seq_transport_fsm);
        g_seq_runtime.running = (seq_transport_fsm_is_running(&g_seq_transport_fsm) != 0U) ? 1U : 0U;
    }
    return 1U;
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

uint32_t seq_runtime_get_effective_tempo_bpm_milli(void)
{
    if ((seq_clock_bridge_is_external_source(
                seq_runtime_get_clock_source_internal()) != 0U)
            && (seq_clock_bridge_is_external_tempo_valid(
                    &g_seq_clock_bridge) != 0U))
        return seq_clock_bridge_get_external_tempo_bpm_milli(
            &g_seq_clock_bridge);
    return seq_clock_bridge_get_internal_tempo_bpm_milli(
        &g_seq_clock_bridge);
}

void seq_runtime_set_tempo_bpm_milli(uint32_t bpm_milli)
{
    seq_clock_bridge_set_internal_tempo(&g_seq_clock_bridge, &g_seq_runtime, bpm_milli);
    seq_runtime_update_samples_per_step_from_tempo();
    control_audio_transport_publish_changes();
    if (seq_clock_bridge_is_external_source(seq_runtime_get_clock_source_internal()) == 0U)
    {
        midi_clock_set_bpm_milli(seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge));
    }
    seq_engine_control_mark_dirty();
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
    (void)track;(void)program_value;
}

void seq_runtime_clear_tracks(const seq_track_id_t *tracks, uint8_t track_count)
{
    (void)tracks;(void)track_count;seq_ingress_discard();
}

void seq_runtime_begin_track_restore(const seq_track_id_t *tracks, uint8_t track_count)
{
    const uint64_t effective_sample =
        control_music_output_first_unpublished_sample(
            seq_runtime_get_now_sample());
    (void)effective_sample;seq_ingress_discard();
    for (uint8_t i = 0U; i < track_count; ++i)
    {
        if (tracks[i] >= SEQ_TRACK_COUNT)
        {
            continue;
        }
        seq_engine_control_disarm_track(tracks[i]);
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
        seq_engine_control_disarm_track(tracks[i]);
    }
    seq_engine_control_mark_dirty();
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
    seq_engine_control_mark_dirty();
}
