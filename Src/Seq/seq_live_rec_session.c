/*
 * Module: seq_live_rec_session
 * Role: Etat et orchestration live-rec du sequenceur.
 * Responsibilities: arming/count-in/pattern-rec, capture note, ecriture plock
 * live-rec et gestion des transitions de session.
 * Integration: possede la capture live-rec et consomme seq_edit, sans porter le transport.
 */
#include "Seq/seq_live_rec_session.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Track/track_runtime.h"
#include "Track/track_state.h"

#include "Seq/seq_edit.h"
#include "Track/control_music_output.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_model.h"
#include "Seq/seq_runtime_control.h"

typedef struct
{
    uint8_t active;
    uint8_t track;
    uint8_t channel;
    uint8_t note;
    uint8_t source;
    uint8_t voice;
    uint8_t step;
    uint8_t start_step;
    int8_t start_mictim;
    uint64_t start_sample;
    uint64_t start_sample_effective;
    uint32_t occurrence_id;
} seq_live_rec_session_pending_note_t;

SEQ_STATE_D2 static uint8_t g_seq_live_rec_armed;
SEQ_STATE_D2 static uint8_t g_seq_live_rec_start_mode;
SEQ_STATE_D2 static uint8_t g_seq_live_rec_waiting_trigger_start;
SEQ_STATE_D2 static uint8_t g_seq_live_rec_len_mode;
SEQ_STATE_D2 static uint8_t g_seq_live_rec_pattern_pending_start;
SEQ_STATE_D2 static uint8_t g_seq_live_rec_pattern_active;
SEQ_STATE_D2 static uint8_t g_seq_live_rec_pattern_target_track;
SEQ_STATE_D2 static uint8_t g_seq_live_rec_pattern_track;
SEQ_STATE_D2 static uint32_t g_seq_live_rec_pattern_steps_remaining;

CONTROL_M4_SRAM2 static seq_live_rec_session_pending_note_t g_seq_live_rec_pending[64U];

static uint8_t seq_live_rec_session_is_live_rec_active(void)
{
    if (g_seq_live_rec_len_mode == (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
    {
        return g_seq_live_rec_pattern_active;
    }

    /* Projection reads: transport state and count-in gates are consumed as runtime mirrors. */
    return ((g_seq_live_rec_armed != 0U)
            && (seq_runtime_is_running() != 0U)
            && (seq_runtime_get_rec_count_in_remaining_steps() == 0U)) ? 1U : 0U;
}

static uint8_t seq_live_rec_session_track_accepts_source(seq_track_id_t track,
                                                         seq_live_rec_source_t source)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return 0U;
    }

    /* Projection read: MIDI source gates live-rec acceptance; no runtime mutation here. */
    const track_runtime_midi_source_t track_source = track_runtime_get_midi_source(track);
    if (source == SEQ_LIVE_REC_SRC_INTERNAL)
    {
        return ((track_source == TRACK_RUNTIME_MIDI_SOURCE_INTERNAL)
                || (track_source == TRACK_RUNTIME_MIDI_SOURCE_ALL)) ? 1U : 0U;
    }

    return ((track_source == TRACK_RUNTIME_MIDI_SOURCE_EXTERNAL)
            || (track_source == TRACK_RUNTIME_MIDI_SOURCE_ALL)) ? 1U : 0U;
}

