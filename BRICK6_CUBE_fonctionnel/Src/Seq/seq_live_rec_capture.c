#include "Seq/seq_live_rec_capture.h"

#include <string.h>

#include "Core/track_runtime.h"
#include "ui_core.h"

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
    uint32_t start_tick;
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
        uint8_t set_id = 0U;
        seq_param8_t param8 = 0U;
        const param_id_t note_id = seq_live_rec_capture_play_param_note(voice);
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
    int32_t best = -1;
    uint32_t best_tick = 0U;

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

        if ((best < 0) || (g_seq_live_rec_pending[i].start_tick >= best_tick))
        {
            best = (int32_t)i;
            best_tick = g_seq_live_rec_pending[i].start_tick;
        }
    }

    return best;
}

static uint8_t seq_live_rec_capture_track_accepts_source(seq_track_id_t track,
                                                          seq_live_rec_source_t source)
{
    const ui_track_midi_source_t track_source = ui_get_track_midi_source(track);
    if (source == SEQ_LIVE_REC_SRC_INTERNAL)
    {
        return ((track_source == UI_TRACK_MIDI_SRC_INT) || (track_source == UI_TRACK_MIDI_SRC_ALL)) ? 1U : 0U;
    }

    return ((track_source == UI_TRACK_MIDI_SRC_EXT) || (track_source == UI_TRACK_MIDI_SRC_ALL)) ? 1U : 0U;
}

static void seq_live_rec_capture_compute_step_and_mictim(seq_track_id_t track,
                                                          seq_step_id_t *io_step,
                                                          int8_t *out_mictim,
                                                          uint16_t ticks_per_step,
                                                          uint32_t tick_accum)
{
    if ((io_step == 0) || (out_mictim == 0))
    {
        return;
    }

    const uint16_t tps = (ticks_per_step == 0U) ? 1U : ticks_per_step;
    const uint32_t offset_ticks = tick_accum;
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

static uint8_t seq_live_rec_capture_upsert_play_param(seq_track_id_t track,
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

static void seq_live_rec_capture_finalize_pending(seq_live_rec_capture_pending_note_t *pending,
                                                  uint32_t stop_tick,
                                                  uint16_t ticks_per_step)
{
    if ((pending == 0) || (pending->active == 0U))
    {
        return;
    }

    const uint32_t duration_ticks = (stop_tick >= pending->start_tick) ? (stop_tick - pending->start_tick) : 0U;
    const uint16_t tps = (ticks_per_step == 0U) ? 1U : ticks_per_step;
    uint32_t len_steps = (duration_ticks + (uint32_t)tps - 1U) / (uint32_t)tps;
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

void seq_live_rec_capture_init(void)
{
    memset(g_seq_live_rec_pending, 0, sizeof(g_seq_live_rec_pending));
}

void seq_live_rec_capture_reset(void)
{
    memset(g_seq_live_rec_pending, 0, sizeof(g_seq_live_rec_pending));
}

void seq_live_rec_capture_flush(uint32_t stop_tick, uint16_t ticks_per_step)
{
    for (uint8_t i = 0U; i < SEQ_LIVE_REC_CAPTURE_PENDING_CAP; ++i)
    {
        seq_live_rec_capture_finalize_pending(&g_seq_live_rec_pending[i], stop_tick, ticks_per_step);
    }
}

void seq_live_rec_capture_note_on(uint8_t active,
                                  const seq_runtime_state_t *runtime_state,
                                  seq_live_rec_source_t source,
                                  uint8_t channel_zero_based,
                                  uint8_t note,
                                  uint8_t velocity,
                                  uint32_t now_tick)
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
        const uint8_t track_ch_1_16 = ui_get_track_midi_channel(track);
        const uint8_t track_ch = (uint8_t)((track_ch_1_16 > 0U) ? (track_ch_1_16 - 1U) : 0U);
        if (track_ch != channel_zero_based)
        {
            continue;
        }

        if (seq_live_rec_capture_track_accepts_source(track, source) == 0U)
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
        seq_live_rec_capture_compute_step_and_mictim(track,
                                                     &write_step,
                                                     &mictim,
                                                     runtime_state->ticks_per_step,
                                                     runtime_state->tick_accum);

        int32_t voice = seq_live_rec_capture_find_voice_with_note_lock(track, write_step, note);
        if (voice < 0)
        {
            voice = seq_live_rec_capture_find_free_voice(track, write_step);
        }
        if (voice < 0)
        {
            continue;
        }

        const int32_t pending_slot = seq_live_rec_capture_alloc_pending_slot();
        if (pending_slot < 0)
        {
            continue;
        }

        const uint8_t note_ok = seq_live_rec_capture_upsert_play_param(track,
                                                                        write_step,
                                                                        seq_live_rec_capture_play_param_note((uint8_t)voice),
                                                                        (float)note);
        const uint8_t vel_ok = seq_live_rec_capture_upsert_play_param(track,
                                                                       write_step,
                                                                       seq_live_rec_capture_play_param_vel((uint8_t)voice),
                                                                       (float)velocity);
        const uint8_t micro_ok = seq_live_rec_capture_upsert_play_param(track,
                                                                         write_step,
                                                                         seq_live_rec_capture_play_param_mictim((uint8_t)voice),
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
        g_seq_live_rec_pending[pending_slot].start_tick = now_tick;
    }
}

void seq_live_rec_capture_note_off(uint8_t active,
                                   const seq_runtime_state_t *runtime_state,
                                   seq_live_rec_source_t source,
                                   uint8_t channel_zero_based,
                                   uint8_t note,
                                   uint32_t now_tick)
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
        const uint8_t track_ch_1_16 = ui_get_track_midi_channel(track);
        const uint8_t track_ch = (uint8_t)((track_ch_1_16 > 0U) ? (track_ch_1_16 - 1U) : 0U);
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
        if (pending_slot < 0)
        {
            continue;
        }

        seq_live_rec_capture_finalize_pending(&g_seq_live_rec_pending[pending_slot],
                                              now_tick,
                                              runtime_state->ticks_per_step);
    }
}
