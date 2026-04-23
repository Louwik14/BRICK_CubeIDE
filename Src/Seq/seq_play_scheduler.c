/*
 * Module: seq_play_scheduler
 * Role: Scheduler PLAY des événements note-on/note-off en sample-domain.
 * Responsibilities: lire steps/plocks, dériver voix note/vel/len/mictim,
 * planifier des événements horodatés en samples et les appliquer via le chemin audio.
 * Integration: scheduling déclenché par seq_runtime aux boundaries de step, collecte/apply en IRQ audio.
 */
#define SEQ_PLAY_SCHEDULER_IMPLEMENTATION 1
#include "Seq/seq_play_scheduler.h"

#include <stdint.h>
#include "stm32h7xx_hal.h"
#include "Core/track_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Audio/drum_synth.h"
#include "Audio/mixer.h"
#include "param_registry.h"
#include "midi.h"

#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_output_guard.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"

#define SEQ_PLAY_SCHEDULER_VOICE_COUNT 4U
#define SEQ_PLAY_SCHEDULER_EVENT_CAP 256U



typedef enum
{
    SEQ_PLAY_SCHEDULER_EVT_NOTE_ON = 0,
    SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF,
    SEQ_PLAY_SCHEDULER_EVT_PROGRAM_CHANGE
} seq_play_scheduler_evt_type_t;

typedef struct
{
    uint64_t due_sample_time;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint8_t type;
    uint8_t audio_dispatched;
    uint8_t generation;
} seq_play_scheduler_evt_t;

static seq_play_scheduler_evt_t g_seq_play_events[SEQ_PLAY_SCHEDULER_EVENT_CAP];
static uint16_t g_seq_play_event_count;
static uint8_t g_seq_play_generation;
static uint8_t g_seq_play_midi_program_valid[SEQ_TRACK_COUNT];
static uint8_t g_seq_play_midi_program_last[SEQ_TRACK_COUNT];
static seq_play_scheduler_diag_t g_seq_play_diag;
static void seq_play_scheduler_refresh_track(uint8_t track);
static void seq_play_scheduler_push(uint64_t due_sample_time,
                                    uint8_t type,
                                    seq_track_id_t track,
                                    uint8_t note,
                                    uint8_t velocity);
static int32_t seq_play_scheduler_apply_quant_percent(int32_t microtiming_samples, uint8_t quant_percent);

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

static void seq_play_scheduler_next_generation(void)
{
    g_seq_play_generation++;
    if (g_seq_play_generation == 0U)
    {
        g_seq_play_generation = 1U;
    }
}

static uint8_t seq_play_scheduler_program_value_decode(float value, uint8_t *out_program_0_127)
{
    const int32_t raw = (int32_t)(value + 0.5f);
    if (raw <= 0)
    {
        return 0U;
    }

    uint8_t program = (uint8_t)(raw - 1);
    if (program > 127U)
    {
        program = 127U;
    }

    if (out_program_0_127 != NULL)
    {
        *out_program_0_127 = program;
    }
    return 1U;
}

static uint8_t seq_play_scheduler_event_priority(uint8_t type)
{
    if (type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF)
    {
        return 0U;
    }
    if (type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_PROGRAM_CHANGE)
    {
        return 1U;
    }
    return 2U;
}

static void seq_play_scheduler_emit_midi_program(seq_track_id_t track, uint8_t program_0_127)
{
    const uint8_t channel = track_runtime_get_midi_channel_zero_based(track);
    midi_program_change(MIDI_DEST_BOTH, channel, program_0_127);

    if (track < SEQ_TRACK_COUNT)
    {
        g_seq_play_midi_program_valid[track] = 1U;
        g_seq_play_midi_program_last[track] = program_0_127;
    }
}

