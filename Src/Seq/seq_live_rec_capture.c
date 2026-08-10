/*
 * Module: seq_live_rec_capture
 * Role: Capture live-record des événements note vers le pattern séquenceur.
 * Responsibilities: suivre note-on/off pendantes, quantifier vers steps,
 * écrire trig/plocks NOTE/VEL/LEN/MICTIM et sécuriser les sorties associées.
 * Integration: piloté par seq_runtime en mode REC; ne remplace pas le scheduler de playback.
 */
#include "Seq/seq_live_rec_capture.h"

#include <string.h>

#include "Core/track_runtime.h"
#include "Core/track_state.h"

#include "Seq/seq_edit.h"
#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_output_guard.h"

#define SEQ_LIVE_REC_CAPTURE_VOICE_COUNT 4U
#define SEQ_LIVE_REC_CAPTURE_PENDING_CAP 64U

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
} seq_live_rec_capture_pending_note_t;

static seq_live_rec_capture_pending_note_t g_seq_live_rec_pending[SEQ_LIVE_REC_CAPTURE_PENDING_CAP];

static param_id_t seq_live_rec_capture_play_param_note(uint8_t voice)
{
    static const param_id_t k_note[SEQ_LIVE_REC_CAPTURE_VOICE_COUNT] = {
        PARAM_SEQ_PLAY_V1_NOTE, PARAM_SEQ_PLAY_V2_NOTE, PARAM_SEQ_PLAY_V3_NOTE, PARAM_SEQ_PLAY_V4_NOTE
    };
    return (voice < SEQ_LIVE_REC_CAPTURE_VOICE_COUNT) ? k_note[voice] : PARAM_SEQ_PLAY_V1_NOTE;
}

static param_id_t seq_live_rec_capture_play_param_vel(uint8_t voice)
{
    static const param_id_t k_vel[SEQ_LIVE_REC_CAPTURE_VOICE_COUNT] = {
        PARAM_SEQ_PLAY_V1_VEL, PARAM_SEQ_PLAY_V2_VEL, PARAM_SEQ_PLAY_V3_VEL, PARAM_SEQ_PLAY_V4_VEL
    };
    return (voice < SEQ_LIVE_REC_CAPTURE_VOICE_COUNT) ? k_vel[voice] : PARAM_SEQ_PLAY_V1_VEL;
}

static param_id_t seq_live_rec_capture_play_param_len(uint8_t voice)
{
    static const param_id_t k_len[SEQ_LIVE_REC_CAPTURE_VOICE_COUNT] = {
        PARAM_SEQ_PLAY_V1_LEN, PARAM_SEQ_PLAY_V2_LEN, PARAM_SEQ_PLAY_V3_LEN, PARAM_SEQ_PLAY_V4_LEN
    };
    return (voice < SEQ_LIVE_REC_CAPTURE_VOICE_COUNT) ? k_len[voice] : PARAM_SEQ_PLAY_V1_LEN;
}

static param_id_t seq_live_rec_capture_play_param_mictim(uint8_t voice)
{
    static const param_id_t k_mictim[SEQ_LIVE_REC_CAPTURE_VOICE_COUNT] = {
        PARAM_SEQ_PLAY_V1_MICTIM, PARAM_SEQ_PLAY_V2_MICTIM, PARAM_SEQ_PLAY_V3_MICTIM, PARAM_SEQ_PLAY_V4_MICTIM
    };
    return (voice < SEQ_LIVE_REC_CAPTURE_VOICE_COUNT) ? k_mictim[voice] : PARAM_SEQ_PLAY_V1_MICTIM;
}

static int32_t seq_live_rec_capture_find_voice_with_note_lock(seq_track_id_t track,
                                                              seq_step_id_t step,
                                                              uint8_t note)
{
    for (uint8_t voice = 0U; voice < SEQ_LIVE_REC_CAPTURE_VOICE_COUNT; ++voice)
    {
        const uint8_t set_id = (uint8_t)SEQ_PLOCK_SET_PLAY;
        seq_param_slot_t param_slot = 0U;
        const param_id_t note_id = seq_live_rec_capture_play_param_note(voice);
        if (seq_param_iface_param_to_slot(track, set_id, note_id, &param_slot) == 0U)
        {
            continue;
        }

        seq_value16_t value16;
        if (seq_edit_step_play_find(track, step, note_id, &value16) == 0U)
        {
            continue;
        }

        const float note_f = seq_param_iface_decode_param_value(note_id, value16);
        const uint8_t existing_note = (uint8_t)(note_f + 0.5f);
        if (existing_note == note)
        {
            return (int32_t)voice;
        }
    }

    return -1;
}

