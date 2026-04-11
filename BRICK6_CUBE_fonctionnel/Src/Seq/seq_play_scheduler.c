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
#include "stm32h7xx_hal.h"
#include "Core/track_runtime.h"
#include "Audio/microdexed_synth.h"
#include "Audio/monob_synth.h"
#include "Audio/drum_synth.h"
#include "Audio/tb3_synth.h"
#include "Audio/mixer.h"
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
    uint64_t due_sample_time;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint8_t type;
    uint8_t midi_dispatched;
    uint8_t audio_dispatched;
} seq_play_scheduler_evt_t;

static seq_play_scheduler_evt_t g_seq_play_events[SEQ_PLAY_SCHEDULER_EVENT_CAP];
static uint8_t g_seq_play_event_count;

static uint32_t seq_play_scheduler_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void seq_play_scheduler_exit_critical(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}


static void seq_play_scheduler_push(uint32_t due_tick,
                                    uint64_t due_sample_time,
                                    uint8_t type,
                                    seq_track_id_t track,
                                    uint8_t note,
                                    uint8_t velocity)
{
    const uint32_t primask = seq_play_scheduler_enter_critical();
    if (g_seq_play_event_count >= SEQ_PLAY_SCHEDULER_EVENT_CAP)
    {
        seq_play_scheduler_exit_critical(primask);
        return;
    }

    seq_play_scheduler_evt_t *const evt = &g_seq_play_events[g_seq_play_event_count++];
    evt->due_tick = due_tick;
    evt->due_sample_time = due_sample_time;
    evt->type = type;
    evt->track = track;
    evt->note = note;
    evt->velocity = velocity;
    evt->midi_dispatched = 0U;
    evt->audio_dispatched = 0U;
    seq_play_scheduler_exit_critical(primask);
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

static void seq_play_scheduler_emit_engine_note(seq_track_id_t track,
                                                uint8_t note,
                                                uint8_t velocity,
                                                uint8_t is_note_on)
{
    uint8_t filter_track = 0U;
    uint8_t mix_track = 0U;
    if (track_runtime_resolve_filter_target_track(track, &filter_track) != 0U)
    {
        if (is_note_on != 0U)
        {
            mixer_track_filter_note_on(filter_track, note, velocity);
        }
        else
        {
            mixer_track_filter_note_off(filter_track, note);
        }
    }
    if (track_runtime_get_mix_target_track(track, &mix_track) != 0U)
    {
        if (is_note_on != 0U)
        {
            mixer_track_vca_note_on(mix_track, note, velocity);
        }
        else
        {
            mixer_track_vca_note_off(mix_track, note);
        }
    }

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
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
    {
        if (is_note_on != 0U)
        {
            drum_synth_note_on_for_instance(ctx->instance_id, note, velocity);
        }
        else
        {
            drum_synth_note_off_for_instance(ctx->instance_id, note);
        }
    }
}

static void seq_play_scheduler_emit_midi_note(const seq_play_scheduler_audio_event_t *event)
{
    if (event == NULL)
    {
        return;
    }

    const uint8_t channel_1_16 = ui_get_track_midi_channel(event->track);
    const uint8_t channel = (uint8_t)((channel_1_16 > 0U) ? (channel_1_16 - 1U) : 0U);
    const uint8_t is_note_on = (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON) ? 1U : 0U;

    if (is_note_on != 0U)
    {
        if (seq_output_guard_is_note_active_on_track(event->track, event->note) != 0U)
        {
            midi_note_off(MIDI_DEST_BOTH, channel, event->note, 0U);
            seq_output_guard_note_off_seen(event->track, event->note);
        }

        midi_note_on(MIDI_DEST_BOTH, channel, event->note, event->velocity);
        seq_output_guard_note_on_seen(event->track, event->note);
        return;
    }

    midi_note_off(MIDI_DEST_BOTH, channel, event->note, 0U);
    seq_output_guard_note_off_seen(event->track, event->note);
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
                                      uint32_t step_tick,
                                      uint64_t step_sample_time,
                                      uint32_t samples_per_step_q16)
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
    const float samples_per_step_f = ((float)samples_per_step_q16) / 65536.0f;

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
        int32_t microtiming_samples = (int32_t)((mictim_f * samples_per_step_f) / 96.0f);
        if (microtiming_samples < 0)
        {
            microtiming_samples = 0;
        }
        uint64_t note_on_sample_time = step_sample_time + (uint64_t)microtiming_samples;

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
        uint64_t len_samples = (uint64_t)((len_steps_f * samples_per_step_f) + 0.5f);
        if (len_samples == 0U)
        {
            len_samples = 1U;
        }
        uint64_t note_off_sample_time = note_on_sample_time + len_samples;

        seq_play_scheduler_push(note_on_due_tick,
                                note_on_sample_time,
                                (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON,
                                track,
                                note,
                                vel);
        seq_play_scheduler_push(note_off_due_tick,
                                note_off_sample_time,
                                (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF,
                                track,
                                note,
                                0U);
    }
}

void seq_play_scheduler_service(uint32_t now_tick, uint8_t running)
{
    (void)now_tick;
    (void)running;
}

uint16_t seq_play_scheduler_audio_collect_block_events(seq_play_scheduler_audio_event_t *out_events,
                                                       uint16_t max_events,
                                                       uint16_t block_frames,
                                                       uint64_t block_start_sample)
{
    if ((out_events == NULL) || (max_events == 0U))
    {
        return 0U;
    }

    if (block_frames == 0U)
    {
        block_frames = 1U;
    }

    const uint64_t block_end_sample = block_start_sample + (uint64_t)block_frames;
    const uint32_t primask = seq_play_scheduler_enter_critical();

    uint16_t count = 0U;
    while (count < max_events)
    {
        uint8_t selected_index = 0xFFU;
        uint64_t selected_sample = 0U;

        for (uint8_t i = 0U; i < g_seq_play_event_count; ++i)
        {
            const seq_play_scheduler_evt_t *const candidate = &g_seq_play_events[i];
            if (candidate->audio_dispatched != 0U)
            {
                continue;
            }
            if ((candidate->due_sample_time < block_start_sample)
                || (candidate->due_sample_time >= block_end_sample))
            {
                continue;
            }

            if ((selected_index == 0xFFU)
                || (candidate->due_sample_time < selected_sample)
                || ((candidate->due_sample_time == selected_sample)
                    && (candidate->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF)
                    && (g_seq_play_events[selected_index].type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON)))
            {
                selected_index = i;
                selected_sample = candidate->due_sample_time;
            }
        }

        if (selected_index == 0xFFU)
        {
            break;
        }

        const seq_play_scheduler_evt_t evt = g_seq_play_events[selected_index];
        seq_play_scheduler_audio_event_t out_evt;
        out_evt.type = evt.type;
        out_evt.track = evt.track;
        out_evt.note = evt.note;
        out_evt.velocity = evt.velocity;
        out_evt.sample_offset_in_block = (uint16_t)(evt.due_sample_time - block_start_sample);
        if (out_evt.sample_offset_in_block >= block_frames)
        {
            out_evt.sample_offset_in_block = (uint16_t)(block_frames - 1U);
        }
        out_events[count++] = out_evt;

        g_seq_play_events[selected_index].audio_dispatched = 1U;
        g_seq_play_events[selected_index].midi_dispatched = 1U;
    }

    uint8_t write = 0U;
    for (uint8_t read = 0U; read < g_seq_play_event_count; ++read)
    {
        const seq_play_scheduler_evt_t evt = g_seq_play_events[read];
        if ((evt.audio_dispatched != 0U) && (evt.midi_dispatched != 0U))
        {
            continue;
        }
        if (write != read)
        {
            g_seq_play_events[write] = evt;
        }
        write++;
    }
    g_seq_play_event_count = write;
    seq_play_scheduler_exit_critical(primask);

    return count;
}

void seq_play_scheduler_audio_apply_event(const seq_play_scheduler_audio_event_t *event)
{
    if (event == NULL)
    {
        return;
    }

    const uint8_t is_note_on = (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON) ? 1U : 0U;
    seq_play_scheduler_emit_midi_note(event);
    seq_play_scheduler_emit_engine_note((seq_track_id_t)event->track,
                                        event->note,
                                        event->velocity,
                                        is_note_on);
}
