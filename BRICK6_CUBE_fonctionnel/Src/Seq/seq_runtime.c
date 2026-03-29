#include "Seq/seq_runtime.h"

#include <string.h>
#include <stdio.h>

#include "Storage/memory_layout.h"
#include "Core/engine_tasklet.h"
#include "Core/track_runtime.h"
#include "midi.h"
#include "ui_core.h"

#include "Seq/seq_model.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_play_scheduler.h"
#include "Seq/seq_output_guard.h"
#include "main.h"

#define SEQ_RUNTIME_MIDI_CLOCKS_PER_STEP 6U
#define SEQ_RUNTIME_STEPS_PER_QUARTER_NOTE 4U
#define SEQ_RUNTIME_PLAY_VOICE_COUNT 4U
#define SEQ_RUNTIME_ENGINE_TICK_HZ 1500U
#define SEQ_RUNTIME_LIVE_REC_PENDING_CAP 64U
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

typedef struct
{
    uint8_t set_id;
    seq_param8_t param8;
    seq_value16_t value16;
    seq_value16_t base_value16;
} seq_runtime_step_lock_t;

typedef struct
{
    uint8_t active;
    uint8_t track;
    uint8_t channel;
    uint8_t note;
    uint8_t source;
    uint8_t voice;
    uint8_t step;
    uint32_t start_tick;
} seq_live_rec_pending_note_t;

SEQ_STATE_D2 static seq_runtime_state_t g_seq_runtime;
SEQ_STATE_D2 static uint32_t g_seq_midi_clock_tick_accum;
SEQ_STATE_D2 static seq_live_rec_pending_note_t g_seq_live_rec_pending[SEQ_RUNTIME_LIVE_REC_PENDING_CAP];
SEQ_STATE_D2 static uint8_t g_seq_rec_armed;
SEQ_STATE_D2 static uint8_t g_seq_rec_count_in_mode;
SEQ_STATE_D2 static uint32_t g_seq_rec_count_in_remaining_steps;
SEQ_STATE_D2 static uint8_t g_seq_start_pending;
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

static uint32_t seq_runtime_rec_count_in_steps_from_mode(uint8_t mode)
{
    switch (mode)
    {
        case 1U:
            return 4U;
        case 2U:
            return 8U;
        case 3U:
            return 16U;
        default:
            return 0U;
    }
}

static param_id_t seq_runtime_play_param_note(uint8_t voice);
static param_id_t seq_runtime_play_param_vel(uint8_t voice);
static param_id_t seq_runtime_play_param_len(uint8_t voice);
static param_id_t seq_runtime_play_param_mictim(uint8_t voice);
static void seq_runtime_restore_all_active_locks(seq_track_id_t track);
static uint8_t seq_runtime_clock_source_is_external(seq_clock_src_t src);
static void seq_runtime_external_tempo_reset(void);
static void seq_runtime_internal_step_period_recompute(void);
static uint32_t seq_runtime_internal_step_next_ticks(void);

static void seq_runtime_live_rec_pending_clear(void)
{
    memset(g_seq_live_rec_pending, 0, sizeof(g_seq_live_rec_pending));
}

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
    if (g_seq_runtime.running != 0U)
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
        seq_runtime_restore_all_active_locks(track);
    }

    g_seq_midi_clock_tick_accum = 0U;
    g_seq_start_pending = 0U;
    seq_runtime_send_transport_start();
}

static int32_t seq_runtime_live_rec_find_voice_with_note_lock(seq_track_id_t track,
                                                              seq_step_id_t step,
                                                              uint8_t note)
{
    for (uint8_t voice = 0U; voice < SEQ_RUNTIME_PLAY_VOICE_COUNT; ++voice)
    {
        uint8_t set_id = 0U;
        seq_param8_t param8 = 0U;
        const param_id_t note_id = seq_runtime_play_param_note(voice);
        if (seq_param_iface_map_param(note_id, &set_id, &param8) == 0U)
        {
            continue;
        }

        seq_plock_entry_t entry;
        if (seq_model_step_plock_find(track, step, set_id, param8, &entry) == 0U)
        {
            continue;
        }

        const float note_f = seq_param_iface_decode_param_value(note_id, entry.value16);
        const uint8_t existing_note = (uint8_t)(note_f + 0.5f);
        if (existing_note == note)
        {
            return (int32_t)voice;
        }
    }

    return -1;
}

