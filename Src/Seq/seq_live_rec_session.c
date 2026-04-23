/*
 * Module: seq_live_rec_session
 * Role: Etat et orchestration live-rec du sequenceur.
 * Responsibilities: arming/count-in/pattern-rec, capture note, ecriture plock
 * live-rec et gestion des transitions de session.
 * Integration: consomme seq_live_rec_capture et seq_edit, sans porter le transport.
 */
#include "Seq/seq_live_rec_session.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/undo_v1.h"
#include "Core/track_runtime.h"

#include "Seq/seq_edit.h"
#include "Seq/seq_live_rec_capture.h"
#include "Seq/seq_output_guard.h"
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
    uint64_t start_sample_quantized;
} seq_live_rec_session_pending_note_t;

SEQ_STATE_D2 static uint8_t g_seq_live_rec_armed;
SEQ_STATE_D2 static uint8_t g_seq_live_rec_count_in_mode;
SEQ_STATE_D2 static uint8_t g_seq_live_rec_len_mode;
SEQ_STATE_D2 static uint8_t g_seq_live_rec_pattern_pending_start;
SEQ_STATE_D2 static uint8_t g_seq_live_rec_pattern_active;
SEQ_STATE_D2 static uint8_t g_seq_live_rec_pattern_target_track;
SEQ_STATE_D2 static uint8_t g_seq_live_rec_pattern_track;
SEQ_STATE_D2 static uint32_t g_seq_live_rec_pattern_steps_remaining;

static seq_live_rec_session_pending_note_t g_seq_live_rec_pending[64U];

static param_id_t seq_live_rec_session_play_param_note(uint8_t voice)
{
    static const param_id_t k_note[4U] = {
        PARAM_SEQ_PLAY_V1_NOTE, PARAM_SEQ_PLAY_V2_NOTE, PARAM_SEQ_PLAY_V3_NOTE, PARAM_SEQ_PLAY_V4_NOTE
    };
    return (voice < 4U) ? k_note[voice] : PARAM_SEQ_PLAY_V1_NOTE;
}

static param_id_t seq_live_rec_session_play_param_vel(uint8_t voice)
{
    static const param_id_t k_vel[4U] = {
        PARAM_SEQ_PLAY_V1_VEL, PARAM_SEQ_PLAY_V2_VEL, PARAM_SEQ_PLAY_V3_VEL, PARAM_SEQ_PLAY_V4_VEL
    };
    return (voice < 4U) ? k_vel[voice] : PARAM_SEQ_PLAY_V1_VEL;
}

static param_id_t seq_live_rec_session_play_param_len(uint8_t voice)
{
    static const param_id_t k_len[4U] = {
        PARAM_SEQ_PLAY_V1_LEN, PARAM_SEQ_PLAY_V2_LEN, PARAM_SEQ_PLAY_V3_LEN, PARAM_SEQ_PLAY_V4_LEN
    };
    return (voice < 4U) ? k_len[voice] : PARAM_SEQ_PLAY_V1_LEN;
}

static param_id_t seq_live_rec_session_play_param_mictim(uint8_t voice)
{
    static const param_id_t k_mictim[4U] = {
        PARAM_SEQ_PLAY_V1_MICTIM, PARAM_SEQ_PLAY_V2_MICTIM, PARAM_SEQ_PLAY_V3_MICTIM, PARAM_SEQ_PLAY_V4_MICTIM
    };
    return (voice < 4U) ? k_mictim[voice] : PARAM_SEQ_PLAY_V1_MICTIM;
}

static uint8_t seq_live_rec_session_is_live_rec_active(void)
{
    if (g_seq_live_rec_len_mode == (uint8_t)SEQ_REC_LEN_MODE_PATTERN)
    {
        return g_seq_live_rec_pattern_active;
    }

    return ((g_seq_live_rec_armed != 0U)
            && (seq_runtime_is_running() != 0U)
            && (seq_runtime_get_rec_count_in_remaining_steps() == 0U)) ? 1U : 0U;
}

static uint8_t seq_live_rec_session_track_accepts_source(seq_track_id_t track,
                                                         seq_live_rec_source_t source)
{
    const track_runtime_midi_source_t track_source = track_runtime_get_midi_source(track);
    if (source == SEQ_LIVE_REC_SRC_INTERNAL)
    {
        return ((track_source == TRACK_RUNTIME_MIDI_SOURCE_INTERNAL)
                || (track_source == TRACK_RUNTIME_MIDI_SOURCE_ALL)) ? 1U : 0U;
    }

    return ((track_source == TRACK_RUNTIME_MIDI_SOURCE_EXTERNAL)
            || (track_source == TRACK_RUNTIME_MIDI_SOURCE_ALL)) ? 1U : 0U;
}