static void seq_play_scheduler_send_program_if_needed(seq_track_id_t track,
                                                      float program_value,
                                                      uint8_t force_send)
{
    uint8_t program_0_127 = 0U;
    if (seq_play_scheduler_program_value_decode(program_value, &program_0_127) == 0U)
    {
        return;
    }

    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if ((force_send == 0U)
            && (g_seq_play_midi_program_valid[track] != 0U)
            && (g_seq_play_midi_program_last[track] == program_0_127))
    {
        return;
    }

    seq_play_scheduler_emit_midi_program(track, program_0_127);
}

static void seq_play_scheduler_push_program_change(uint64_t due_sample_time,
                                                   seq_track_id_t track,
                                                   uint8_t program_0_127)
{
    seq_play_scheduler_push(due_sample_time,
                            (uint8_t)SEQ_PLAY_SCHEDULER_EVT_PROGRAM_CHANGE,
                            track,
                            program_0_127,
                            0U);
}

static uint8_t seq_play_scheduler_track_supports_program_change(const track_runtime_descriptor_t *descriptor)
{
    if ((descriptor == NULL) || (descriptor->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    if (descriptor->family == TRACK_RUNTIME_FAMILY_MIDI)
    {
        return 1U;
    }

    return ((descriptor->family == TRACK_RUNTIME_FAMILY_INPUT)
            && (descriptor->type == TRACK_RUNTIME_TYPE_HYBRID)) ? 1U : 0U;
}

static int32_t seq_play_scheduler_apply_quant_percent(int32_t microtiming_samples, uint8_t quant_percent)
{
    if (quant_percent == 0U)
    {
        return microtiming_samples;
    }

    if (quant_percent >= 100U)
    {
        return 0;
    }

    const int32_t remaining_percent = (int32_t)(100U - quant_percent);
    const int64_t scaled = (int64_t)microtiming_samples * (int64_t)remaining_percent;
    return (int32_t)((scaled + 50LL) / 100LL);
}

static void seq_play_scheduler_refresh_track(uint8_t track)
{
    track_runtime_refresh_track(track);
}


static void seq_play_scheduler_push(uint64_t due_sample_time,
                                    uint8_t type,
                                    seq_track_id_t track,
                                    uint8_t note,
                                    uint8_t velocity)
{
    const uint32_t primask = seq_play_scheduler_enter_critical();
    if (g_seq_play_event_count >= SEQ_PLAY_SCHEDULER_EVENT_CAP)
    {
        g_seq_play_diag.queue_overflow_drop_count++;
        seq_play_scheduler_exit_critical(primask);
        return;
    }

    seq_play_scheduler_evt_t *const evt = &g_seq_play_events[g_seq_play_event_count++];
    evt->due_sample_time = due_sample_time;
    evt->type = type;
    evt->track = track;
    evt->note = note;
    evt->velocity = velocity;
    evt->audio_dispatched = 0U;
    evt->generation = g_seq_play_generation;
    if (g_seq_play_event_count > g_seq_play_diag.queue_high_water)
    {
        g_seq_play_diag.queue_high_water = g_seq_play_event_count;
    }
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
    track_runtime_resolved_track_t resolved;
    if (track_runtime_resolve_track(track, &resolved) == 0U)
    {
        return;
    }

    if (resolved.descriptor.bind_state != TRACK_RUNTIME_BIND_BOUND)
    {
        return;
    }

    if (resolved.has_filter_target != 0U)
    {
        if (is_note_on != 0U)
        {
            mixer_track_filter_note_on(resolved.filter_track_id, note, velocity);
        }
        else
        {
            mixer_track_filter_note_off(resolved.filter_track_id, note);
        }
    }
    if ((resolved.supports_vca_gate != 0U) && (resolved.has_mix_target != 0U))
    {
        if (is_note_on != 0U)
        {
            mixer_track_vca_note_on(resolved.mix_track_id, note, velocity);
        }
        else
        {
            mixer_track_vca_note_off(resolved.mix_track_id, note);
        }
    }

    if (resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_DRUM)
    {
        if (is_note_on != 0U)
        {
            drum_synth_note_on_for_instance(resolved.descriptor.instance_id, note, velocity);
        }
        else
        {
            drum_synth_note_off_for_instance(resolved.descriptor.instance_id, note);
        }
    }
    else if (resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        if (is_note_on != 0U)
        {
            brick6_sampler_runtime_trigger_note(track, note);
        }
        else if (resolved.supports_vca_gate == 0U)
        {
            brick6_sampler_runtime_stop(track);
        }
    }
}

static void seq_play_scheduler_emit_midi_note(const seq_play_scheduler_audio_event_t *event)
{
    if (event == NULL)
    {
        return;
    }

    const uint8_t channel = track_runtime_get_midi_channel_zero_based(event->track);
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
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param_id);
    if (seq_param_iface_map_param(param_id, &set_id, &param8) == 0U)
    {
        return seq_param_iface_encode_param_value(param_id, param_registry[param_id].default_value);
    }

    seq_plock_entry_t entry;
    if (seq_model_step_plock_find(track, step, set_id, param8, &entry) != 0U)
    {
        return entry.value16;
    }

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
    {
        seq_value16_t base_value16 = 0U;
        if (seq_param_iface_get_play_base_value(track, param8, &base_value16) != 0U)
        {
            return base_value16;
        }
    }

    return seq_param_iface_encode_param_value(param_id, param_registry[param_id].default_value);
}

void seq_play_scheduler_init(void)
{
    g_seq_play_event_count = 0U;
    g_seq_play_generation = 1U;
    g_seq_play_diag = (seq_play_scheduler_diag_t){0};
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        g_seq_play_midi_program_valid[track] = 0U;
        g_seq_play_midi_program_last[track] = 0U;
    }
}