static void seq_live_rec_session_compute_step_and_mictim(seq_track_id_t track,
                                                         seq_step_id_t *io_step,
                                                         int8_t *out_mictim,
                                                         uint32_t samples_per_step_q16,
                                                         uint64_t step_sample_q16,
                                                         uint64_t now_sample)
{
    if ((io_step == 0) || (out_mictim == 0))
    {
        return;
    }

    const uint32_t sps_q16 = (samples_per_step_q16 == 0U) ? 1U : samples_per_step_q16;
    const uint64_t now_q16 = now_sample << 16;
    const uint64_t offset_q16 = (now_q16 >= step_sample_q16) ? (now_q16 - step_sample_q16) : 0U;
    const uint32_t offset_q16_mod = (uint32_t)(offset_q16 % (uint64_t)sps_q16);
    const uint32_t half_q16 = sps_q16 / 2U;
    const uint32_t quarter_q16 = sps_q16 / 4U;
    const uint32_t three_quarter_q16 = quarter_q16 * 3U;
    int32_t micro = 0;

    if (offset_q16_mod <= quarter_q16)
    {
        micro = (int32_t)(((uint64_t)offset_q16_mod * 96ULL) / (uint64_t)sps_q16);
        if (micro > 24)
        {
            micro = 24;
        }
        *out_mictim = (int8_t)micro;
        return;
    }

    if (offset_q16_mod >= three_quarter_q16)
    {
        micro = (int32_t)((((int64_t)offset_q16_mod - (int64_t)sps_q16) * 96LL) / (int64_t)sps_q16);
        if (micro < -24)
        {
            micro = -24;
        }

        const uint8_t length = seq_model_get_track_playback_length(track);

        uint8_t next = (uint8_t)(*io_step + 1U);
        if (next >= length)
        {
            next = 0U;
        }

        *io_step = next;
        *out_mictim = (int8_t)micro;
        return;
    }

    if (offset_q16_mod < half_q16)
    {
        *out_mictim = 24;
        return;
    }

    const uint8_t length = seq_model_get_track_playback_length(track);

    uint8_t next = (uint8_t)(*io_step + 1U);
    if (next >= length)
    {
        next = 0U;
    }

    *io_step = next;
    *out_mictim = -24;
}

static uint8_t seq_live_rec_session_upsert_play_param(seq_track_id_t track,
                                                     seq_step_id_t step,
                                                     uint8_t voice,
                                                     seq_step_play_field_t field,
                                                     int16_t value)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return 0U;
    }

    const seq_plock_op_status_t st = seq_edit_step_play_upsert(track, step, voice, field, value);
    return ((st == SEQ_PLOCK_OP_CREATED) || (st == SEQ_PLOCK_OP_UPDATED)) ? 1U : 0U;
}

static uint8_t seq_live_rec_session_delete_play_param(seq_track_id_t track,
                                                      seq_step_id_t step,
                                                      uint8_t voice,
                                                      seq_step_play_field_t field)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return 0U;
    }

    const seq_plock_op_status_t st = seq_edit_step_play_delete(track, step, voice, field);
    return (st == SEQ_PLOCK_OP_DELETED) ? 1U : 0U;
}

typedef struct
{
    uint8_t present;
    seq_value16_t value16;
} seq_live_rec_session_saved_param_t;

static uint8_t seq_live_rec_session_read_play_param(seq_track_id_t track,
                                                    seq_step_id_t step,
                                                    uint8_t voice,
                                                    seq_step_play_field_t field,
                                                    seq_live_rec_session_saved_param_t *out_saved)
{
    if (out_saved == 0)
    {
        return 0U;
    }

    out_saved->present = 0U;
    out_saved->value16 = 0U;

    int16_t value;
    if (seq_edit_step_play_get(track, step, voice, field, &value) == 0U)
    {
        return 1U;
    }

    out_saved->present = 1U;
    out_saved->value16 = (seq_value16_t)value;
    return 1U;
}

static uint8_t seq_live_rec_session_restore_play_param(seq_track_id_t track,
                                                       seq_step_id_t step,
                                                       uint8_t voice,
                                                       seq_step_play_field_t field,
                                                       const seq_live_rec_session_saved_param_t *saved)
{
    if ((saved == 0) || (seq_edit_track_sequence_is_locked(track) != 0U))
    {
        return 0U;
    }

    if (saved->present == 0U)
    {
        (void)seq_live_rec_session_delete_play_param(track, step, voice, field);
        return 1U;
    }

    const seq_plock_op_status_t st = seq_edit_step_play_upsert(track, step, voice, field,
                                                               (int16_t)saved->value16);
    return ((st == SEQ_PLOCK_OP_CREATED) || (st == SEQ_PLOCK_OP_UPDATED)) ? 1U : 0U;
}

static void seq_live_rec_session_clear_voice_play_params(seq_track_id_t track,
                                                        seq_step_id_t step,
                                                        uint8_t voice)
{
    for (uint8_t field = 0U; field < SEQ_STEP_PLAY_FIELD_COUNT; ++field)
        (void)seq_live_rec_session_delete_play_param(track, step, voice,
                                                     (seq_step_play_field_t)field);
}

static void seq_live_rec_session_normalize_step_for_write(seq_track_id_t track, seq_step_id_t step)
{
    if (seq_model_get_trig(track, step) != 0U)
    {
        return;
    }

    const uint8_t play_capacity = seq_model_play_capacity(track);
    for (uint8_t voice = 0U; voice < play_capacity; ++voice)
    {
        seq_live_rec_session_clear_voice_play_params(track, step, voice);
    }
}