static uint64_t seq_live_rec_session_mictim_positive_offset_q16(int8_t mictim,
                                                                uint32_t samples_per_step_q16)
{
    if ((mictim <= 0) || (samples_per_step_q16 == 0U))
    {
        return 0U;
    }

    return ((uint64_t)(uint32_t)mictim * (uint64_t)samples_per_step_q16) / 96ULL;
}

static uint64_t seq_live_rec_session_compute_quantized_on_sample(seq_step_id_t current_step,
                                                                  seq_step_id_t write_step,
                                                                  int8_t mictim,
                                                                  uint32_t samples_per_step_q16,
                                                                  uint64_t step_sample_q16)
{
    const uint32_t sps_q16 = (samples_per_step_q16 == 0U) ? 1U : samples_per_step_q16;
    uint64_t write_step_sample_q16 = step_sample_q16;
    if (write_step != current_step)
    {
        write_step_sample_q16 += (uint64_t)sps_q16;
    }

    write_step_sample_q16 += seq_live_rec_session_mictim_positive_offset_q16(mictim, sps_q16);
    return write_step_sample_q16 >> 16;
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

static uint8_t seq_live_rec_session_delete_play_param(seq_track_id_t track,
                                                      seq_step_id_t step,
                                                      param_id_t param_id)
{
    uint8_t set_id = 0U;
    seq_param8_t param8 = 0U;
    if (seq_param_iface_map_param(param_id, &set_id, &param8) == 0U)
    {
        return 0U;
    }

    const seq_plock_op_status_t st = seq_model_step_plock_delete(track, step, set_id, param8);
    return (st == SEQ_PLOCK_OP_DELETED) ? 1U : 0U;
}

typedef struct
{
    uint8_t present;
    seq_value16_t value16;
} seq_live_rec_session_saved_param_t;

static uint8_t seq_live_rec_session_read_play_param(seq_track_id_t track,
                                                    seq_step_id_t step,
                                                    param_id_t param_id,
                                                    seq_live_rec_session_saved_param_t *out_saved)
{
    if (out_saved == 0)
    {
        return 0U;
    }

    out_saved->present = 0U;
    out_saved->value16 = 0U;

    uint8_t set_id = 0U;
    seq_param8_t param8 = 0U;
    if (seq_param_iface_map_param(param_id, &set_id, &param8) == 0U)
    {
        return 0U;
    }

    seq_plock_entry_t entry;
    if (seq_model_step_plock_find(track, step, set_id, param8, &entry) == 0U)
    {
        return 1U;
    }

    out_saved->present = 1U;
    out_saved->value16 = entry.value16;
    return 1U;
}

static uint8_t seq_live_rec_session_restore_play_param(seq_track_id_t track,
                                                       seq_step_id_t step,
                                                       param_id_t param_id,
                                                       const seq_live_rec_session_saved_param_t *saved)
{
    if (saved == 0)
    {
        return 0U;
    }

    if (saved->present == 0U)
    {
        (void)seq_live_rec_session_delete_play_param(track, step, param_id);
        return 1U;
    }

    uint8_t set_id = 0U;
    seq_param8_t param8 = 0U;
    if (seq_param_iface_map_param(param_id, &set_id, &param8) == 0U)
    {
        return 0U;
    }

    const seq_plock_op_status_t st = seq_model_step_plock_upsert(track,
                                                                 step,
                                                                 set_id,
                                                                 param8,
                                                                 saved->value16,
                                                                 0U);
    return ((st == SEQ_PLOCK_OP_CREATED) || (st == SEQ_PLOCK_OP_UPDATED)) ? 1U : 0U;
}

static void seq_live_rec_session_clear_voice_play_params(seq_track_id_t track,
                                                        seq_step_id_t step,
                                                        uint8_t voice)
{
    const param_id_t note_param = seq_live_rec_session_play_param_note(voice);
    const param_id_t vel_param = seq_live_rec_session_play_param_vel(voice);
    const param_id_t len_param = seq_live_rec_session_play_param_len(voice);
    const param_id_t mictim_param = seq_live_rec_session_play_param_mictim(voice);

    (void)seq_live_rec_session_delete_play_param(track, step, note_param);
    (void)seq_live_rec_session_delete_play_param(track, step, vel_param);
    (void)seq_live_rec_session_delete_play_param(track, step, len_param);
    (void)seq_live_rec_session_delete_play_param(track, step, mictim_param);
}

static void seq_live_rec_session_normalize_step_for_write(seq_track_id_t track, seq_step_id_t step)
{
    if (seq_model_get_trig(track, step) != 0U)
    {
        return;
    }

    for (uint8_t voice = 0U; voice < 4U; ++voice)
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

    const uint64_t effective_start_sample = pending->start_sample_quantized;
    const uint64_t duration_samples = (stop_sample >= effective_start_sample)
                                            ? (stop_sample - effective_start_sample)
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
                                                 seq_live_rec_session_play_param_len(pending->voice),
                                                 (float)len_steps);
    pending->active = 0U;
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
                                                          uint8_t note)
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
    for (uint8_t voice = 0U; voice < 4U; ++voice)
    {
        uint8_t set_id = 0U;
        seq_param8_t param8 = 0U;
        const param_id_t note_id = seq_live_rec_session_play_param_note(voice);
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

static int32_t seq_live_rec_session_find_free_voice(seq_track_id_t track,
                                                    seq_step_id_t step)
{
    for (uint8_t voice = 0U; voice < 4U; ++voice)
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

    for (uint8_t v = 0U; v < 4U; ++v)
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
    const param_id_t note_id = seq_live_rec_session_play_param_note(voice);
    const param_id_t vel_id = seq_live_rec_session_play_param_vel(voice);
    const param_id_t len_id = seq_live_rec_session_play_param_len(voice);
    const param_id_t mictim_id = seq_live_rec_session_play_param_mictim(voice);
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
    seq_edit_clear_steps(track, steps, length);
    seq_live_rec_capture_reset();
}

static void seq_live_rec_session_pattern_rec_cancel(void)
{
    g_seq_live_rec_pattern_pending_start = 0U;
    g_seq_live_rec_pattern_active = 0U;
    g_seq_live_rec_pattern_steps_remaining = 0U;
}

static void seq_live_rec_session_flush_and_reset(uint64_t stop_sample, uint32_t samples_per_step_q16)
{
    seq_live_rec_capture_flush(stop_sample, samples_per_step_q16);
    seq_live_rec_capture_reset();
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
        seq_live_rec_capture_flush(now_sample, runtime_state->samples_per_step_q16);
        seq_live_rec_capture_reset();
        seq_live_rec_session_pattern_rec_cancel();
        g_seq_live_rec_armed = 0U;
    }
}

