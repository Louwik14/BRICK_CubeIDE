#include "Seq/seq_play_scheduler.h"

#include <stdint.h>

#include "Core/track_runtime.h"
#include "Audio/microdexed_synth.h"
#include "Audio/monob_synth.h"
#include "param_registry.h"
#include "midi.h"
#include "ui_core.h"

#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_output_guard.h"

#define SEQ_PLAY_SCHEDULER_VOICE_COUNT 4U
#define SEQ_PLAY_SCHEDULER_EVENT_CAP 64U

typedef enum
{
    SEQ_PLAY_SCHEDULER_EVT_NOTE_ON = 0,
    SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF
} seq_play_scheduler_evt_type_t;

typedef struct
{
    uint32_t due_tick;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint8_t type;
} seq_play_scheduler_evt_t;

static seq_play_scheduler_evt_t g_seq_play_events[SEQ_PLAY_SCHEDULER_EVENT_CAP];
static uint8_t g_seq_play_event_count;

static void seq_play_scheduler_push(uint32_t due_tick,
                                    uint8_t type,
                                    seq_track_id_t track,
                                    uint8_t note,
                                    uint8_t velocity)
{
    if (g_seq_play_event_count >= SEQ_PLAY_SCHEDULER_EVENT_CAP)
    {
        return;
    }

    seq_play_scheduler_evt_t *const evt = &g_seq_play_events[g_seq_play_event_count++];
    evt->due_tick = due_tick;
    evt->type = type;
    evt->track = track;
    evt->note = note;
    evt->velocity = velocity;
}

static param_id_t seq_play_scheduler_param_note(uint8_t voice)
{
    static const param_id_t k_note[SEQ_PLAY_SCHEDULER_VOICE_COUNT] = {
        PARAM_SEQ_PLAY_V1_NOTE, PARAM_SEQ_PLAY_V2_NOTE, PARAM_SEQ_PLAY_V3_NOTE, PARAM_SEQ_PLAY_V4_NOTE
    };
    return (voice < SEQ_PLAY_SCHEDULER_VOICE_COUNT) ? k_note[voice] : PARAM_SEQ_PLAY_V1_NOTE;
}

static param_id_t seq_play_scheduler_param_vel(uint8_t voice)
{
    static const param_id_t k_vel[SEQ_PLAY_SCHEDULER_VOICE_COUNT] = {
        PARAM_SEQ_PLAY_V1_VEL, PARAM_SEQ_PLAY_V2_VEL, PARAM_SEQ_PLAY_V3_VEL, PARAM_SEQ_PLAY_V4_VEL
    };
    return (voice < SEQ_PLAY_SCHEDULER_VOICE_COUNT) ? k_vel[voice] : PARAM_SEQ_PLAY_V1_VEL;
}

static param_id_t seq_play_scheduler_param_len(uint8_t voice)
{
    static const param_id_t k_len[SEQ_PLAY_SCHEDULER_VOICE_COUNT] = {
        PARAM_SEQ_PLAY_V1_LEN, PARAM_SEQ_PLAY_V2_LEN, PARAM_SEQ_PLAY_V3_LEN, PARAM_SEQ_PLAY_V4_LEN
    };
    return (voice < SEQ_PLAY_SCHEDULER_VOICE_COUNT) ? k_len[voice] : PARAM_SEQ_PLAY_V1_LEN;
}

static param_id_t seq_play_scheduler_param_mictim(uint8_t voice)
{
    static const param_id_t k_mictim[SEQ_PLAY_SCHEDULER_VOICE_COUNT] = {
        PARAM_SEQ_PLAY_V1_MICTIM, PARAM_SEQ_PLAY_V2_MICTIM, PARAM_SEQ_PLAY_V3_MICTIM, PARAM_SEQ_PLAY_V4_MICTIM
    };
    return (voice < SEQ_PLAY_SCHEDULER_VOICE_COUNT) ? k_mictim[voice] : PARAM_SEQ_PLAY_V1_MICTIM;
}

static seq_value16_t seq_play_scheduler_get_locked_or_default(seq_track_id_t track,
                                                              seq_step_id_t step,
                                                              param_id_t param_id)
{
    uint8_t set_id = 0U;
    seq_param8_t param8 = 0U;
    if (seq_param_iface_map_param(param_id, &set_id, &param8) == 0U)
    {
        return seq_param_iface_encode_param_value(param_id, param_registry[param_id].default_value);
    }

    seq_plock_entry_t entry;
    if (seq_model_step_plock_find(track, step, set_id, param8, &entry) != 0U)
    {
        return entry.value16;
    }

    return seq_param_iface_encode_param_value(param_id, param_registry[param_id].default_value);
}

void seq_play_scheduler_init(void)
{
    g_seq_play_event_count = 0U;
}