static uint8_t seq_runtime_live_rec_voice_has_any_lock(seq_track_id_t track,
                                                       seq_step_id_t step,
                                                       uint8_t voice)
{
    const param_id_t note_id = seq_runtime_play_param_note(voice);
    const param_id_t vel_id = seq_runtime_play_param_vel(voice);
    const param_id_t len_id = seq_runtime_play_param_len(voice);
    const param_id_t mictim_id = seq_runtime_play_param_mictim(voice);
    const param_id_t params[4] = { note_id, vel_id, len_id, mictim_id };

    for (uint8_t i = 0U; i < 4U; ++i)
    {
        uint8_t set_id = 0U;
        seq_param8_t param8 = 0U;
        if (seq_param_iface_map_param(params[i], &set_id, &param8) == 0U)
        {
            continue;
        }

        seq_plock_entry_t entry;
        if (seq_model_step_plock_find(track, step, set_id, param8, &entry) != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

static int32_t seq_runtime_live_rec_find_free_voice(seq_track_id_t track,
                                                    seq_step_id_t step)
{
    for (uint8_t voice = 0U; voice < SEQ_RUNTIME_PLAY_VOICE_COUNT; ++voice)
    {
        if (seq_runtime_live_rec_voice_has_any_lock(track, step, voice) == 0U)
        {
            return (int32_t)voice;
        }
    }

    return -1;
}

static int32_t seq_runtime_live_rec_alloc_pending_slot(void)
{
    for (uint8_t i = 0U; i < SEQ_RUNTIME_LIVE_REC_PENDING_CAP; ++i)
    {
        if (g_seq_live_rec_pending[i].active == 0U)
        {
            return (int32_t)i;
        }
    }

    return -1;
}

static int32_t seq_runtime_live_rec_find_pending_for_note(seq_track_id_t track,
                                                          seq_live_rec_source_t source,
                                                          uint8_t channel_zero_based,
                                                          uint8_t note)
{
    int32_t best = -1;
    uint32_t best_tick = 0U;

    for (uint8_t i = 0U; i < SEQ_RUNTIME_LIVE_REC_PENDING_CAP; ++i)
    {
        if ((g_seq_live_rec_pending[i].active == 0U)
            || (g_seq_live_rec_pending[i].track != track)
            || (g_seq_live_rec_pending[i].channel != channel_zero_based)
            || (g_seq_live_rec_pending[i].note != note)
            || (g_seq_live_rec_pending[i].source != (uint8_t)source))
        {
            continue;
        }

        if ((best < 0) || (g_seq_live_rec_pending[i].start_tick >= best_tick))
        {
            best = (int32_t)i;
            best_tick = g_seq_live_rec_pending[i].start_tick;
        }
    }

    return best;
}

static uint8_t seq_runtime_live_rec_track_accepts_source(seq_track_id_t track,
                                                         seq_live_rec_source_t source)
{
    const ui_track_midi_source_t track_source = ui_get_track_midi_source(track);
    if (source == SEQ_LIVE_REC_SRC_INTERNAL)
    {
        return ((track_source == UI_TRACK_MIDI_SRC_INT) || (track_source == UI_TRACK_MIDI_SRC_ALL)) ? 1U : 0U;
    }

    return ((track_source == UI_TRACK_MIDI_SRC_EXT) || (track_source == UI_TRACK_MIDI_SRC_ALL)) ? 1U : 0U;
}

static uint8_t seq_runtime_live_rec_is_active(void)
{
    return ((g_seq_rec_armed != 0U)
            && (g_seq_runtime.running != 0U)
            && (g_seq_rec_count_in_remaining_steps == 0U)) ? 1U : 0U;
}

static void seq_runtime_live_rec_compute_step_and_mictim(seq_track_id_t track,
                                                         seq_step_id_t *io_step,
                                                         int8_t *out_mictim)
{
    if ((io_step == 0) || (out_mictim == 0))
    {
        return;
    }

    const uint16_t tps = (g_seq_runtime.ticks_per_step == 0U) ? 1U : g_seq_runtime.ticks_per_step;
    const uint32_t offset_ticks = g_seq_runtime.tick_accum;
    int32_t micro = 0;

    if (offset_ticks <= (uint32_t)(tps / 4U))
    {
        micro = (int32_t)((offset_ticks * 96U) / tps);
        if (micro > 24)
        {
            micro = 24;
        }
        *out_mictim = (int8_t)micro;
        return;
    }

    if (offset_ticks >= (uint32_t)((3U * tps) / 4U))
    {
        micro = (int32_t)(((int32_t)offset_ticks - (int32_t)tps) * 96) / (int32_t)tps;
        if (micro < -24)
        {
            micro = -24;
        }

        uint8_t length = seq_model_get_track_length(track);
        if ((length == 0U) || (length > SEQ_MAX_STEPS))
        {
            length = SEQ_MAX_STEPS;
        }

        uint8_t next = (uint8_t)(*io_step + 1U);
        if (next >= length)
        {
            next = 0U;
        }

        *io_step = next;
        *out_mictim = (int8_t)micro;
        return;
    }

    if (offset_ticks < (uint32_t)(tps / 2U))
    {
        *out_mictim = 24;
        return;
    }

    uint8_t length = seq_model_get_track_length(track);
    if ((length == 0U) || (length > SEQ_MAX_STEPS))
    {
        length = SEQ_MAX_STEPS;
    }

    uint8_t next = (uint8_t)(*io_step + 1U);
    if (next >= length)
    {
        next = 0U;
    }

    *io_step = next;
    *out_mictim = -24;
}

static uint8_t seq_runtime_live_rec_upsert_play_param(seq_track_id_t track,
                                                      seq_step_id_t step,
                                                      param_id_t param_id,
                                                      float value)
{
    uint8_t set_id = 0U;
    seq_param8_t param8 = 0U;
    if (seq_param_iface_map_param(param_id, &set_id, &param8) == 0U)
    {
        return 0U;
    }

    const seq_value16_t encoded = seq_param_iface_encode_param_value(param_id, value);
    const seq_plock_op_status_t st = seq_model_step_plock_upsert(track,
                                                                  step,
                                                                  set_id,
                                                                  param8,
                                                                  encoded,
                                                                  0U);
    return ((st == SEQ_PLOCK_OP_CREATED) || (st == SEQ_PLOCK_OP_UPDATED)) ? 1U : 0U;
}

static void seq_runtime_live_rec_finalize_pending(seq_live_rec_pending_note_t *pending,
                                                  uint32_t stop_tick)
{
    if ((pending == 0) || (pending->active == 0U))
    {
        return;
    }

    const uint32_t duration_ticks = (stop_tick >= pending->start_tick) ? (stop_tick - pending->start_tick) : 0U;
    const uint16_t tps = (g_seq_runtime.ticks_per_step == 0U) ? 1U : g_seq_runtime.ticks_per_step;
    uint32_t len_steps = (duration_ticks + (uint32_t)tps - 1U) / (uint32_t)tps;
    if (len_steps < 1U)
    {
        len_steps = 1U;
    }
    if (len_steps > 64U)
    {
        len_steps = 64U;
    }

    (void)seq_runtime_live_rec_upsert_play_param(pending->track,
                                                 pending->step,
                                                 seq_runtime_play_param_len(pending->voice),
                                                 (float)len_steps);
    pending->active = 0U;
}

static void seq_runtime_live_rec_flush_all_pending(uint32_t stop_tick)
{
    for (uint8_t i = 0U; i < SEQ_RUNTIME_LIVE_REC_PENDING_CAP; ++i)
    {
        seq_runtime_live_rec_finalize_pending(&g_seq_live_rec_pending[i], stop_tick);
    }
}

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
            seq_play_scheduler_schedule_step(track,
                                             current_step,
                                             g_seq_runtime.ticks_per_step,
                                             engine_tick_count);

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
    g_seq_runtime.last_tick_count = engine_tick_count;
    seq_play_scheduler_init();
    seq_output_guard_init();
    seq_runtime_live_rec_pending_clear();
    g_seq_rec_armed = 0U;
    g_seq_rec_count_in_mode = 0U;
    g_seq_rec_count_in_remaining_steps = 0U;
    g_seq_start_pending = 0U;
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
    if ((g_seq_runtime.running != 0U) || (g_seq_start_pending != 0U))
    {
        return;
    }

    g_seq_runtime.tick_accum = 0U;
    g_seq_internal_next_step_ticks = seq_runtime_internal_step_next_ticks();
    g_seq_runtime.last_tick_count = engine_tick_count;
    g_seq_runtime.ext_clock_tick_accum = 0U;
    seq_play_scheduler_clear();
    seq_output_guard_reset();
    seq_runtime_live_rec_pending_clear();
    g_seq_rec_count_in_remaining_steps = (g_seq_rec_armed != 0U)
                                         ? seq_runtime_rec_count_in_steps_from_mode(g_seq_rec_count_in_mode)
                                         : 0U;

    if (g_seq_rec_count_in_remaining_steps > 0U)
    {
        g_seq_start_pending = 1U;
        g_seq_runtime.running = 0U;
        return;
    }

    seq_runtime_begin_running_now();
}

void seq_runtime_stop(void)
{
    if ((g_seq_runtime.running == 0U) && (g_seq_start_pending == 0U))
    {
        return;
    }

    if (g_seq_start_pending != 0U)
    {
        g_seq_start_pending = 0U;
        g_seq_runtime.running = 0U;
        g_seq_runtime.tick_accum = 0U;
        g_seq_runtime.ext_clock_tick_accum = 0U;
        g_seq_rec_count_in_remaining_steps = 0U;
        seq_play_scheduler_clear();
        seq_output_guard_reset();
        seq_runtime_live_rec_pending_clear();
        return;
    }

    seq_runtime_live_rec_flush_all_pending(engine_tick_count);
    g_seq_runtime.running = 0U;
    g_seq_runtime.tick_accum = 0U;
    SEQ_STOP_LOG("[SEQ][STOP] request\r\n");
    seq_play_scheduler_clear();

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        seq_runtime_restore_all_active_locks(track);
        g_seq_runtime.prev_step_valid[track] = 0U;
    }

    g_seq_midi_clock_tick_accum = 0U;
    g_seq_rec_count_in_remaining_steps = 0U;
    seq_runtime_send_transport_stop_and_panic();
    seq_output_guard_reset();
    seq_runtime_live_rec_pending_clear();
}

void seq_runtime_toggle_play_stop(void)
{
    if ((g_seq_runtime.running == 0U) && (g_seq_start_pending == 0U))
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

    if ((g_seq_runtime.running == 0U) && (g_seq_start_pending == 0U))
    {
        g_seq_runtime.last_tick_count = engine_tick_count;
        seq_play_scheduler_service(engine_tick_count, g_seq_runtime.running);
        return;
    }

    if (g_seq_start_pending != 0U)
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
               && (g_seq_rec_count_in_remaining_steps > 0U))
        {
            const uint32_t step_ticks = g_seq_internal_next_step_ticks;
            g_seq_runtime.tick_accum -= step_ticks;
            g_seq_rec_count_in_remaining_steps--;
            g_seq_internal_next_step_ticks = seq_runtime_internal_step_next_ticks();
        }

        if (g_seq_rec_count_in_remaining_steps == 0U)
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
        seq_runtime_advance_one_step();
        if ((g_seq_rec_count_in_remaining_steps > 0U) && (g_seq_runtime.running != 0U))
        {
            g_seq_rec_count_in_remaining_steps--;
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

    if ((g_seq_runtime.running == 0U) && (g_seq_start_pending == 0U))
    {
        return;
    }

    g_seq_runtime.ext_clock_tick_accum++;
    if (g_seq_runtime.ext_clock_tick_accum < SEQ_RUNTIME_MIDI_CLOCKS_PER_STEP)
    {
        return;
    }

    g_seq_runtime.ext_clock_tick_accum = 0U;
    if (g_seq_start_pending != 0U)
    {
        if (g_seq_rec_count_in_remaining_steps > 0U)
        {
            g_seq_rec_count_in_remaining_steps--;
        }
        if (g_seq_rec_count_in_remaining_steps == 0U)
        {
            seq_runtime_begin_running_now();
        }
        return;
    }

    seq_runtime_advance_one_step();
    if (g_seq_rec_count_in_remaining_steps > 0U)
    {
        g_seq_rec_count_in_remaining_steps--;
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
        seq_runtime_live_rec_flush_all_pending(engine_tick_count);
        seq_runtime_live_rec_pending_clear();
        g_seq_rec_count_in_remaining_steps = 0U;
        g_seq_start_pending = 0U;
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
    return g_seq_rec_count_in_remaining_steps;
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
    if ((velocity == 0U) || (note >= 128U) || (source > SEQ_LIVE_REC_SRC_EXTERNAL))
    {
        return;
    }

    if (seq_runtime_live_rec_is_active() == 0U)
    {
        return;
    }

    if ((source == SEQ_LIVE_REC_SRC_EXTERNAL)
        && (seq_output_guard_is_note_active_on_channel(channel_zero_based, note) != 0U))
    {
        /* Ignore external echo of sequencer playback notes to avoid live-rec feedback duplication. */
        return;
    }

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const uint8_t track_ch_1_16 = ui_get_track_midi_channel(track);
        const uint8_t track_ch = (uint8_t)((track_ch_1_16 > 0U) ? (track_ch_1_16 - 1U) : 0U);
        if (track_ch != channel_zero_based)
        {
            continue;
        }

        if (seq_runtime_live_rec_track_accepts_source(track, source) == 0U)
        {
            continue;
        }

        if (track_runtime_get_effective_param_status(track, PARAM_SEQ_PLAY_V1_NOTE) != TRACK_RUNTIME_PARAM_ALLOWED)
        {
            continue;
        }

        const seq_step_id_t step = g_seq_runtime.play_step[track];
        seq_step_id_t write_step = step;
        int8_t mictim = 0;
        seq_runtime_live_rec_compute_step_and_mictim(track, &write_step, &mictim);
        int32_t voice = seq_runtime_live_rec_find_voice_with_note_lock(track, write_step, note);
        if (voice < 0)
        {
            voice = seq_runtime_live_rec_find_free_voice(track, write_step);
        }
        if (voice < 0)
        {
            continue;
        }

        const int32_t pending_slot = seq_runtime_live_rec_alloc_pending_slot();
        if (pending_slot < 0)
        {
            continue;
        }

        const uint8_t note_ok = seq_runtime_live_rec_upsert_play_param(track,
                                                                       write_step,
                                                                       seq_runtime_play_param_note((uint8_t)voice),
                                                                       (float)note);
        const uint8_t vel_ok = seq_runtime_live_rec_upsert_play_param(track,
                                                                      write_step,
                                                                      seq_runtime_play_param_vel((uint8_t)voice),
                                                                      (float)velocity);
        const uint8_t micro_ok = seq_runtime_live_rec_upsert_play_param(track,
                                                                        write_step,
                                                                        seq_runtime_play_param_mictim((uint8_t)voice),
                                                                        (float)mictim);
        if ((note_ok == 0U) || (vel_ok == 0U) || (micro_ok == 0U))
        {
            continue;
        }

        seq_model_set_trig(track, write_step, 1U);

        g_seq_live_rec_pending[pending_slot].active = 1U;
        g_seq_live_rec_pending[pending_slot].track = track;
        g_seq_live_rec_pending[pending_slot].channel = channel_zero_based;
        g_seq_live_rec_pending[pending_slot].note = note;
        g_seq_live_rec_pending[pending_slot].source = (uint8_t)source;
        g_seq_live_rec_pending[pending_slot].voice = (uint8_t)voice;
        g_seq_live_rec_pending[pending_slot].step = write_step;
        g_seq_live_rec_pending[pending_slot].start_tick = engine_tick_count;
    }
}

void seq_runtime_live_rec_note_off(seq_live_rec_source_t source,
                                   uint8_t channel_zero_based,
                                   uint8_t note)
{
    if ((note >= 128U) || (source > SEQ_LIVE_REC_SRC_EXTERNAL))
    {
        return;
    }

    if (seq_runtime_live_rec_is_active() == 0U)
    {
        return;
    }

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const uint8_t track_ch_1_16 = ui_get_track_midi_channel(track);
        const uint8_t track_ch = (uint8_t)((track_ch_1_16 > 0U) ? (track_ch_1_16 - 1U) : 0U);
        if (track_ch != channel_zero_based)
        {
            continue;
        }

        if (seq_runtime_live_rec_track_accepts_source(track, source) == 0U)
        {
            continue;
        }

        const int32_t pending_slot = seq_runtime_live_rec_find_pending_for_note(track,
                                                                                 source,
                                                                                 channel_zero_based,
                                                                                 note);
        if (pending_slot < 0)
        {
            continue;
        }

        seq_runtime_live_rec_finalize_pending(&g_seq_live_rec_pending[pending_slot], engine_tick_count);
    }
}
