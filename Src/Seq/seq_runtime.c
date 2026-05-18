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
#include "usart.h"

#include <stdio.h>

#define SEQ_RUNTIME_DEFAULT_TEMPO_BPM_MILLI 120000U
#define SEQ_RUNTIME_AUDIO_SAMPLE_RATE 48000U
#define SEQ_RUNTIME_STEPS_PER_QUARTER 4U
#define SEQ_RUNTIME_MIDI_CLOCKS_PER_STEP 6U
#define SEQ_RELOOP_DEBUG_RING_CAP 128U
#define SEQ_RELOOP_DEBUG_FLUSH_LIMIT 8U

/* Shared execution state lives in seq_runtime_exec. */
#define g_seq_runtime (*seq_runtime_exec_state())
typedef enum
{
    SEQ_RELOOP_DEBUG_KIND_STEP = 1,
    SEQ_RELOOP_DEBUG_KIND_LEN,
    SEQ_RELOOP_DEBUG_KIND_EVT
} seq_reloop_debug_kind_t;

typedef struct
{
    uint8_t kind;
    uint8_t phase;
    uint8_t type;
    uint8_t track;
    uint8_t previous_step;
    uint8_t current_step;
    uint8_t next_step;
    uint8_t effective_length;
    uint8_t model_length;
    uint8_t old_length;
    uint8_t new_length;
    uint8_t did_wrap;
    uint8_t boundary_hit;
    uint8_t running;
    uint8_t start_pending;
    uint8_t clock_src;
    uint16_t block_offset;
    uint16_t block_frames;
    uint16_t event_count;
    uint16_t reserved;
    uint32_t loop_generation;
    uint32_t samples_per_step_q16;
    uint32_t dropped;
    uint64_t audio_sample;
} seq_reloop_debug_entry_t;
SEQ_STATE_D2 static struct
{
    seq_clock_src_t clock_src;
    uint8_t track_div[SEQ_TRACK_COUNT];
    uint8_t track_quant[SEQ_TRACK_COUNT];
    uint8_t track_swing[SEQ_TRACK_COUNT];
} g_seq_runtime_control;
SEQ_STATE_D2 static seq_reloop_debug_entry_t g_seq_reloop_debug_ring[SEQ_RELOOP_DEBUG_RING_CAP];
static volatile uint16_t g_seq_reloop_debug_head;
static volatile uint16_t g_seq_reloop_debug_tail;
static volatile uint32_t g_seq_reloop_debug_dropped;
static uint32_t g_seq_reloop_debug_dropped_reported;
static uint8_t g_seq_reloop_debug_length_shadow[SEQ_TRACK_COUNT];
static volatile uint32_t g_seq_internal_time_tick;
SEQ_STATE_D2 static uint32_t g_seq_track_loop_generation[SEQ_TRACK_COUNT];
SEQ_STATE_D2 static seq_runtime_diag_t g_seq_runtime_diag;
SEQ_STATE_D2 static seq_transport_fsm_t g_seq_transport_fsm;
SEQ_STATE_D2 static seq_clock_bridge_t g_seq_clock_bridge;
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
static uint8_t seq_runtime_reloop_debug_should_log_step(uint8_t previous_step,
                                                        uint8_t current_step,
                                                        uint8_t length,
                                                        uint8_t did_wrap);
static void seq_runtime_reloop_debug_push(const seq_reloop_debug_entry_t *entry);
static uint8_t seq_runtime_reloop_debug_pop(seq_reloop_debug_entry_t *out_entry);
static uint32_t seq_runtime_reloop_debug_samples_q16_to_samples(uint32_t samples_per_step_q16);
static void seq_runtime_reloop_debug_u64_to_dec(uint64_t value, char *out, uint32_t out_len);
static void seq_runtime_copy_audio_event(seq_play_scheduler_audio_event_t *scheduler_event,
                                         const seq_runtime_audio_event_t *event);

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