static uint8_t seq_live_rec_capture_voice_has_any_lock(seq_track_id_t track,
                                                        seq_step_id_t step,
                                                        uint8_t voice)
{
    const param_id_t note_id = seq_live_rec_capture_play_param_note(voice);
    const param_id_t vel_id = seq_live_rec_capture_play_param_vel(voice);
    const param_id_t len_id = seq_live_rec_capture_play_param_len(voice);
    const param_id_t mictim_id = seq_live_rec_capture_play_param_mictim(voice);
    const param_id_t params[4] = { note_id, vel_id, len_id, mictim_id };

    for (uint8_t i = 0U; i < 4U; ++i)
    {
        const uint8_t set_id = (uint8_t)SEQ_PLOCK_SET_PLAY;
        seq_param_slot_t param_slot = 0U;
        if (seq_param_iface_param_to_slot(track, set_id, params[i], &param_slot) == 0U)
        {
            continue;
        }

        seq_value16_t value16;
        if (seq_edit_step_play_find(track, step, params[i], &value16) != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

static int32_t seq_live_rec_capture_find_free_voice(seq_track_id_t track,
                                                     seq_step_id_t step)
{
    for (uint8_t voice = 0U; voice < SEQ_LIVE_REC_CAPTURE_VOICE_COUNT; ++voice)
    {
        if (seq_live_rec_capture_voice_has_any_lock(track, step, voice) == 0U)
        {
            return (int32_t)voice;
        }
    }

    return -1;
}

static int32_t seq_live_rec_capture_alloc_pending_slot(void)
{
    for (uint8_t i = 0U; i < SEQ_LIVE_REC_CAPTURE_PENDING_CAP; ++i)
    {
        if (g_seq_live_rec_pending[i].active == 0U)
        {
            return (int32_t)i;
        }
    }

    return -1;
}

static int32_t seq_live_rec_capture_find_pending_for_note(seq_track_id_t track,
                                                           seq_live_rec_source_t source,
                                                           uint8_t channel_zero_based,
                                                           uint8_t note)
{
    for (uint8_t i = 0U; i < SEQ_LIVE_REC_CAPTURE_PENDING_CAP; ++i)
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

static int32_t seq_live_rec_capture_find_pending_for_note_any_source(seq_track_id_t track,
                                                                      uint8_t channel_zero_based,
                                                                      uint8_t note)
{
    for (uint8_t i = 0U; i < SEQ_LIVE_REC_CAPTURE_PENDING_CAP; ++i)
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

static int32_t seq_live_rec_capture_find_pending_for_voice(seq_track_id_t track,
                                                            seq_step_id_t step,
                                                            uint8_t voice)
{
    for (uint8_t i = 0U; i < SEQ_LIVE_REC_CAPTURE_PENDING_CAP; ++i)
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

static uint8_t seq_live_rec_capture_voice_is_owned(seq_track_id_t track,
                                                   seq_step_id_t step,
                                                   uint8_t voice)
{
    return (seq_live_rec_capture_find_pending_for_voice(track, step, voice) >= 0) ? 1U : 0U;
}

static int32_t seq_live_rec_capture_select_voice_deterministic(seq_track_id_t track,
                                                                seq_step_id_t step,
                                                                uint8_t note)
{
    int32_t voice = seq_live_rec_capture_find_voice_with_note_lock(track, step, note);
    if (voice >= 0)
    {
        return voice;
    }

    voice = seq_live_rec_capture_find_free_voice(track, step);
    if (voice >= 0)
    {
        return voice;
    }

    for (uint8_t v = 0U; v < SEQ_LIVE_REC_CAPTURE_VOICE_COUNT; ++v)
    {
        if (seq_live_rec_capture_voice_is_owned(track, step, v) == 0U)
        {
            return (int32_t)v;
        }
    }

    /* Fully occupied and owned: deterministic fallback slot. */
    return 0;
}

static uint8_t seq_live_rec_capture_track_accepts_source(seq_track_id_t track,
                                                          seq_live_rec_source_t source)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return 0U;
    }

    /* Projection read: MIDI source gates capture acceptance; no runtime mutation here. */
    const track_runtime_midi_source_t track_source = track_runtime_get_midi_source(track);
    if (source == SEQ_LIVE_REC_SRC_INTERNAL)
    {
        return ((track_source == TRACK_RUNTIME_MIDI_SOURCE_INTERNAL)
                || (track_source == TRACK_RUNTIME_MIDI_SOURCE_ALL)) ? 1U : 0U;
    }

    return ((track_source == TRACK_RUNTIME_MIDI_SOURCE_EXTERNAL)
            || (track_source == TRACK_RUNTIME_MIDI_SOURCE_ALL)) ? 1U : 0U;
}

static uint64_t seq_live_rec_capture_mictim_positive_offset_q16(int8_t mictim,
                                                                uint32_t samples_per_step_q16)
{
    if ((mictim <= 0) || (samples_per_step_q16 == 0U))
    {
        return 0U;
    }

    return ((uint64_t)(uint32_t)mictim * (uint64_t)samples_per_step_q16) / 96ULL;
}

static uint64_t seq_live_rec_capture_compute_quantized_on_sample(seq_step_id_t current_step,
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

    write_step_sample_q16 += seq_live_rec_capture_mictim_positive_offset_q16(mictim, sps_q16);
    return write_step_sample_q16 >> 16;
}

static void seq_live_rec_capture_compute_step_and_mictim(seq_track_id_t track,
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

static uint8_t seq_live_rec_capture_upsert_play_param(seq_track_id_t track,
                                                       seq_step_id_t step,
                                                       param_id_t param_id,
                                                       float value)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return 0U;
    }

    const uint8_t set_id = (uint8_t)SEQ_PLOCK_SET_PLAY;
    seq_param_slot_t param_slot = 0U;
    if (seq_param_iface_param_to_slot(track, set_id, param_id, &param_slot) == 0U)
    {
        return 0U;
    }

    const seq_value16_t encoded = seq_param_iface_encode_param_value(param_id, value);
    const seq_plock_op_status_t st = seq_edit_step_play_upsert(track, step, param_id, encoded);
    return ((st == SEQ_PLOCK_OP_CREATED) || (st == SEQ_PLOCK_OP_UPDATED)) ? 1U : 0U;
}

static uint8_t seq_live_rec_capture_delete_play_param(seq_track_id_t track,
                                                       seq_step_id_t step,
                                                       param_id_t param_id)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return 0U;
    }

    const uint8_t set_id = (uint8_t)SEQ_PLOCK_SET_PLAY;
    seq_param_slot_t param_slot = 0U;
    if (seq_param_iface_param_to_slot(track, set_id, param_id, &param_slot) == 0U)
    {
        return 0U;
    }

    const seq_plock_op_status_t st = seq_edit_step_play_delete(track, step, param_id);
    return (st == SEQ_PLOCK_OP_DELETED) ? 1U : 0U;
}

typedef struct
{
    uint8_t present;
    seq_value16_t value16;
} seq_live_rec_capture_saved_param_t;

static uint8_t seq_live_rec_capture_read_play_param(seq_track_id_t track,
                                                    seq_step_id_t step,
                                                    param_id_t param_id,
                                                    seq_live_rec_capture_saved_param_t *out_saved)
{
    if (out_saved == 0)
    {
        return 0U;
    }

    out_saved->present = 0U;
    out_saved->value16 = 0U;

    const uint8_t set_id = (uint8_t)SEQ_PLOCK_SET_PLAY;
    seq_param_slot_t param_slot = 0U;
    if (seq_param_iface_param_to_slot(track, set_id, param_id, &param_slot) == 0U)
    {
        return 0U;
    }

    seq_value16_t value16;
    if (seq_edit_step_play_find(track, step, param_id, &value16) == 0U)
    {
        return 1U;
    }

    out_saved->present = 1U;
    out_saved->value16 = value16;
    return 1U;
}

static uint8_t seq_live_rec_capture_restore_play_param(seq_track_id_t track,
                                                       seq_step_id_t step,
                                                       param_id_t param_id,
                                                       const seq_live_rec_capture_saved_param_t *saved)
{
    if ((saved == 0) || (seq_edit_track_sequence_is_locked(track) != 0U))
    {
        return 0U;
    }

    if (saved->present == 0U)
    {
        (void)seq_live_rec_capture_delete_play_param(track, step, param_id);
        return 1U;
    }

    const uint8_t set_id = (uint8_t)SEQ_PLOCK_SET_PLAY;
    seq_param_slot_t param_slot = 0U;
    if (seq_param_iface_param_to_slot(track, set_id, param_id, &param_slot) == 0U)
    {
        return 0U;
    }

    const seq_plock_op_status_t st = seq_edit_step_play_upsert(track, step, param_id, saved->value16);
    return ((st == SEQ_PLOCK_OP_CREATED) || (st == SEQ_PLOCK_OP_UPDATED)) ? 1U : 0U;
}

static void seq_live_rec_capture_clear_voice_play_params(seq_track_id_t track,
                                                         seq_step_id_t step,
                                                         uint8_t voice)
{
    const param_id_t note_param = seq_live_rec_capture_play_param_note(voice);
    const param_id_t vel_param = seq_live_rec_capture_play_param_vel(voice);
    const param_id_t len_param = seq_live_rec_capture_play_param_len(voice);
    const param_id_t mictim_param = seq_live_rec_capture_play_param_mictim(voice);

    (void)seq_live_rec_capture_delete_play_param(track, step, note_param);
    (void)seq_live_rec_capture_delete_play_param(track, step, vel_param);
    (void)seq_live_rec_capture_delete_play_param(track, step, len_param);
    (void)seq_live_rec_capture_delete_play_param(track, step, mictim_param);
}

static void seq_live_rec_capture_normalize_step_for_write(seq_track_id_t track,
                                                          seq_step_id_t step)
{
    /*
     * Local step normalization (live-rec only):
     * if step is detriggered, clear all voice play params so stale history
     * does not pollute deterministic slot selection/rewrite.
     */
    if (seq_model_get_trig(track, step) != 0U)
    {
        return;
    }

    for (uint8_t voice = 0U; voice < SEQ_LIVE_REC_CAPTURE_VOICE_COUNT; ++voice)
    {
        seq_live_rec_capture_clear_voice_play_params(track, step, voice);
    }
}

static void seq_live_rec_capture_finalize_pending(seq_live_rec_capture_pending_note_t *pending,
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

    (void)seq_live_rec_capture_upsert_play_param(pending->track,
                                                  pending->step,
                                                  seq_live_rec_capture_play_param_len(pending->voice),
                                                  (float)len_steps);
    pending->active = 0U;
}

static void seq_live_rec_capture_finalize_slot(int32_t slot,
                                               uint64_t stop_sample,
                                               uint32_t samples_per_step_q16)
{
    if ((slot < 0) || (slot >= (int32_t)SEQ_LIVE_REC_CAPTURE_PENDING_CAP))
    {
        return;
    }

    seq_live_rec_capture_finalize_pending(&g_seq_live_rec_pending[(uint8_t)slot],
                                          stop_sample,
                                          samples_per_step_q16);
}

void seq_live_rec_capture_init(void)
{
    memset(g_seq_live_rec_pending, 0, sizeof(g_seq_live_rec_pending));
}

void seq_live_rec_capture_reset(void)
{
    memset(g_seq_live_rec_pending, 0, sizeof(g_seq_live_rec_pending));
}

void seq_live_rec_capture_flush(uint64_t stop_sample, uint32_t samples_per_step_q16)
{
    for (uint8_t i = 0U; i < SEQ_LIVE_REC_CAPTURE_PENDING_CAP; ++i)
    {
        seq_live_rec_capture_finalize_pending(&g_seq_live_rec_pending[i], stop_sample, samples_per_step_q16);
    }
}

void seq_live_rec_capture_note_on(uint8_t active,
                                  const seq_runtime_state_t *runtime_state,
                                  seq_live_rec_source_t source,
                                  uint8_t channel_zero_based,
                                  uint8_t note,
                                  uint8_t velocity,
                                  uint64_t now_sample)
{
    if ((runtime_state == 0)
        || (active == 0U)
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
        /* Projection read: MIDI channel is a runtime mirror used to route capture. */
        const uint8_t track_ch = track_runtime_get_midi_channel_zero_based(track);
        if (track_ch != channel_zero_based)
        {
            continue;
        }

        if (seq_live_rec_capture_track_accepts_source(track, source) == 0U)
        {
            continue;
        }

        /* Projection read: param status is used as a runtime guard, not recomputed locally. */
        if (track_runtime_get_effective_param_status(track, PARAM_SEQ_PLAY_V1_NOTE) != TRACK_RUNTIME_PARAM_ALLOWED)
        {
            continue;
        }

        const seq_step_id_t step = runtime_state->play_step[track];
        seq_step_id_t write_step = step;
        int8_t mictim = 0;
        seq_live_rec_capture_compute_step_and_mictim(track,
                                                     &write_step,
                                                     &mictim,
                                                     runtime_state->samples_per_step_q16,
                                                     runtime_state->step_sample_q16,
                                                     now_sample);

        seq_live_rec_capture_normalize_step_for_write(track, write_step);

        const int32_t same_source_pending = seq_live_rec_capture_find_pending_for_note(track,
                                                                                        source,
                                                                                        channel_zero_based,
                                                                                        note);
        if (same_source_pending >= 0)
        {
            /*
             * Explicit pending ownership for same source:
             * close previous pending note and let current note-on re-own the slot.
             */
            seq_live_rec_capture_finalize_slot(same_source_pending,
                                               now_sample,
                                               runtime_state->samples_per_step_q16);
        }

        if (seq_live_rec_capture_find_pending_for_note_any_source(track,
                                                                   channel_zero_based,
                                                                   note) >= 0)
        {
            continue;
        }

        int32_t voice = seq_live_rec_capture_select_voice_deterministic(track, write_step, note);
        if (voice < 0)
        {
            continue;
        }

        const int32_t owner_slot = seq_live_rec_capture_find_pending_for_voice(track,
                                                                                write_step,
                                                                                (uint8_t)voice);
        if (owner_slot >= 0)
        {
            /*
             * Deterministic overwrite policy:
             * selected slot is forcibly released before reuse.
             */
            seq_live_rec_capture_finalize_slot(owner_slot,
                                               now_sample,
                                               runtime_state->samples_per_step_q16);
        }

        const int32_t pending_slot = seq_live_rec_capture_alloc_pending_slot();
        if (pending_slot < 0)
        {
            continue;
        }

        const param_id_t note_param = seq_live_rec_capture_play_param_note((uint8_t)voice);
        const param_id_t vel_param = seq_live_rec_capture_play_param_vel((uint8_t)voice);
        const param_id_t len_param = seq_live_rec_capture_play_param_len((uint8_t)voice);
        const param_id_t mictim_param = seq_live_rec_capture_play_param_mictim((uint8_t)voice);

        seq_live_rec_capture_saved_param_t saved_note;
        seq_live_rec_capture_saved_param_t saved_vel;
        seq_live_rec_capture_saved_param_t saved_len;
        seq_live_rec_capture_saved_param_t saved_mictim;
        (void)seq_live_rec_capture_read_play_param(track, write_step, note_param, &saved_note);
        (void)seq_live_rec_capture_read_play_param(track, write_step, vel_param, &saved_vel);
        (void)seq_live_rec_capture_read_play_param(track, write_step, len_param, &saved_len);
        (void)seq_live_rec_capture_read_play_param(track, write_step, mictim_param, &saved_mictim);

        /*
         * Local cleanup of the targeted voice slot before live-rec rewrite.
         * Keep cleanup strictly scoped to NOTE/VEL/LEN/MICTIM on this voice.
         */
        seq_live_rec_capture_clear_voice_play_params(track, write_step, (uint8_t)voice);

        const uint8_t note_ok = seq_live_rec_capture_upsert_play_param(track,
                                                                        write_step,
                                                                        note_param,
                                                                        (float)note);
        const uint8_t vel_ok = seq_live_rec_capture_upsert_play_param(track,
                                                                       write_step,
                                                                       vel_param,
                                                                       (float)velocity);
        const uint8_t micro_ok = seq_live_rec_capture_upsert_play_param(track,
                                                                         write_step,
                                                                         mictim_param,
                                                                         (float)mictim);
        if ((note_ok == 0U) || (vel_ok == 0U) || (micro_ok == 0U))
        {
            (void)seq_live_rec_capture_restore_play_param(track, write_step, note_param, &saved_note);
            (void)seq_live_rec_capture_restore_play_param(track, write_step, vel_param, &saved_vel);
            (void)seq_live_rec_capture_restore_play_param(track, write_step, len_param, &saved_len);
            (void)seq_live_rec_capture_restore_play_param(track, write_step, mictim_param, &saved_mictim);
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
                seq_live_rec_capture_compute_quantized_on_sample(step,
                                                                 write_step,
                                                                 mictim,
                                                                 runtime_state->samples_per_step_q16,
                                                                 runtime_state->step_sample_q16);
    }
}

void seq_live_rec_capture_note_off(uint8_t active,
                                   const seq_runtime_state_t *runtime_state,
                                   seq_live_rec_source_t source,
                                   uint8_t channel_zero_based,
                                   uint8_t note,
                                   uint64_t now_sample)
{
    if ((runtime_state == 0)
        || (active == 0U)
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

        if (seq_live_rec_capture_track_accepts_source(track, source) == 0U)
        {
            continue;
        }

        const int32_t pending_slot = seq_live_rec_capture_find_pending_for_note(track,
                                                                                 source,
                                                                                 channel_zero_based,
                                                                                 note);
        const int32_t slot_to_close = pending_slot;

        if (slot_to_close < 0)
        {
            continue;
        }
        seq_live_rec_capture_finalize_slot(slot_to_close,
                                           now_sample,
                                           runtime_state->samples_per_step_q16);
    }
}