static uint8_t seq_live_rec_session_voice_has_any_lock(seq_track_id_t track,
                                                       seq_step_id_t step,
                                                       uint8_t voice);

static void seq_live_rec_session_finalize_pending(seq_live_rec_session_pending_note_t *pending,
                                                  uint64_t stop_sample,
                                                  uint32_t samples_per_step_q16)
{
    if ((pending == 0) || (pending->active == 0U))
    {
        return;
    }

    const uint64_t effective_start_sample = pending->start_sample_effective;
    const uint64_t effective_stop_sample = (stop_sample >= effective_start_sample)
                                             ? stop_sample : effective_start_sample;
    const uint64_t duration_samples = (effective_stop_sample >= effective_start_sample)
                                            ? (effective_stop_sample - effective_start_sample)
                                            : 0U;
    const uint32_t sps_q16 = (samples_per_step_q16 == 0U) ? 1U : samples_per_step_q16;
    const uint64_t duration_q16 = duration_samples << 16;
    uint32_t len_steps = (uint32_t)((duration_q16 + (uint64_t)sps_q16 - 1U) / (uint64_t)sps_q16);
    if (len_steps < 1U)
    {
        len_steps = 1U;
    }
    if (len_steps > 64U)
    {
        len_steps = 64U;
    }

    (void)seq_live_rec_session_upsert_play_param(pending->track,
                                                 pending->step,
                                                 pending->voice,
                                                 SEQ_STEP_PLAY_FIELD_LENGTH,
                                                 (int16_t)len_steps);
    pending->active = 0U;
}

static void seq_live_rec_session_reset_pending(void)
{
    memset(g_seq_live_rec_pending, 0, sizeof(g_seq_live_rec_pending));
}

static void seq_live_rec_session_flush_pending(uint64_t stop_sample,
                                               uint32_t samples_per_step_q16)
{
    for (uint8_t i = 0U; i < 64U; ++i)
    {
        seq_live_rec_session_finalize_pending(&g_seq_live_rec_pending[i],
                                              stop_sample,
                                              samples_per_step_q16);
    }
}

static void seq_live_rec_session_finalize_slot(int32_t slot,
                                               uint64_t stop_sample,
                                               uint32_t samples_per_step_q16)
{
    if ((slot < 0) || (slot >= 64))
    {
        return;
    }

    seq_live_rec_session_finalize_pending(&g_seq_live_rec_pending[(uint8_t)slot],
                                          stop_sample,
                                          samples_per_step_q16);
}

static int32_t seq_live_rec_session_alloc_pending_slot(void)
{
    for (uint8_t i = 0U; i < 64U; ++i)
    {
        if (g_seq_live_rec_pending[i].active == 0U)
        {
            return (int32_t)i;
        }
    }

    return -1;
}

static int32_t seq_live_rec_session_find_pending_for_note(seq_track_id_t track,
                                                          seq_live_rec_source_t source,
                                                          uint8_t channel_zero_based,
                                                          uint8_t note,
                                                          uint32_t occurrence_id)
{
    for (uint8_t i = 0U; i < 64U; ++i)
    {
        if ((g_seq_live_rec_pending[i].active == 0U)
            || (g_seq_live_rec_pending[i].track != track)
            || (g_seq_live_rec_pending[i].channel != channel_zero_based)
            || (g_seq_live_rec_pending[i].note != note)
            || (g_seq_live_rec_pending[i].source != (uint8_t)source))
        {
            continue;
        }
        if ((occurrence_id != 0U)
            && (g_seq_live_rec_pending[i].occurrence_id != occurrence_id))
        {
            continue;
        }

        return (int32_t)i;
    }

    return -1;
}

static int32_t seq_live_rec_session_find_pending_for_note_any_source(seq_track_id_t track,
                                                                     uint8_t channel_zero_based,
                                                                     uint8_t note)
{
    for (uint8_t i = 0U; i < 64U; ++i)
    {
        if ((g_seq_live_rec_pending[i].active == 0U)
            || (g_seq_live_rec_pending[i].track != track)
            || (g_seq_live_rec_pending[i].channel != channel_zero_based)
            || (g_seq_live_rec_pending[i].note != note))
        {
            continue;
        }

        return (int32_t)i;
    }

    return -1;
}

