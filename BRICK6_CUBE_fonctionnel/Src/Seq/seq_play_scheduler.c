/*
 * Module: seq_play_scheduler
 * Role: Scheduler PLAY des événements note-on/note-off par tick moteur.
 * Responsibilities: lire steps/plocks, dériver voix note/vel/len/mictim,
 * planifier et émettre les événements vers moteurs audio/MIDI avec output guard.
 * Integration: exécuté par seq_runtime à chaque step/tick; hors transport FSM/clock source.
 */
#define SEQ_PLAY_SCHEDULER_IMPLEMENTATION 1
#include "Seq/seq_play_scheduler.h"

#include <stdint.h>

#include "Core/track_runtime.h"
#include "Audio/microdexed_synth.h"
#include "Audio/monob_synth.h"
#include "Audio/tb3_synth.h"
#include "param_registry.h"
#include "midi.h"
#include "ui_core.h"

#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_output_guard.h"
#include "Seq/seq_runtime.h"

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

static uint8_t seq_play_scheduler_has_note_off_due_at(uint32_t due_tick,
                                                      seq_track_id_t track,
                                                      uint8_t note)
{
    for (uint8_t i = 0U; i < g_seq_play_event_count; ++i)
    {
        const seq_play_scheduler_evt_t *const evt = &g_seq_play_events[i];
        if ((evt->type != (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF)
            || (evt->due_tick != due_tick)
            || (evt->note != note)
            || (evt->track != track))
        {
            continue;
        }

        return 1U;
    }

    return 0U;
}

static void seq_play_scheduler_emit_engine_note(seq_track_id_t track,
                                                uint8_t note,
                                                uint8_t velocity,
                                                uint8_t is_note_on)
{
    track_runtime_refresh_track(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return;
    }

    if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_MONOB)
    {
        if (is_note_on != 0U)
        {
            monob_synth_note_on_for_instance(ctx->instance_id, note, velocity);
        }
        else
        {
            monob_synth_note_off_for_instance(ctx->instance_id, note);
        }
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7)
    {
        if (is_note_on != 0U)
        {
            microdexed_synth_note_on(note, velocity);
        }
        else
        {
            microdexed_synth_note_off(note);
        }
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_TB3)
    {
        if (is_note_on != 0U)
        {
            tb3_synth_note_on_for_instance(ctx->instance_id, note, velocity);
        }
        else
        {
            tb3_synth_note_off_for_instance(ctx->instance_id, note);
        }
    }
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

    track_runtime_refresh_track(track);
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
        if (track_runtime_get_effective_param_status(track, note_id) == TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL)
        {
            continue;
        }

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
        uint32_t len_ticks = (uint32_t)((len_steps_f * tps_f) + 0.5f);
        if (len_ticks == 0U)
        {
            len_ticks = 1U;
        }

        const float mictim_f = seq_param_iface_decode_param_value(mictim_id,
                                                                  seq_play_scheduler_get_locked_or_default(track, step, mictim_id));
        int32_t on_tick = (int32_t)step_tick + (int32_t)((mictim_f * tps_f) / 96.0f);
        if (on_tick < (int32_t)step_tick)
        {
            on_tick = (int32_t)step_tick;
        }

        uint32_t note_on_due_tick = (uint32_t)on_tick;

        /*
         * Adjacent same-note retrigger guard:
         * if a NOTE_OFF for the same MIDI key (channel+note) is already due
         * on this exact tick, delay NOTE_ON by 1 tick to avoid zero-gap OFF/ON
         * collapse on some sinks/engines.
         */
        (void)track;

        uint32_t note_off_due_tick = note_on_due_tick + len_ticks;
        if (note_off_due_tick <= note_on_due_tick)
        {
            note_off_due_tick = note_on_due_tick + 1U;
        }

        seq_play_scheduler_push(note_on_due_tick,
                                (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON,
                                track,
                                note,
                                vel);
        seq_play_scheduler_push(note_off_due_tick,
                                (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF,
                                track,
                                note,
                                0U);
    }
}

void seq_play_scheduler_service(uint32_t now_tick, uint8_t running)
{
    while (g_seq_play_event_count > 0U)
    {
        uint8_t selected_index = 0xFFU;
        uint32_t selected_due_tick = 0U;

        for (uint8_t i = 0U; i < g_seq_play_event_count; ++i)
        {
            const seq_play_scheduler_evt_t *const candidate = &g_seq_play_events[i];
            if ((int32_t)(now_tick - candidate->due_tick) < 0)
            {
                continue;
            }

            if (selected_index == 0xFFU)
            {
                selected_index = i;
                selected_due_tick = candidate->due_tick;
                continue;
            }

            const seq_play_scheduler_evt_t *const selected = &g_seq_play_events[selected_index];
            if (candidate->due_tick < selected_due_tick)
            {
                selected_index = i;
                selected_due_tick = candidate->due_tick;
                continue;
            }

            if ((candidate->due_tick == selected_due_tick)
                && (candidate->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF)
                && (selected->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON))
            {
                selected_index = i;
            }
        }

        if (selected_index == 0xFFU)
        {
            break;
        }

        seq_play_scheduler_evt_t *const evt = &g_seq_play_events[selected_index];
        const uint8_t channel_1_16 = ui_get_track_midi_channel(evt->track);
        const uint8_t channel = (uint8_t)((channel_1_16 > 0U) ? (channel_1_16 - 1U) : 0U);
        const uint8_t emit_note_on = ((evt->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON) && (running != 0U)) ? 1U : 0U;
        const uint8_t emit_note_off = (evt->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF) ? 1U : 0U;

        if (emit_note_on != 0U)
        {
            /*
             * Retrigger correctness for long notes across pattern wrap:
             * if this track still owns the same note at NOTE_ON time, force
             * a NOTE_OFF first (unless one is already due at this exact tick).
             */
            if ((seq_output_guard_is_note_active_on_track(evt->track, evt->note) != 0U)
                && (seq_play_scheduler_has_note_off_due_at(evt->due_tick, evt->track, evt->note) == 0U))
            {
                midi_note_off(MIDI_DEST_BOTH, channel, evt->note, 0U);
                seq_output_guard_note_off_seen(evt->track, evt->note);
                seq_play_scheduler_emit_engine_note(evt->track, evt->note, 0U, 0U);
            }

            midi_note_on(MIDI_DEST_BOTH, channel, evt->note, evt->velocity);
            seq_output_guard_note_on_seen(evt->track, evt->note);
            seq_play_scheduler_emit_engine_note(evt->track, evt->note, evt->velocity, 1U);
        }
        else if (emit_note_off != 0U)
        {
            midi_note_off(MIDI_DEST_BOTH, channel, evt->note, 0U);
            seq_output_guard_note_off_seen(evt->track, evt->note);
            seq_play_scheduler_emit_engine_note(evt->track, evt->note, 0U, 0U);
        }
        else
        {
            /* STOP guard: drop NOTE ON when transport is stopped. */
        }

        for (uint8_t j = selected_index + 1U; j < g_seq_play_event_count; ++j)
        {
            g_seq_play_events[j - 1U] = g_seq_play_events[j];
        }
        g_seq_play_event_count--;
    }
}