void seq_play_scheduler_clear(void)
{
    const uint32_t primask = seq_play_scheduler_enter_critical();
    g_seq_play_event_count = 0U;
    seq_play_scheduler_next_generation();
    seq_play_scheduler_exit_critical(primask);
}

void seq_play_scheduler_schedule_step(seq_track_id_t track,
                                      seq_step_id_t step,
                                      uint16_t ticks_per_step,
                                      uint32_t step_tick,
                                      uint64_t step_sample_time,
                                      uint32_t samples_per_step_q16)
{
    (void)ticks_per_step;
    (void)step_tick;

    seq_play_scheduler_refresh_track(track);

    if (seq_model_step_is_active(track, step) == 0U)
    {
        return;
    }

    if (seq_model_step_has_play_plock(track, step) == 0U)
    {
        return;
    }

    track_runtime_resolved_track_t resolved;
    if (track_runtime_resolve_track(track, &resolved) == 0U)
    {
        return;
    }
    if ((resolved.descriptor.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (track_runtime_get_effective_param_status(track, PARAM_SEQ_PLAY_V1_NOTE) == TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL))
    {
        return;
    }

    const float samples_per_step_f = ((float)samples_per_step_q16) / 65536.0f;
    uint8_t track_quant = 0U;
    if (track < SEQ_TRACK_COUNT)
    {
        /* Projection read: quant is a runtime mirror applied to note timing, not an authority. */
        (void)seq_runtime_get_track_quant(track, &track_quant);
        if (track_quant > 100U)
        {
            track_quant = 100U;
        }
    }

    uint8_t has_first_note = 0U;
    uint64_t first_note_sample_time = 0U;

    for (uint8_t voice = 0U; voice < SEQ_PLAY_SCHEDULER_VOICE_COUNT; ++voice)
    {
        const param_id_t note_id = seq_play_scheduler_param_note(voice);
        const param_id_t vel_id = seq_play_scheduler_param_vel(voice);
        const param_id_t len_id = seq_play_scheduler_param_len(voice);
        const param_id_t mictim_id = seq_play_scheduler_param_mictim(voice);
        /* Projection read: per-param status is a runtime guard, not a local recomputation. */
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
        const float mictim_f = seq_param_iface_decode_param_value(mictim_id,
                                                                  seq_play_scheduler_get_locked_or_default(track, step, mictim_id));
        int32_t microtiming_samples = (int32_t)((mictim_f * samples_per_step_f) / 96.0f);
        if (microtiming_samples < 0)
        {
            microtiming_samples = 0;
        }
        microtiming_samples = seq_play_scheduler_apply_quant_percent(microtiming_samples, track_quant);
        uint64_t note_on_sample_time = step_sample_time + (uint64_t)microtiming_samples;
        if ((has_first_note == 0U) || (note_on_sample_time < first_note_sample_time))
        {
            has_first_note = 1U;
            first_note_sample_time = note_on_sample_time;
        }

        uint64_t len_samples = (uint64_t)((len_steps_f * samples_per_step_f) + 0.5f);
        if (len_samples == 0U)
        {
            len_samples = 1U;
        }
        uint64_t note_off_sample_time = note_on_sample_time + len_samples;

        seq_play_scheduler_push(note_on_sample_time,
                                (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON,
                                track,
                                note,
                                vel);
        seq_play_scheduler_push(note_off_sample_time,
                                (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF,
                                track,
                                note,
                                0U);
    }

    if (has_first_note != 0U)
    {
        const float program_f = seq_param_iface_decode_param_value(PARAM_MIDI_PROGRAM,
                                                                   seq_play_scheduler_get_locked_or_default(track, step, PARAM_MIDI_PROGRAM));
        uint8_t program_0_127 = 0U;
        if (seq_play_scheduler_program_value_decode(program_f, &program_0_127) != 0U)
        {
            if ((track < SEQ_TRACK_COUNT)
                    && ((g_seq_play_midi_program_valid[track] == 0U)
                        || (g_seq_play_midi_program_last[track] != program_0_127)))
            {
                seq_play_scheduler_push_program_change(first_note_sample_time, track, program_0_127);
            }
        }
    }
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
    uint16_t overdue_count = 0U;
    uint16_t stale_generation_count = 0U;
    uint16_t clamp_count = 0U;
    while (count < max_events)
    {
        uint16_t selected_index = UINT16_MAX;
        uint64_t selected_sample = 0U;

        for (uint16_t i = 0U; i < g_seq_play_event_count; ++i)
        {
            const seq_play_scheduler_evt_t *const candidate = &g_seq_play_events[i];
            if (candidate->audio_dispatched != 0U)
            {
                continue;
            }
            if (candidate->generation != g_seq_play_generation)
            {
                g_seq_play_events[i].audio_dispatched = 1U;
                stale_generation_count++;
                continue;
            }
            if (candidate->due_sample_time >= block_end_sample)
            {
                continue;
            }

            if ((selected_index == UINT16_MAX)
                || (candidate->due_sample_time < selected_sample)
                || ((candidate->due_sample_time == selected_sample)
                    && (seq_play_scheduler_event_priority(candidate->type)
                        < seq_play_scheduler_event_priority(g_seq_play_events[selected_index].type))))
            {
                selected_index = i;
                selected_sample = candidate->due_sample_time;
            }
        }

        if (selected_index == UINT16_MAX)
        {
            break;
        }

        const seq_play_scheduler_evt_t evt = g_seq_play_events[selected_index];
        seq_play_scheduler_audio_event_t out_evt;
        out_evt.type = evt.type;
        out_evt.track = evt.track;
        out_evt.note = evt.note;
        out_evt.velocity = evt.velocity;
        if (evt.due_sample_time < block_start_sample)
        {
            out_evt.sample_offset_in_block = 0U;
            overdue_count++;
        }
        else
        {
            out_evt.sample_offset_in_block = (uint16_t)(evt.due_sample_time - block_start_sample);
        }
        if (out_evt.sample_offset_in_block >= block_frames)
        {
            out_evt.sample_offset_in_block = (uint16_t)(block_frames - 1U);
            clamp_count++;
        }
        out_events[count++] = out_evt;

        g_seq_play_events[selected_index].audio_dispatched = 1U;
    }

    uint16_t write = 0U;
    for (uint16_t read = 0U; read < g_seq_play_event_count; ++read)
    {
        const seq_play_scheduler_evt_t evt = g_seq_play_events[read];
        if (evt.audio_dispatched != 0U)
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
    g_seq_play_diag.overdue_event_count += overdue_count;
    g_seq_play_diag.offset_clamp_count += clamp_count;
    g_seq_play_diag.stale_generation_drop_count += stale_generation_count;
    if (count > g_seq_play_diag.max_events_collected_per_call)
    {
        g_seq_play_diag.max_events_collected_per_call = count;
    }
    seq_play_scheduler_exit_critical(primask);

    return count;
}

void seq_play_scheduler_diag_reset(void)
{
    const uint32_t primask = seq_play_scheduler_enter_critical();
    g_seq_play_diag = (seq_play_scheduler_diag_t){0};
    g_seq_play_diag.queue_high_water = g_seq_play_event_count;
    seq_play_scheduler_exit_critical(primask);
}

void seq_play_scheduler_diag_snapshot(seq_play_scheduler_diag_t *out_diag)
{
    if (out_diag == NULL)
    {
        return;
    }

    const uint32_t primask = seq_play_scheduler_enter_critical();
    *out_diag = g_seq_play_diag;
    seq_play_scheduler_exit_critical(primask);
}

void seq_play_scheduler_audio_apply_event(const seq_play_scheduler_audio_event_t *event)
{
    if (event == NULL)
    {
        return;
    }

    if (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_PROGRAM_CHANGE)
    {
        seq_play_scheduler_emit_midi_program((seq_track_id_t)event->track, event->note);
        return;
    }

    const uint8_t is_note_on = (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON) ? 1U : 0U;
    seq_play_scheduler_emit_midi_note(event);
    seq_play_scheduler_emit_engine_note((seq_track_id_t)event->track,
                                        event->note,
                                        event->velocity,
                                        is_note_on);
}

void seq_play_scheduler_live_midi_program_changed(seq_track_id_t track, float program_value)
{
    seq_play_scheduler_refresh_track(track);

    track_runtime_descriptor_t descriptor;
    if ((track_runtime_get_descriptor(track, &descriptor) == 0U)
            || (seq_play_scheduler_track_supports_program_change(&descriptor) == 0U))
    {
        return;
    }

    seq_play_scheduler_send_program_if_needed(track, program_value, 0U);
}

void seq_play_scheduler_emit_midi_program_on_transport_start(void)
{
    track_runtime_refresh_all();

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        track_runtime_descriptor_t descriptor;
        if ((track_runtime_get_descriptor(track, &descriptor) == 0U)
                || (seq_play_scheduler_track_supports_program_change(&descriptor) == 0U))
        {
            continue;
        }

        float program_f = 0.0f;
        if (param_registry_get_track_value(PARAM_MIDI_PROGRAM, track, &program_f) == 0U)
        {
            continue;
        }

        seq_play_scheduler_send_program_if_needed(track, program_f, 1U);
    }
}

void seq_play_scheduler_notify_track_pattern_change(seq_track_id_t track)
{
    seq_play_scheduler_refresh_track(track);

    track_runtime_descriptor_t descriptor;
    if ((track_runtime_get_descriptor(track, &descriptor) == 0U)
            || (seq_play_scheduler_track_supports_program_change(&descriptor) == 0U))
    {
        return;
    }

    float program_f = 0.0f;
    if (param_registry_get_track_value(PARAM_MIDI_PROGRAM, track, &program_f) == 0U)
    {
        return;
    }

    seq_play_scheduler_send_program_if_needed(track, program_f, 1U);
}