static int32_t seq_live_rec_session_find_pending_for_voice(seq_track_id_t track,
                                                           seq_step_id_t step,
                                                           uint8_t voice)
{
    for (uint8_t i = 0U; i < 64U; ++i)
    {
        if ((g_seq_live_rec_pending[i].active == 0U)
            || (g_seq_live_rec_pending[i].track != track)
            || (g_seq_live_rec_pending[i].step != step)
            || (g_seq_live_rec_pending[i].voice != voice))
        {
            continue;
        }

        return (int32_t)i;
    }

    return -1;
}

static uint8_t seq_live_rec_session_voice_is_owned(seq_track_id_t track,
                                                   seq_step_id_t step,
                                                   uint8_t voice)
{
    return (seq_live_rec_session_find_pending_for_voice(track, step, voice) >= 0) ? 1U : 0U;
}

static int32_t seq_live_rec_session_find_voice_with_note_lock(seq_track_id_t track,
                                                              seq_step_id_t step,
                                                              uint8_t note)
{
    const uint8_t play_capacity = seq_model_play_capacity(track);
    for (uint8_t voice = 0U; voice < play_capacity; ++voice)
    {
        int16_t value;
        if (seq_edit_step_play_get(track, step, voice,
                                    SEQ_STEP_PLAY_FIELD_NOTE, &value) == 0U)
        {
            continue;
        }

        const uint8_t existing_note = (uint8_t)value;
        if (existing_note == note)
        {
            return (int32_t)voice;
        }
    }

    return -1;
}

static int32_t seq_live_rec_session_find_free_voice(seq_track_id_t track,
                                                    seq_step_id_t step)
{
    const uint8_t play_capacity = seq_model_play_capacity(track);
    for (uint8_t voice = 0U; voice < play_capacity; ++voice)
    {
        if (seq_live_rec_session_voice_has_any_lock(track, step, voice) == 0U)
        {
            return (int32_t)voice;
        }
    }

    return -1;
}

static int32_t seq_live_rec_session_select_voice_deterministic(seq_track_id_t track,
                                                               seq_step_id_t step,
                                                               uint8_t note)
{
    int32_t voice = seq_live_rec_session_find_voice_with_note_lock(track, step, note);
    if (voice >= 0)
    {
        return voice;
    }

    voice = seq_live_rec_session_find_free_voice(track, step);
    if (voice >= 0)
    {
        return voice;
    }

    const uint8_t play_capacity = seq_model_play_capacity(track);
    for (uint8_t v = 0U; v < play_capacity; ++v)
    {
        if (seq_live_rec_session_voice_is_owned(track, step, v) == 0U)
        {
            return (int32_t)v;
        }
    }

    return 0;
}