void seq_play_scheduler_clear(void)
{
    g_seq_play_event_count = 0U;
}

void seq_play_scheduler_schedule_step(seq_track_id_t track,
                                      seq_step_id_t step,
                                      uint16_t ticks_per_step,
                                      uint32_t step_tick)
{
    if (seq_model_get_trig(track, step) == 0U)
    {
        return;
    }

    const track_runtime_param_status_t play_status =
            track_runtime_get_effective_param_status(track, PARAM_SEQ_PLAY_V1_NOTE);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == 0) || (play_status == TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL))
    {
        return;
    }

    const float tps_f = (ticks_per_step == 0U) ? 1.0f : (float)ticks_per_step;

    for (uint8_t voice = 0U; voice < SEQ_PLAY_SCHEDULER_VOICE_COUNT; ++voice)
    {
        const param_id_t note_id = seq_play_scheduler_param_note(voice);
        const param_id_t vel_id = seq_play_scheduler_param_vel(voice);
        const param_id_t len_id = seq_play_scheduler_param_len(voice);
        const param_id_t mictim_id = seq_play_scheduler_param_mictim(voice);

        const float vel_f = seq_param_iface_decode_param_value(vel_id,
                                                               seq_play_scheduler_get_locked_or_default(track, step, vel_id));
        const uint8_t vel = (uint8_t)(vel_f + 0.5f);
        if (vel == 0U)
        {
            continue;
        }

        const float note_f = seq_param_iface_decode_param_value(note_id,
                                                                seq_play_scheduler_get_locked_or_default(track, step, note_id));
        const uint8_t note = (uint8_t)(note_f + 0.5f);
        if (note >= 128U)
        {
            continue;
        }

        const float len_f = seq_param_iface_decode_param_value(len_id,
                                                               seq_play_scheduler_get_locked_or_default(track, step, len_id));
        float len_steps_f = len_f;
        if (len_steps_f < 1.0f)
        {
            len_steps_f = 1.0f;
        }
        if (len_steps_f > 64.0f)
        {
            len_steps_f = 64.0f;
        }
        const uint32_t len_ticks = (uint32_t)(len_steps_f * tps_f);

        const float mictim_f = seq_param_iface_decode_param_value(mictim_id,
                                                                  seq_play_scheduler_get_locked_or_default(track, step, mictim_id));
        int32_t on_tick = (int32_t)step_tick + (int32_t)((mictim_f * tps_f) / 96.0f);
        if (on_tick < (int32_t)step_tick)
        {
            on_tick = (int32_t)step_tick;
        }

        seq_play_scheduler_push((uint32_t)on_tick,
                                (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON,
                                track,
                                note,
                                vel);
        seq_play_scheduler_push((uint32_t)on_tick + ((len_ticks == 0U) ? 1U : len_ticks),
                                (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF,
                                track,
                                note,
                                0U);
    }
}

void seq_play_scheduler_service(uint32_t now_tick, uint8_t running)
{
    uint8_t i = 0U;

    while (i < g_seq_play_event_count)
    {
        seq_play_scheduler_evt_t *const evt = &g_seq_play_events[i];
        if ((int32_t)(now_tick - evt->due_tick) < 0)
        {
            i++;
            continue;
        }

        const uint8_t channel_1_16 = ui_get_track_midi_channel(evt->track);
        const uint8_t channel = (uint8_t)((channel_1_16 > 0U) ? (channel_1_16 - 1U) : 0U);

        if ((evt->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON) && (running == 0U))
        {
            /* STOP guard: block NOTE ON when transport is stopped. */
        }
        else if (evt->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON)
        {
            midi_note_on(MIDI_DEST_BOTH, channel, evt->note, evt->velocity);
            seq_output_guard_note_on_seen(evt->track, evt->note);
        }
        else
        {
            midi_note_off(MIDI_DEST_BOTH, channel, evt->note, 0U);
            seq_output_guard_note_off_seen(evt->track, evt->note);
        }

        track_runtime_refresh_track(evt->track);
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(evt->track);
        if ((ctx != NULL) && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND))
        {
            if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_MONOB)
            {
                if (evt->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON)
                {
                    monob_synth_note_on_for_instance(ctx->instance_id, evt->note, evt->velocity);
                }
                else
                {
                    monob_synth_note_off_for_instance(ctx->instance_id, evt->note);
                }
            }
            else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7)
            {
                if (evt->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON)
                {
                    microdexed_synth_note_on(evt->note, evt->velocity);
                }
                else
                {
                    microdexed_synth_note_off(evt->note);
                }
            }
        }

        for (uint8_t j = i + 1U; j < g_seq_play_event_count; ++j)
        {
            g_seq_play_events[j - 1U] = g_seq_play_events[j];
        }
        g_seq_play_event_count--;
    }
}