static uint32_t seq_runtime_reloop_debug_samples_q16_to_samples(uint32_t samples_per_step_q16)
{
    return (samples_per_step_q16 + 0x8000UL) >> 16;
}

static void seq_runtime_reloop_debug_u64_to_dec(uint64_t value, char *out, uint32_t out_len)
{
    char tmp[21];
    uint32_t count = 0U;

    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    if (value == 0U)
    {
        if (out_len > 1U)
        {
            out[0] = '0';
            out[1] = '\0';
        }
        else
        {
            out[0] = '\0';
        }
        return;
    }

    while ((value != 0U) && (count < (uint32_t)sizeof(tmp)))
    {
        tmp[count++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    }

    uint32_t dst = 0U;
    while ((count > 0U) && ((dst + 1U) < out_len))
    {
        out[dst++] = tmp[--count];
    }
    out[dst] = '\0';
}

static uint8_t seq_runtime_reloop_debug_should_log_step(uint8_t previous_step,
                                                        uint8_t current_step,
                                                        uint8_t length,
                                                        uint8_t did_wrap)
{
    if (length == 0U)
    {
        return 0U;
    }
    if (did_wrap != 0U)
    {
        return 1U;
    }
    if ((length >= 2U) && (previous_step >= (uint8_t)(length - 2U)))
    {
        return 1U;
    }
    return (current_step <= 1U) ? 1U : 0U;
}

static void seq_runtime_reloop_debug_push(const seq_reloop_debug_entry_t *entry)
{
#if SEQ_RELOOP_UART_DEBUG
    if (entry == NULL)
    {
        return;
    }

    const uint32_t primask = seq_runtime_enter_critical();
    const uint16_t next_head = (uint16_t)((g_seq_reloop_debug_head + 1U) % SEQ_RELOOP_DEBUG_RING_CAP);
    if (next_head == g_seq_reloop_debug_tail)
    {
        g_seq_reloop_debug_dropped++;
        seq_runtime_exit_critical(primask);
        return;
    }

    seq_reloop_debug_entry_t copy = *entry;
    copy.dropped = g_seq_reloop_debug_dropped;
    g_seq_reloop_debug_ring[g_seq_reloop_debug_head] = copy;
    g_seq_reloop_debug_head = next_head;
    seq_runtime_exit_critical(primask);
#else
    (void)entry;
#endif
}

static uint8_t seq_runtime_reloop_debug_pop(seq_reloop_debug_entry_t *out_entry)
{
#if SEQ_RELOOP_UART_DEBUG
    if (out_entry == NULL)
    {
        return 0U;
    }

    const uint32_t primask = seq_runtime_enter_critical();
    if (g_seq_reloop_debug_tail == g_seq_reloop_debug_head)
    {
        seq_runtime_exit_critical(primask);
        return 0U;
    }

    *out_entry = g_seq_reloop_debug_ring[g_seq_reloop_debug_tail];
    g_seq_reloop_debug_tail = (uint16_t)((g_seq_reloop_debug_tail + 1U) % SEQ_RELOOP_DEBUG_RING_CAP);
    seq_runtime_exit_critical(primask);
    return 1U;
#else
    (void)out_entry;
    return 0U;
#endif
}

void seq_runtime_reloop_debug_log_step(seq_track_id_t track,
                                        seq_step_id_t previous_step,
                                        seq_step_id_t current_step,
                                        uint8_t effective_length,
                                        uint8_t model_length,
                                        uint8_t did_wrap,
                                        uint8_t boundary_hit,
                                        uint32_t loop_generation,
                                        uint64_t audio_sample,
                                        uint16_t block_offset,
                                        uint16_t block_frames,
                                        uint32_t samples_per_step_q16,
                                        uint8_t running,
                                        uint8_t start_pending,
                                        seq_clock_src_t clock_src)
{
#if SEQ_RELOOP_UART_DEBUG
    if ((track >= SEQ_TRACK_COUNT)
        || (seq_runtime_reloop_debug_should_log_step(previous_step, current_step, effective_length, did_wrap) == 0U))
    {
        return;
    }

    uint8_t next_step = (uint8_t)(current_step + 1U);
    if ((effective_length == 0U) || (next_step >= effective_length))
    {
        next_step = 0U;
    }

    seq_reloop_debug_entry_t entry = {0};
    entry.kind = (uint8_t)SEQ_RELOOP_DEBUG_KIND_STEP;
    entry.track = track;
    entry.previous_step = previous_step;
    entry.current_step = current_step;
    entry.next_step = next_step;
    entry.effective_length = effective_length;
    entry.model_length = model_length;
    entry.did_wrap = (did_wrap != 0U) ? 1U : 0U;
    entry.boundary_hit = (boundary_hit != 0U) ? 1U : 0U;
    entry.loop_generation = loop_generation;
    entry.audio_sample = audio_sample;
    if ((block_frames != 0U) && (block_offset >= block_frames))
    {
        g_seq_reloop_debug_dropped++;
        return;
    }
    entry.block_offset = block_offset;
    entry.block_frames = block_frames;
    entry.samples_per_step_q16 = samples_per_step_q16;
    entry.running = running;
    entry.start_pending = start_pending;
    entry.clock_src = (uint8_t)clock_src;
    seq_runtime_reloop_debug_push(&entry);
#else
    (void)track; (void)previous_step; (void)current_step; (void)effective_length;
    (void)model_length; (void)did_wrap; (void)boundary_hit; (void)loop_generation;
    (void)audio_sample; (void)block_offset; (void)block_frames; (void)samples_per_step_q16; (void)running;
    (void)start_pending; (void)clock_src;
#endif
}

void seq_runtime_reloop_debug_log_length(seq_track_id_t track)
{
#if SEQ_RELOOP_UART_DEBUG
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    const uint8_t new_length = seq_model_get_track_playback_length(track);
    const uint8_t old_length = g_seq_reloop_debug_length_shadow[track];
    if (old_length == new_length)
    {
        return;
    }
    g_seq_reloop_debug_length_shadow[track] = new_length;

    seq_reloop_debug_entry_t entry = {0};
    entry.kind = (uint8_t)SEQ_RELOOP_DEBUG_KIND_LEN;
    entry.track = track;
    entry.old_length = old_length;
    entry.new_length = new_length;
    entry.current_step = g_seq_runtime.play_step[track];
    entry.effective_length = new_length;
    entry.running = g_seq_runtime.running;
    entry.start_pending = seq_transport_fsm_is_start_pending(&g_seq_transport_fsm);
    entry.clock_src = (uint8_t)seq_runtime_get_clock_source_internal();
    entry.loop_generation = g_seq_track_loop_generation[track];
    seq_runtime_reloop_debug_push(&entry);
#else
    (void)track;
#endif
}

void seq_runtime_reloop_debug_log_audio_event(uint8_t phase,
                                              const seq_runtime_audio_event_t *event,
                                              uint16_t block_frames,
                                              uint16_t event_count,
                                              uint64_t block_start_sample)
{
#if SEQ_RELOOP_UART_DEBUG
    if (event == NULL)
    {
        return;
    }

    seq_reloop_debug_entry_t entry = {0};
    entry.kind = (uint8_t)SEQ_RELOOP_DEBUG_KIND_EVT;
    entry.phase = phase;
    entry.type = event->type;
    entry.track = event->track;
    if ((block_frames != 0U) && (event->sample_offset_in_block >= block_frames))
    {
        g_seq_reloop_debug_dropped++;
        return;
    }
    entry.block_offset = event->sample_offset_in_block;
    entry.block_frames = block_frames;
    entry.event_count = event_count;
    entry.audio_sample = block_start_sample + (uint64_t)event->sample_offset_in_block;
    seq_runtime_reloop_debug_push(&entry);
#else
    (void)phase; (void)event; (void)block_frames; (void)event_count; (void)block_start_sample;
#endif
}

void seq_runtime_reloop_debug_flush(void)
{
#if SEQ_RELOOP_UART_DEBUG
    if (huart1.Instance == 0)
    {
        return;
    }

    uint8_t emitted = 0U;
    if (g_seq_reloop_debug_dropped_reported != g_seq_reloop_debug_dropped)
    {
        char drop_line[48];
        const int n = snprintf(drop_line,
                               sizeof(drop_line),
                               "SEQDROP count=%lu\n",
                               (unsigned long)g_seq_reloop_debug_dropped);
        if (n > 0)
        {
            const uint16_t tx_len = (n >= (int)sizeof(drop_line)) ? (uint16_t)(sizeof(drop_line) - 1U) : (uint16_t)n;
            (void)HAL_UART_Transmit(&huart1, (uint8_t *)drop_line, tx_len, 20U);
        }
        g_seq_reloop_debug_dropped_reported = g_seq_reloop_debug_dropped;
        emitted++;
    }

    while (emitted < SEQ_RELOOP_DEBUG_FLUSH_LIMIT)
    {
        seq_reloop_debug_entry_t entry;
        if (seq_runtime_reloop_debug_pop(&entry) == 0U)
        {
            break;
        }

        char line[192];
        char abs_buf[24];
        seq_runtime_reloop_debug_u64_to_dec(entry.audio_sample, abs_buf, sizeof(abs_buf));
        int n = 0;
        if (entry.kind == (uint8_t)SEQ_RELOOP_DEBUG_KIND_STEP)
        {
            n = snprintf(line,
                         sizeof(line),
                         "SEQDBG tr=%u prev=%u cur=%u next=%u len=%u model=%u wrap=%u hit=%u gen=%lu abs=%s off=%u sps=%lu run=%u pend=%u clk=%u drop=%lu\n",
                         (unsigned)entry.track,
                         (unsigned)entry.previous_step,
                         (unsigned)entry.current_step,
                         (unsigned)entry.next_step,
                         (unsigned)entry.effective_length,
                         (unsigned)entry.model_length,
                         (unsigned)entry.did_wrap,
                         (unsigned)entry.boundary_hit,
                         (unsigned long)entry.loop_generation,
                         abs_buf,
                         (unsigned)entry.block_offset,
                         (unsigned long)seq_runtime_reloop_debug_samples_q16_to_samples(entry.samples_per_step_q16),
                         (unsigned)entry.running,
                         (unsigned)entry.start_pending,
                         (unsigned)entry.clock_src,
                         (unsigned long)entry.dropped);
        }
        else if (entry.kind == (uint8_t)SEQ_RELOOP_DEBUG_KIND_LEN)
        {
            n = snprintf(line,
                         sizeof(line),
                         "SEQ_LEN tr=%u old=%u new=%u cur=%u eff=%u run=%u pend=%u gen=%lu clk=%u drop=%lu\n",
                         (unsigned)entry.track,
                         (unsigned)entry.old_length,
                         (unsigned)entry.new_length,
                         (unsigned)entry.current_step,
                         (unsigned)entry.effective_length,
                         (unsigned)entry.running,
                         (unsigned)entry.start_pending,
                         (unsigned long)entry.loop_generation,
                         (unsigned)entry.clock_src,
                         (unsigned long)entry.dropped);
        }
        else if (entry.kind == (uint8_t)SEQ_RELOOP_DEBUG_KIND_EVT)
        {
            n = snprintf(line,
                         sizeof(line),
                         "SEQEVT ph=%c typ=%u tr=%u off=%u frames=%u cnt=%u abs=%s drop=%lu\n",
                         (entry.phase == 0U) ? 'C' : 'A',
                         (unsigned)entry.type,
                         (unsigned)entry.track,
                         (unsigned)entry.block_offset,
                         (unsigned)entry.block_frames,
                         (unsigned)entry.event_count,
                         abs_buf,
                         (unsigned long)entry.dropped);
        }

        if (n > 0)
        {
            const uint16_t tx_len = (n >= (int)sizeof(line)) ? (uint16_t)(sizeof(line) - 1U) : (uint16_t)n;
            (void)HAL_UART_Transmit(&huart1, (uint8_t *)line, tx_len, 20U);
        }
        emitted++;
    }
#endif
}
static void seq_runtime_copy_audio_event(seq_play_scheduler_audio_event_t *scheduler_event,
                                         const seq_runtime_audio_event_t *event)
{
    if ((scheduler_event == NULL) || (event == NULL))
    {
        return;
    }

    scheduler_event->type = event->type;
    scheduler_event->track = event->track;
    scheduler_event->note = event->note;
    scheduler_event->velocity = event->velocity;
    scheduler_event->sample_offset_in_block = event->sample_offset_in_block;
    scheduler_event->event_token = event->event_token;
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

    /* Orchestration seam: runtime bootstrap delegates execution-state ownership to seq_runtime_exec. */
    seq_runtime_exec_init();
    memset(g_seq_track_loop_generation, 0, sizeof(g_seq_track_loop_generation));
    memset(g_seq_reloop_debug_ring, 0, sizeof(g_seq_reloop_debug_ring));
    g_seq_reloop_debug_head = 0U;
    g_seq_reloop_debug_tail = 0U;
    g_seq_reloop_debug_dropped = 0U;
    g_seq_reloop_debug_dropped_reported = 0U;
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        g_seq_reloop_debug_length_shadow[track] = seq_model_get_track_playback_length(track);
    }
    /* Default to internal clock at boot; runtime policy may retarget later. */
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

    /* Orchestration seam: runtime asks clock policy to prepare cadence, then asks transport FSM for START. */
    seq_runtime_exec_prepare_start_lifecycle(&g_seq_runtime,
                                             &g_seq_clock_bridge,
                                             seq_runtime_get_now_tick());
    seq_runtime_update_samples_per_step_from_tempo();

    /* Orchestration seam: transport FSM owns the start transition and count-in state. */
    if (seq_transport_fsm_request_start(&g_seq_transport_fsm,
                                        seq_live_rec_session_rec_is_armed(),
                                        seq_live_rec_session_get_rec_count_in_mode()) == 0U)
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
                                                     (uint64_t)seq_runtime_exec_get_audio_timeline_sample() << 16);
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
    seq_runtime_reloop_debug_flush();
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

    /* Audio-block seam: collect via execution owner, then hand events to audio. */
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
    /* Audio apply seam: runtime forwards collected events to scheduler/engines only. */
    seq_play_scheduler_audio_event_t scheduler_event;
    seq_runtime_copy_audio_event(&scheduler_event, event);
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
        /* Execution seam: external clock disables audio clock TX and pending step pulses. */
        seq_runtime_exec_set_midi_clock_audio_enabled(0U);
        midi_clock_set_running(false);
        midi_clock_set_mode(MIDI_CLOCK_MODE_SLAVE);
    }
    else
    {
        midi_clock_set_running(false);
        midi_clock_set_mode(MIDI_CLOCK_MODE_MASTER);
        midi_clock_set_bpm_milli(seq_clock_bridge_get_internal_tempo_bpm_milli(&g_seq_clock_bridge));
        /* Execution seam: rebase audio clock timeline after clock-source policy changes. */
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

uint32_t seq_runtime_get_samples_per_step_q16(void)
{
    return g_seq_runtime.samples_per_step_q16;
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

    seq_runtime_reloop_debug_log_length(track);

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
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_seq_runtime_control.track_div[track] = seq_runtime_clamp_track_div(div);
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

    g_seq_runtime_control.track_quant[track] = seq_runtime_clamp_percent(quant);
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

    g_seq_runtime_control.track_swing[track] = seq_runtime_clamp_percent(swing);
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

    /* Post-commit notification: runtime relays a committed program change to the scheduler. */
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

    /* Post-commit notification: pattern changes are forwarded to the scheduler only when running. */
    seq_play_scheduler_notify_track_pattern_change(track);
}