static uint8_t seq_live_rec_session_voice_has_any_lock(seq_track_id_t track,
                                                       seq_step_id_t step,
                                                       uint8_t voice)
{
    for (uint8_t field = 0U; field < SEQ_STEP_PLAY_FIELD_COUNT; ++field)
    {
        int16_t value;
        if (seq_edit_step_play_get(track, step, voice,
                (seq_step_play_field_t)field, &value) != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

static void seq_live_rec_session_bind_pattern_track_to_target(void)
{
    if (g_seq_live_rec_pattern_target_track >= SEQ_TRACK_COUNT)
    {
        g_seq_live_rec_pattern_track = 0U;
        return;
    }

    g_seq_live_rec_pattern_track = g_seq_live_rec_pattern_target_track;
}

static uint32_t seq_live_rec_session_get_track_pattern_duration_steps(seq_track_id_t track)
{
    uint8_t div = 1U;
    /* Projection read: pattern duration uses track div as a runtime mirror. */
    (void)seq_runtime_get_track_div(track, &div);
    return (uint32_t)seq_model_get_track_playback_length(track) * (uint32_t)div;
}

static void seq_live_rec_session_pattern_rec_start_now(void)
{
    uint8_t track = g_seq_live_rec_pattern_track;
    if (track >= SEQ_TRACK_COUNT)
    {
        track = 0U;
    }

    const uint8_t length = seq_model_get_track_playback_length(track);

    g_seq_live_rec_pattern_track = track;
    g_seq_live_rec_pattern_steps_remaining = seq_live_rec_session_get_track_pattern_duration_steps(track);
    g_seq_live_rec_pattern_pending_start = 0U;
    g_seq_live_rec_pattern_active = 1U;

    seq_step_id_t steps[SEQ_MAX_STEPS];
    for (uint8_t i = 0U; i < length; ++i)
    {
        steps[i] = (seq_step_id_t)i;
    }
    seq_edit_clear_steps_without_undo(track, steps, length);
    seq_live_rec_session_reset_pending();
}

static void seq_live_rec_session_pattern_rec_cancel(void)
{
    g_seq_live_rec_pattern_pending_start = 0U;
    g_seq_live_rec_pattern_active = 0U;
    g_seq_live_rec_pattern_steps_remaining = 0U;
}

static void seq_live_rec_session_flush_and_reset(uint64_t stop_sample, uint32_t samples_per_step_q16)
{
    seq_live_rec_session_flush_pending(stop_sample, samples_per_step_q16);
    seq_live_rec_session_reset_pending();
}

static void seq_live_rec_session_pattern_rec_on_step_advanced(const seq_runtime_state_t *runtime_state,
                                                              uint64_t now_sample)
{
    if ((g_seq_live_rec_len_mode != (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
        || (g_seq_live_rec_armed == 0U))
    {
        return;
    }

    if ((g_seq_live_rec_pattern_pending_start != 0U)
        && (runtime_state->play_step[g_seq_live_rec_pattern_track] == 0U))
    {
        seq_live_rec_session_pattern_rec_start_now();
    }

    if (g_seq_live_rec_pattern_active == 0U)
    {
        return;
    }

    if (g_seq_live_rec_pattern_steps_remaining > 0U)
    {
        g_seq_live_rec_pattern_steps_remaining--;
    }

    if (g_seq_live_rec_pattern_steps_remaining == 0U)
    {
        seq_live_rec_session_flush_and_reset(now_sample, runtime_state->samples_per_step_q16);
        seq_live_rec_session_pattern_rec_cancel();
        g_seq_live_rec_armed = 0U;
    }
}

void seq_live_rec_session_init(void)
{
    seq_live_rec_session_reset_pending();
    g_seq_live_rec_armed = 0U;
    g_seq_live_rec_start_mode = (uint8_t)SEQ_REC_START_DEFAULT;
    g_seq_live_rec_waiting_trigger_start = 0U;
    g_seq_live_rec_len_mode = (uint8_t)SEQ_REC_LEN_MODE_OVERDUB;
    g_seq_live_rec_pattern_pending_start = 0U;
    g_seq_live_rec_pattern_active = 0U;
    g_seq_live_rec_pattern_target_track = 0U;
    g_seq_live_rec_pattern_track = 0U;
    g_seq_live_rec_pattern_steps_remaining = 0U;
    seq_edit_init();
}

void seq_live_rec_session_reset_capture(void)
{
    seq_live_rec_session_reset_pending();
}

void seq_live_rec_session_on_transport_start(void)
{
    seq_live_rec_session_reset_capture();

    if ((g_seq_live_rec_len_mode == (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
        && (g_seq_live_rec_armed != 0U))
    {
        seq_live_rec_session_pattern_rec_start_now();
    }
}

void seq_live_rec_session_on_transport_stop(uint64_t stop_sample, uint32_t samples_per_step_q16)
{
    seq_live_rec_session_flush_and_reset(stop_sample, samples_per_step_q16);
    seq_live_rec_session_pattern_rec_cancel();
    g_seq_live_rec_waiting_trigger_start = 0U;
}

void seq_live_rec_session_on_step_advanced(const seq_runtime_state_t *runtime_state, uint64_t now_sample)
{
    if (runtime_state == 0)
    {
        return;
    }

    seq_live_rec_session_pattern_rec_on_step_advanced(runtime_state, now_sample);
}

void seq_live_rec_session_toggle_arm(uint64_t now_sample, uint32_t samples_per_step_q16)
{
    if (g_seq_live_rec_len_mode == (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
    {
        if (g_seq_live_rec_pattern_pending_start != 0U)
        {
            g_seq_live_rec_armed = 0U;
            seq_live_rec_session_pattern_rec_cancel();
            seq_live_rec_session_flush_and_reset(now_sample, samples_per_step_q16);
            g_seq_live_rec_waiting_trigger_start = 0U;
            return;
        }

        if ((g_seq_live_rec_armed == 0U) && (seq_runtime_is_running() != 0U))
        {
            g_seq_live_rec_armed = 1U;
            seq_live_rec_session_bind_pattern_track_to_target();
            g_seq_live_rec_pattern_pending_start = 1U;
            g_seq_live_rec_pattern_active = 0U;
            g_seq_live_rec_pattern_steps_remaining = 0U;
            seq_live_rec_session_flush_and_reset(now_sample, samples_per_step_q16);
            return;
        }
    }

    g_seq_live_rec_armed = (g_seq_live_rec_armed == 0U) ? 1U : 0U;
    if ((g_seq_live_rec_armed != 0U) && (g_seq_live_rec_len_mode == (uint8_t)SEQ_REC_LEN_MODE_PATTERN))
    {
        seq_live_rec_session_bind_pattern_track_to_target();
    }
    if (g_seq_live_rec_armed == 0U)
    {
        seq_live_rec_session_flush_and_reset(now_sample, samples_per_step_q16);
        seq_live_rec_session_pattern_rec_cancel();
        g_seq_live_rec_waiting_trigger_start = 0U;
    }
    else if ((g_seq_live_rec_start_mode == (uint8_t)SEQ_REC_START_TRIG)
             && (seq_runtime_is_running() == 0U))
    {
        g_seq_live_rec_waiting_trigger_start = 1U;
    }
}

uint8_t seq_live_rec_session_rec_is_armed(void)
{
    return g_seq_live_rec_armed;
}

void seq_live_rec_session_set_rec_start_mode(uint8_t mode)
{
    if (mode > (uint8_t)SEQ_REC_START_ROLL_1)
    {
        mode = (uint8_t)SEQ_REC_START_ROLL_1;
    }

    g_seq_live_rec_start_mode = mode;
    if (mode != (uint8_t)SEQ_REC_START_TRIG)
    {
        g_seq_live_rec_waiting_trigger_start = 0U;
    }
    else if ((g_seq_live_rec_armed != 0U) && (seq_runtime_is_running() == 0U))
    {
        g_seq_live_rec_waiting_trigger_start = 1U;
    }
}

uint8_t seq_live_rec_session_get_rec_start_mode(void)
{
    return g_seq_live_rec_start_mode;
}

uint8_t seq_live_rec_session_rec_should_wait_trigger_start(void)
{
    if ((g_seq_live_rec_armed == 0U)
        || (g_seq_live_rec_start_mode != (uint8_t)SEQ_REC_START_TRIG)
        || (seq_runtime_is_running() != 0U))
    {
        return 0U;
    }

    g_seq_live_rec_waiting_trigger_start = 1U;
    return 1U;
}

uint8_t seq_live_rec_session_consume_trigger_start_note_on(void)
{
    if ((g_seq_live_rec_armed == 0U)
        || (g_seq_live_rec_start_mode != (uint8_t)SEQ_REC_START_TRIG)
        || (g_seq_live_rec_waiting_trigger_start == 0U))
    {
        return 0U;
    }

    g_seq_live_rec_waiting_trigger_start = 0U;
    return 1U;
}

void seq_live_rec_session_clear_trigger_start_wait(void)
{
    g_seq_live_rec_waiting_trigger_start = 0U;
}

uint8_t seq_live_rec_session_rec_is_waiting_trigger_start(void)
{
    return g_seq_live_rec_waiting_trigger_start;
}

void seq_live_rec_session_set_rec_len_mode(uint8_t mode)
{
    if (mode > (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
    {
        mode = (uint8_t)SEQ_REC_LEN_MODE_PATTERN;
    }

    g_seq_live_rec_len_mode = mode;
    if (g_seq_live_rec_len_mode == (uint8_t)SEQ_REC_LEN_MODE_OVERDUB)
    {
        seq_live_rec_session_pattern_rec_cancel();
    }
}

uint8_t seq_live_rec_session_get_rec_len_mode(void)
{
    return g_seq_live_rec_len_mode;
}

uint8_t seq_live_rec_session_rec_is_pattern_pending_start(void)
{
    return ((g_seq_live_rec_len_mode == (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
            && (g_seq_live_rec_pattern_pending_start != 0U)) ? 1U : 0U;
}

void seq_live_rec_session_set_pattern_rec_target_track(seq_track_id_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_seq_live_rec_pattern_target_track = track;

    if ((g_seq_live_rec_len_mode == (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
        && (g_seq_live_rec_pattern_active == 0U))
    {
        g_seq_live_rec_pattern_track = g_seq_live_rec_pattern_target_track;
    }
}

uint8_t seq_live_rec_session_live_rec_param_can_write(seq_track_id_t track,
                                                      uint8_t set_id,
                                                      seq_param_slot_t param_slot)
{
    if ((track >= SEQ_TRACK_COUNT)
        || (seq_param_iface_is_set_plockable(set_id) == 0U)
        || (seq_live_rec_session_is_live_rec_active() == 0U)
        || (seq_edit_track_sequence_is_locked(track) != 0U))
    {
        return 0U;
    }

    if (seq_param_iface_slot_is_supported(track, set_id, param_slot) == 0U)
    {
        return 0U;
    }

    return (seq_model_get_track_playback_length(track) == 0U) ? 0U : 1U;
}

uint8_t seq_live_rec_session_live_rec_param_write(const seq_runtime_state_t *runtime_state,
                                                  seq_track_id_t track,
                                                  uint8_t set_id,
                                                  seq_param_slot_t param_slot,
                                                  seq_value16_t value16)
{
    if ((runtime_state == 0)
        || (seq_live_rec_session_live_rec_param_can_write(track, set_id, param_slot) == 0U))
    {
        return 0U;
    }

    const uint8_t length = seq_model_get_track_playback_length(track);
    seq_step_id_t step = runtime_state->play_step[track];
    if (step >= length)
    {
        step = 0U;
    }

    const seq_plock_op_status_t status = seq_edit_step_plock_upsert(track,
                                                                     step,
                                                                     set_id,
                                                                     param_slot,
                                                                     value16,
                                                                     0U);
    if ((status != SEQ_PLOCK_OP_CREATED) && (status != SEQ_PLOCK_OP_UPDATED))
    {
        return 0U;
    }

    seq_edit_step_plock_commit(track, step, set_id, param_slot);
    return 1U;
}

void seq_live_rec_session_live_rec_note_on(seq_live_rec_source_t source,
                                           uint8_t channel_zero_based,
                                           uint8_t note,
                                           uint8_t velocity,
                                           const seq_runtime_state_t *runtime_state,
                                           uint64_t now_sample,
                                           uint32_t occurrence_id)
{
    if ((runtime_state == 0)
        || (seq_live_rec_session_is_live_rec_active() == 0U)
        || (velocity == 0U)
        || (note >= 128U)
        || (source > SEQ_LIVE_REC_SRC_EXTERNAL))
    {
        return;
    }

    if ((source == SEQ_LIVE_REC_SRC_EXTERNAL)
        && (control_music_output_is_note_active_on_channel(
                channel_zero_based, note) != 0U))
    {
        return;
    }

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const uint8_t track_ch = track_runtime_get_midi_channel_zero_based(track);
        if (track_ch != channel_zero_based)
        {
            continue;
        }

        if (seq_live_rec_session_track_accepts_source(track, source) == 0U)
        {
            continue;
        }

        if (seq_model_track_can_store_play(track) == 0U)
        {
            continue;
        }

        const seq_step_id_t step = runtime_state->play_step[track];
        seq_step_id_t write_step = step;
        int8_t mictim = 0;
        seq_live_rec_session_compute_step_and_mictim(track,
                                                     &write_step,
                                                     &mictim,
                                                     runtime_state->samples_per_step_q16,
                                                     runtime_state->step_sample_q16,
                                                     now_sample);

        seq_live_rec_session_normalize_step_for_write(track, write_step);

        const int32_t same_source_pending = seq_live_rec_session_find_pending_for_note(track,
                                                                                       source,
                                                                                       channel_zero_based,
                                                                                       note,
                                                                                       occurrence_id);
        if (same_source_pending >= 0)
        {
            seq_live_rec_session_finalize_slot(same_source_pending,
                                               now_sample,
                                               runtime_state->samples_per_step_q16);
        }

        if (seq_live_rec_session_find_pending_for_note_any_source(track,
                                                                  channel_zero_based,
                                                                  note) >= 0)
        {
            continue;
        }

        int32_t voice = seq_live_rec_session_select_voice_deterministic(track, write_step, note);
        if (voice < 0)
        {
            continue;
        }

        const int32_t owner_slot = seq_live_rec_session_find_pending_for_voice(track,
                                                                               write_step,
                                                                               (uint8_t)voice);
        if (owner_slot >= 0)
        {
            seq_live_rec_session_finalize_slot(owner_slot,
                                               now_sample,
                                               runtime_state->samples_per_step_q16);
        }

        const int32_t pending_slot = seq_live_rec_session_alloc_pending_slot();
        if (pending_slot < 0)
        {
            continue;
        }

        seq_live_rec_session_saved_param_t saved_note;
        seq_live_rec_session_saved_param_t saved_vel;
        seq_live_rec_session_saved_param_t saved_len;
        seq_live_rec_session_saved_param_t saved_mictim;
        (void)seq_live_rec_session_read_play_param(track, write_step, (uint8_t)voice, SEQ_STEP_PLAY_FIELD_NOTE, &saved_note);
        (void)seq_live_rec_session_read_play_param(track, write_step, (uint8_t)voice, SEQ_STEP_PLAY_FIELD_VELOCITY, &saved_vel);
        (void)seq_live_rec_session_read_play_param(track, write_step, (uint8_t)voice, SEQ_STEP_PLAY_FIELD_LENGTH, &saved_len);
        (void)seq_live_rec_session_read_play_param(track, write_step, (uint8_t)voice, SEQ_STEP_PLAY_FIELD_MICROTIMING, &saved_mictim);

        seq_live_rec_session_clear_voice_play_params(track, write_step, (uint8_t)voice);

        const uint8_t note_ok = seq_live_rec_session_upsert_play_param(track,
                                                                        write_step,
                                                                        (uint8_t)voice,
                                                                        SEQ_STEP_PLAY_FIELD_NOTE,
                                                                        note);
        const uint8_t vel_ok = seq_live_rec_session_upsert_play_param(track,
                                                                       write_step,
                                                                       (uint8_t)voice,
                                                                       SEQ_STEP_PLAY_FIELD_VELOCITY,
                                                                       velocity);
        const uint8_t micro_ok = seq_live_rec_session_upsert_play_param(track,
                                                                         write_step,
                                                                         (uint8_t)voice,
                                                                         SEQ_STEP_PLAY_FIELD_MICROTIMING,
                                                                         mictim);
        if ((note_ok == 0U) || (vel_ok == 0U) || (micro_ok == 0U))
        {
            (void)seq_live_rec_session_restore_play_param(track, write_step, (uint8_t)voice, SEQ_STEP_PLAY_FIELD_NOTE, &saved_note);
            (void)seq_live_rec_session_restore_play_param(track, write_step, (uint8_t)voice, SEQ_STEP_PLAY_FIELD_VELOCITY, &saved_vel);
            (void)seq_live_rec_session_restore_play_param(track, write_step, (uint8_t)voice, SEQ_STEP_PLAY_FIELD_LENGTH, &saved_len);
            (void)seq_live_rec_session_restore_play_param(track, write_step, (uint8_t)voice, SEQ_STEP_PLAY_FIELD_MICROTIMING, &saved_mictim);
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
        g_seq_live_rec_pending[pending_slot].start_step = step;
        g_seq_live_rec_pending[pending_slot].start_mictim = mictim;
        g_seq_live_rec_pending[pending_slot].start_sample = now_sample;
        g_seq_live_rec_pending[pending_slot].start_sample_effective = now_sample;
        g_seq_live_rec_pending[pending_slot].occurrence_id = occurrence_id;
    }
}

void seq_live_rec_session_live_rec_note_off(seq_live_rec_source_t source,
                                            uint8_t channel_zero_based,
                                            uint8_t note,
                                            const seq_runtime_state_t *runtime_state,
                                            uint64_t now_sample,
                                            uint32_t occurrence_id)
{
    if ((runtime_state == 0)
        || (seq_live_rec_session_is_live_rec_active() == 0U)
        || (note >= 128U)
        || (source > SEQ_LIVE_REC_SRC_EXTERNAL))
    {
        return;
    }

    for (uint8_t pending_index = 0U; pending_index < 64U; ++pending_index)
    {
        const seq_live_rec_session_pending_note_t *const pending =
            &g_seq_live_rec_pending[pending_index];
        if ((pending->active == 0U)
                || (pending->source != (uint8_t)source)
                || (pending->channel != channel_zero_based)
                || (pending->note != note)
                || ((occurrence_id != 0U)
                    && (pending->occurrence_id != occurrence_id)))
        {
            continue;
        }

        const seq_track_id_t track = pending->track;
        if (track >= SEQ_TRACK_COUNT)
        {
            continue;
        }

        seq_live_rec_session_finalize_slot((int32_t)pending_index,
                                           now_sample,
                                           runtime_state->samples_per_step_q16);
    }
}