void seq_live_rec_session_init(void)
{
    memset(g_seq_live_rec_pending, 0, sizeof(g_seq_live_rec_pending));
    g_seq_live_rec_armed = 0U;
    g_seq_live_rec_count_in_mode = 0U;
    g_seq_live_rec_len_mode = (uint8_t)SEQ_REC_LEN_MODE_OVERDUB;
    g_seq_live_rec_pattern_pending_start = 0U;
    g_seq_live_rec_pattern_active = 0U;
    g_seq_live_rec_pattern_target_track = 0U;
    g_seq_live_rec_pattern_track = 0U;
    g_seq_live_rec_pattern_steps_remaining = 0U;
    seq_edit_init();
    seq_live_rec_capture_init();
}

void seq_live_rec_session_reset_capture(void)
{
    seq_live_rec_capture_reset();
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
    }
}

uint8_t seq_live_rec_session_rec_is_armed(void)
{
    return g_seq_live_rec_armed;
}

void seq_live_rec_session_set_rec_count_in_mode(uint8_t mode)
{
    if (mode > 3U)
    {
        mode = 3U;
    }

    g_seq_live_rec_count_in_mode = mode;
}

uint8_t seq_live_rec_session_get_rec_count_in_mode(void)
{
    return g_seq_live_rec_count_in_mode;
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
                                                      seq_param8_t param8)
{
    if ((track >= SEQ_TRACK_COUNT)
        || (seq_param_iface_is_set_plockable(set_id) == 0U)
        || (seq_live_rec_session_is_live_rec_active() == 0U))
    {
        return 0U;
    }

    track_runtime_refresh_track(track);
    if (seq_param_iface_is_param_supported(track, set_id, param8) == 0U)
    {
        return 0U;
    }

    return (seq_model_get_track_playback_length(track) == 0U) ? 0U : 1U;
}

uint8_t seq_live_rec_session_live_rec_param_write(const seq_runtime_state_t *runtime_state,
                                                  seq_track_id_t track,
                                                  uint8_t set_id,
                                                  seq_param8_t param8,
                                                  seq_value16_t value16)
{
    if ((runtime_state == 0)
        || (seq_live_rec_session_live_rec_param_can_write(track, set_id, param8) == 0U))
    {
        return 0U;
    }

    const uint8_t length = seq_model_get_track_playback_length(track);
    seq_step_id_t step = runtime_state->play_step[track];
    if (step >= length)
    {
        step = 0U;
    }

    undo_v1_begin_gesture((0x40000000UL
                           | ((uint32_t)8U << 24)
                           | ((uint32_t)track << 16)
                           | ((uint32_t)step << 8)
                           | ((uint32_t)set_id ^ (uint32_t)param8)));
    const seq_plock_op_status_t status = seq_edit_step_plock_upsert(track,
                                                                     step,
                                                                     set_id,
                                                                     param8,
                                                                     value16,
                                                                     0U);
    if ((status != SEQ_PLOCK_OP_CREATED) && (status != SEQ_PLOCK_OP_UPDATED))
    {
        return 0U;
    }

    seq_edit_step_plock_commit(track, step, set_id, param8);
    return 1U;
}

void seq_live_rec_session_live_rec_note_on(seq_live_rec_source_t source,
                                           uint8_t channel_zero_based,
                                           uint8_t note,
                                           uint8_t velocity,
                                           const seq_runtime_state_t *runtime_state,
                                           uint64_t now_sample)
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
        && (seq_output_guard_is_note_active_on_channel(channel_zero_based, note) != 0U))
    {
        return;
    }

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        track_runtime_refresh_track(track);
        const uint8_t track_ch = track_runtime_get_midi_channel_zero_based(track);
        if (track_ch != channel_zero_based)
        {
            continue;
        }

        if (seq_live_rec_session_track_accepts_source(track, source) == 0U)
        {
            continue;
        }

        if (track_runtime_get_effective_param_status(track, PARAM_SEQ_PLAY_V1_NOTE) != TRACK_RUNTIME_PARAM_ALLOWED)
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
                                                                                       note);
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

        const param_id_t note_param = seq_live_rec_session_play_param_note((uint8_t)voice);
        const param_id_t vel_param = seq_live_rec_session_play_param_vel((uint8_t)voice);
        const param_id_t len_param = seq_live_rec_session_play_param_len((uint8_t)voice);
        const param_id_t mictim_param = seq_live_rec_session_play_param_mictim((uint8_t)voice);

        seq_live_rec_session_saved_param_t saved_note;
        seq_live_rec_session_saved_param_t saved_vel;
        seq_live_rec_session_saved_param_t saved_len;
        seq_live_rec_session_saved_param_t saved_mictim;
        (void)seq_live_rec_session_read_play_param(track, write_step, note_param, &saved_note);
        (void)seq_live_rec_session_read_play_param(track, write_step, vel_param, &saved_vel);
        (void)seq_live_rec_session_read_play_param(track, write_step, len_param, &saved_len);
        (void)seq_live_rec_session_read_play_param(track, write_step, mictim_param, &saved_mictim);

        seq_live_rec_session_clear_voice_play_params(track, write_step, (uint8_t)voice);

        const uint8_t note_ok = seq_live_rec_session_upsert_play_param(track,
                                                                        write_step,
                                                                        note_param,
                                                                        (float)note);
        const uint8_t vel_ok = seq_live_rec_session_upsert_play_param(track,
                                                                       write_step,
                                                                       vel_param,
                                                                       (float)velocity);
        const uint8_t micro_ok = seq_live_rec_session_upsert_play_param(track,
                                                                         write_step,
                                                                         mictim_param,
                                                                         (float)mictim);
        if ((note_ok == 0U) || (vel_ok == 0U) || (micro_ok == 0U))
        {
            (void)seq_live_rec_session_restore_play_param(track, write_step, note_param, &saved_note);
            (void)seq_live_rec_session_restore_play_param(track, write_step, vel_param, &saved_vel);
            (void)seq_live_rec_session_restore_play_param(track, write_step, len_param, &saved_len);
            (void)seq_live_rec_session_restore_play_param(track, write_step, mictim_param, &saved_mictim);
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
        g_seq_live_rec_pending[pending_slot].start_sample_quantized =
                seq_live_rec_session_compute_quantized_on_sample(step,
                                                                 write_step,
                                                                 mictim,
                                                                 runtime_state->samples_per_step_q16,
                                                                 runtime_state->step_sample_q16);
    }
}

void seq_live_rec_session_live_rec_note_off(seq_live_rec_source_t source,
                                            uint8_t channel_zero_based,
                                            uint8_t note,
                                            const seq_runtime_state_t *runtime_state,
                                            uint64_t now_sample)
{
    if ((runtime_state == 0)
        || (seq_live_rec_session_is_live_rec_active() == 0U)
        || (note >= 128U)
        || (source > SEQ_LIVE_REC_SRC_EXTERNAL))
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

        const int32_t pending_slot = seq_live_rec_session_find_pending_for_note(track,
                                                                                 source,
                                                                                 channel_zero_based,
                                                                                 note);
        if (pending_slot < 0)
        {
            continue;
        }

        seq_live_rec_session_finalize_slot(pending_slot,
                                           now_sample,
                                           runtime_state->samples_per_step_q16);
    }
}
