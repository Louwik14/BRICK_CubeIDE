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
#include <string.h>
#include "stm32h7xx_hal.h"
#include "Core/brick6_braids_runtime.h"
#include "Audio/audio_control_snapshot.h"
#include "Audio/audio_midi_out.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Core/brick6_sampler_runtime.h"
#include "Audio/drum_synth.h"
#include "Audio/mixer.h"
#include "Mod/mod_lfo_v1.h"
#include "Storage/memory_layout.h"
#include "param_registry.h"
#include "midi.h"

#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_output_guard.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"

#define SEQ_PLAY_SCHEDULER_VOICE_COUNT 4U
#define SEQ_PLAY_SCHEDULER_EVENT_CAP 256U
#define SEQ_PLAY_PREPARED_ACTIONS_PER_STEP 9U
#define SEQ_PLAY_PREPARED_BUFFER_COUNT 2U



typedef enum
{
    SEQ_PLAY_SCHEDULER_EVT_NOTE_ON = 0,
    SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF,
    SEQ_PLAY_SCHEDULER_EVT_PROGRAM_CHANGE
} seq_play_scheduler_evt_type_t;

typedef enum
{
    SEQ_PLAY_PREPARED_ACTION_NOTE = 0,
    SEQ_PLAY_PREPARED_ACTION_PROGRAM_CHANGE,
    SEQ_PLAY_PREPARED_ACTION_PLOCK_APPLY,
    SEQ_PLAY_PREPARED_ACTION_PLOCK_RELEASE,
    SEQ_PLAY_PREPARED_ACTION_CONTROL
} seq_play_prepared_action_type_t;

typedef struct
{
    uint8_t type;
    uint8_t source_track;
    uint8_t note;
    uint8_t velocity;
    uint8_t set_id;
    seq_param_slot_t param_slot;
    seq_value16_t value16;
    float len_steps;
    float microtiming_ticks;
} seq_play_prepared_action_t;

typedef struct
{
    uint32_t generation;
    uint8_t action_count;
    uint8_t rejected_normal;
    uint8_t critical_failures;
    uint8_t reserved;
    seq_play_prepared_action_t actions[SEQ_PLAY_PREPARED_ACTIONS_PER_STEP];
} seq_play_prepared_step_t;

typedef struct
{
    uint64_t due_sample_time;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint8_t type;
    uint8_t audio_dispatched;
    uint8_t generation;
    uint32_t event_token;
} seq_play_scheduler_evt_t;

static seq_play_scheduler_evt_t g_seq_play_events[SEQ_PLAY_SCHEDULER_EVENT_CAP];
static uint16_t g_seq_play_event_count;
static uint8_t g_seq_play_generation;
static uint8_t g_seq_play_midi_program_valid[SEQ_TRACK_COUNT];
static uint8_t g_seq_play_midi_program_last[SEQ_TRACK_COUNT];
static seq_play_scheduler_diag_t g_seq_play_diag;
CONTROL_STATE_SDRAM static seq_play_prepared_step_t
    g_seq_play_prepared_steps[SEQ_PLAY_PREPARED_BUFFER_COUNT][SEQ_TRACK_COUNT][SEQ_MAX_STEPS];
static volatile uint8_t g_seq_play_prepared_active_index;
static volatile uint32_t g_seq_play_prepared_generation;
static volatile uint32_t g_seq_play_consumed_prepared_generation;
static uint8_t g_seq_play_group_rr_cursor[SEQ_TRACK_COUNT];
static uint8_t g_seq_play_group_track_active_count[SEQ_TRACK_COUNT];
static uint32_t g_seq_play_next_event_token;
static uint32_t g_seq_play_active_event_token[SEQ_TRACK_COUNT][128U];
static const param_id_t g_seq_play_voice_note_ids[SEQ_PLAY_SCHEDULER_VOICE_COUNT] = {
    PARAM_SEQ_PLAY_V1_NOTE, PARAM_SEQ_PLAY_V2_NOTE, PARAM_SEQ_PLAY_V3_NOTE, PARAM_SEQ_PLAY_V4_NOTE
};
static const param_id_t g_seq_play_voice_vel_ids[SEQ_PLAY_SCHEDULER_VOICE_COUNT] = {
    PARAM_SEQ_PLAY_V1_VEL, PARAM_SEQ_PLAY_V2_VEL, PARAM_SEQ_PLAY_V3_VEL, PARAM_SEQ_PLAY_V4_VEL
};
static const param_id_t g_seq_play_voice_len_ids[SEQ_PLAY_SCHEDULER_VOICE_COUNT] = {
    PARAM_SEQ_PLAY_V1_LEN, PARAM_SEQ_PLAY_V2_LEN, PARAM_SEQ_PLAY_V3_LEN, PARAM_SEQ_PLAY_V4_LEN
};
static const param_id_t g_seq_play_voice_mictim_ids[SEQ_PLAY_SCHEDULER_VOICE_COUNT] = {
    PARAM_SEQ_PLAY_V1_MICTIM, PARAM_SEQ_PLAY_V2_MICTIM, PARAM_SEQ_PLAY_V3_MICTIM, PARAM_SEQ_PLAY_V4_MICTIM
};
static void seq_play_scheduler_refresh_track(uint8_t track);
static void seq_play_scheduler_push(uint64_t due_sample_time,
                                    uint8_t type,
                                    seq_track_id_t track,
                                    uint8_t note,
                                    uint8_t velocity,
                                    uint32_t event_token);
static uint32_t seq_play_scheduler_alloc_event_token(void);
static int32_t seq_play_scheduler_apply_quant_percent(int32_t microtiming_samples, uint8_t quant_percent);
static param_id_t seq_play_scheduler_param_by_voice(const param_id_t *voice_ids,
                                                    uint8_t voice,
                                                    param_id_t fallback);

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

static uint32_t seq_play_scheduler_alloc_event_token(void)
{
    g_seq_play_next_event_token++;
    if (g_seq_play_next_event_token == 0U)
    {
        g_seq_play_next_event_token = 1U;
    }
    return g_seq_play_next_event_token;
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
    (void)audio_midi_out_program_change(channel, program_0_127, 0U);

    if (track < SEQ_TRACK_COUNT)
    {
        g_seq_play_midi_program_valid[track] = 1U;
        g_seq_play_midi_program_last[track] = program_0_127;
    }
}

static void seq_play_scheduler_emit_midi_program_audio(seq_track_id_t track, uint8_t program_0_127)
{
    const uint8_t channel = audio_control_snapshot_get_midi_channel_zero_based(track);
    (void)audio_midi_out_program_change(channel, program_0_127, 0U);

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
                            0U,
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
                                    uint8_t velocity,
                                    uint32_t event_token)
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
    evt->event_token = event_token;
    if (g_seq_play_event_count > g_seq_play_diag.queue_high_water)
    {
        g_seq_play_diag.queue_high_water = g_seq_play_event_count;
    }
    seq_play_scheduler_exit_critical(primask);
}

static param_id_t seq_play_scheduler_param_by_voice(const param_id_t *voice_ids,
                                                    uint8_t voice,
                                                    param_id_t fallback)
{
    return (voice_ids != NULL && voice < SEQ_PLAY_SCHEDULER_VOICE_COUNT) ? voice_ids[voice] : fallback;
}

static param_id_t seq_play_scheduler_param_note(uint8_t voice)
{
    return seq_play_scheduler_param_by_voice(g_seq_play_voice_note_ids, voice, PARAM_SEQ_PLAY_V1_NOTE);
}

static param_id_t seq_play_scheduler_param_vel(uint8_t voice)
{
    return seq_play_scheduler_param_by_voice(g_seq_play_voice_vel_ids, voice, PARAM_SEQ_PLAY_V1_VEL);
}

static param_id_t seq_play_scheduler_param_len(uint8_t voice)
{
    return seq_play_scheduler_param_by_voice(g_seq_play_voice_len_ids, voice, PARAM_SEQ_PLAY_V1_LEN);
}

static param_id_t seq_play_scheduler_param_mictim(uint8_t voice)
{
    return seq_play_scheduler_param_by_voice(g_seq_play_voice_mictim_ids, voice, PARAM_SEQ_PLAY_V1_MICTIM);
}

static uint8_t seq_play_scheduler_resolve_note_target_track(seq_track_id_t source_track, uint8_t note)
{
    (void)note;
    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)audio_control_snapshot_get_voice_group_role(source_track, &role_u8);
    if (role_u8 != (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER)
    {
        return source_track;
    }

    uint8_t members[SEQ_TRACK_COUNT];
    uint8_t member_count = 0U;
    if ((audio_control_snapshot_collect_voice_group_members(source_track,
                                                   members,
                                                   (uint8_t)SEQ_TRACK_COUNT,
                                                   &member_count) == 0U)
            || (member_count == 0U))
    {
        return source_track;
    }

    const uint8_t start = (uint8_t)(g_seq_play_group_rr_cursor[source_track] % member_count);
    uint8_t target = members[start];
    for (uint8_t i = 0U; i < member_count; ++i)
    {
        const uint8_t candidate = members[(uint8_t)((start + i) % member_count)];
        if (g_seq_play_group_track_active_count[candidate] == 0U)
        {
            target = candidate;
            break;
        }
    }
    g_seq_play_group_rr_cursor[source_track] = (uint8_t)((start + 1U) % member_count);
    return target;
}

static void seq_play_scheduler_emit_engine_note(seq_track_id_t track,
                                                uint8_t note,
                                                uint8_t velocity,
                                                uint8_t is_note_on)
{
    track_runtime_resolved_track_t resolved;
    if (audio_control_snapshot_resolve_track(track, &resolved) == 0U)
    {
        return;
    }

    if (resolved.descriptor.bind_state != TRACK_RUNTIME_BIND_BOUND)
    {
        return;
    }

    if (is_note_on != 0U)
    {
        mod_lfo_v1_note_trigger(track);
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
    else if (resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_WAVE)
    {
        if (is_note_on != 0U)
        {
            brick6_braids_runtime_note_on(resolved.descriptor.instance_id, (float)note, (float)velocity / 127.0f);
        }
        else
        {
            brick6_braids_runtime_note_off(resolved.descriptor.instance_id, note);
        }
    }
    else if (resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        if (resolved.descriptor.type == TRACK_RUNTIME_TYPE_MULTI)
        {
            if (is_note_on != 0U)
            {
                (void)brick6_sampler_runtime_trigger_multi_track_note_velocity(track, note, velocity);
            }
            else
            {
                brick6_sampler_runtime_note_off_multi_track_note(track, note);
            }
            return;
        }

        if (is_note_on != 0U)
        {
            brick6_sampler_runtime_trigger_note_velocity(track, note, velocity);
        }
        else if (resolved.supports_vca_gate == 0U)
        {
            brick6_sampler_runtime_note_off(track);
        }
    }
}

static void seq_play_scheduler_emit_midi_note_raw(const seq_play_scheduler_audio_event_t *event)
{
    if (event == NULL)
    {
        return;
    }

    const uint8_t channel = audio_control_snapshot_get_midi_channel_zero_based(event->track);
    if (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON)
    {
        (void)audio_midi_out_note_on(channel, event->note, event->velocity, event->sample_time);
        seq_output_guard_note_on_seen(event->track, event->note);
        return;
    }

    (void)audio_midi_out_note_off(channel, event->note, 0U, event->sample_time);
    seq_output_guard_note_off_seen(event->track, event->note);
}

static seq_value16_t seq_play_scheduler_get_locked_or_default(seq_track_id_t track,
                                                              seq_step_id_t step,
                                                              param_id_t param_id)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param_id);
    uint8_t set_id = 0U;
    switch (rule.domain)
    {
        case TRACK_RUNTIME_PARAM_DOMAIN_TONE:
            set_id = (uint8_t)SEQ_PLOCK_SET_TONE;
            break;
        case TRACK_RUNTIME_PARAM_DOMAIN_PLAY:
            set_id = (uint8_t)SEQ_PLOCK_SET_PLAY;
            break;
        default:
            return seq_param_iface_encode_param_value(param_id, param_registry[param_id].default_value);
    }

    seq_param_slot_t param_slot = 0U;
    if (seq_param_iface_param_to_slot(track, set_id, param_id, &param_slot) == 0U)
    {
        return seq_param_iface_encode_param_value(param_id, param_registry[param_id].default_value);
    }

    seq_plock_entry_t entry;
    if (seq_model_step_plock_find(track, step, set_id, param_slot, &entry) != 0U)
    {
        return entry.value16;
    }

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
    {
        seq_value16_t base_value16 = 0U;
        if (seq_param_iface_get_play_base_value(track, param_slot, &base_value16) != 0U)
        {
            return base_value16;
        }
    }

    return seq_param_iface_encode_param_value(param_id, param_registry[param_id].default_value);
}

static void seq_play_scheduler_prepared_push(seq_play_prepared_step_t *step,
                                             const seq_play_prepared_action_t *action,
                                             uint8_t critical)
{
    if ((step == NULL) || (action == NULL))
    {
        return;
    }

    if (step->action_count >= SEQ_PLAY_PREPARED_ACTIONS_PER_STEP)
    {
        if (critical != 0U)
        {
            step->critical_failures++;
            g_seq_play_diag.prepared_critical_failure_count++;
        }
        else
        {
            step->rejected_normal++;
            g_seq_play_diag.prepared_action_reject_count++;
        }
        return;
    }

    step->actions[step->action_count++] = *action;
}

static void seq_play_scheduler_prepare_step_from_control(uint8_t buffer_index,
                                                         seq_track_id_t track,
                                                         seq_step_id_t step,
                                                         uint32_t generation)
{
    seq_play_prepared_step_t prepared = {0};
    prepared.generation = generation;

    if ((seq_model_step_is_active(track, step) != 0U)
        && (seq_model_step_has_play_plock(track, step) != 0U))
    {
        uint8_t has_first_note = 0U;
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

            float len_steps_f = seq_param_iface_decode_param_value(len_id,
                                                                   seq_play_scheduler_get_locked_or_default(track, step, len_id));
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
            const seq_play_prepared_action_t action = {
                .type = (uint8_t)SEQ_PLAY_PREPARED_ACTION_NOTE,
                .source_track = track,
                .note = note,
                .velocity = vel,
                .len_steps = len_steps_f,
                .microtiming_ticks = mictim_f
            };
            seq_play_scheduler_prepared_push(&prepared, &action, 1U);
            has_first_note = 1U;
        }

        if (has_first_note != 0U)
        {
            const float program_f = seq_param_iface_decode_param_value(PARAM_MIDI_PROGRAM,
                                                                       seq_play_scheduler_get_locked_or_default(track, step, PARAM_MIDI_PROGRAM));
            uint8_t program_0_127 = 0U;
            if (seq_play_scheduler_program_value_decode(program_f, &program_0_127) != 0U)
            {
                const seq_play_prepared_action_t action = {
                    .type = (uint8_t)SEQ_PLAY_PREPARED_ACTION_PROGRAM_CHANGE,
                    .source_track = track,
                    .note = program_0_127
                };
                seq_play_scheduler_prepared_push(&prepared, &action, 0U);
            }
        }
    }

    if (prepared.action_count > g_seq_play_diag.max_prepared_actions_per_step)
    {
        g_seq_play_diag.max_prepared_actions_per_step = prepared.action_count;
    }

    g_seq_play_prepared_steps[buffer_index][track][step] = prepared;
}

void seq_play_scheduler_prepare_all_steps_from_control(void)
{
    const uint32_t t0 = DWT->CYCCNT;
    uint32_t next_generation = g_seq_play_prepared_generation + 1U;
    if (next_generation == 0U)
    {
        next_generation = 1U;
    }
    const uint8_t staging_index = (uint8_t)((g_seq_play_prepared_active_index + 1U)
                                           % SEQ_PLAY_PREPARED_BUFFER_COUNT);

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (seq_step_id_t step = 0U; step < SEQ_MAX_STEPS; ++step)
        {
            seq_play_scheduler_prepare_step_from_control(staging_index, track, step, next_generation);
        }
    }

    const uint32_t primask = seq_play_scheduler_enter_critical();
    g_seq_play_prepared_active_index = staging_index;
    g_seq_play_prepared_generation = next_generation;
    g_seq_play_diag.prepared_lots_published++;
    seq_play_scheduler_exit_critical(primask);

    const uint32_t cycles = DWT->CYCCNT - t0;
    if (cycles > g_seq_play_diag.max_prepare_cycles)
    {
        g_seq_play_diag.max_prepare_cycles = cycles;
    }
}

void seq_play_scheduler_init(void)
{
    g_seq_play_event_count = 0U;
    g_seq_play_generation = 1U;
    g_seq_play_next_event_token = 0U;
    g_seq_play_diag = (seq_play_scheduler_diag_t){0};
    memset(g_seq_play_prepared_steps, 0, sizeof(g_seq_play_prepared_steps));
    g_seq_play_prepared_active_index = 0U;
    g_seq_play_prepared_generation = 0U;
    g_seq_play_consumed_prepared_generation = 0U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        g_seq_play_midi_program_valid[track] = 0U;
        g_seq_play_midi_program_last[track] = 0U;
        g_seq_play_group_rr_cursor[track] = 0U;
        g_seq_play_group_track_active_count[track] = 0U;
    }
    memset(g_seq_play_active_event_token, 0, sizeof(g_seq_play_active_event_token));
}

void seq_play_scheduler_clear(void)
{
    const uint32_t primask = seq_play_scheduler_enter_critical();
    g_seq_play_event_count = 0U;
    memset(g_seq_play_group_rr_cursor, 0, sizeof(g_seq_play_group_rr_cursor));
    memset(g_seq_play_group_track_active_count, 0, sizeof(g_seq_play_group_track_active_count));
    memset(g_seq_play_active_event_token, 0, sizeof(g_seq_play_active_event_token));
    seq_play_scheduler_next_generation();
    seq_play_scheduler_exit_critical(primask);
}

static void seq_play_scheduler_schedule_step_filtered(seq_track_id_t track,
                                                     seq_step_id_t step,
                                                     uint64_t step_sample_time,
                                                     uint32_t samples_per_step_q16,
                                                     uint8_t negative_lookahead)
{
    const uint32_t place_t0 = DWT->CYCCNT;
    if ((track >= SEQ_TRACK_COUNT) || (step >= SEQ_MAX_STEPS))
    {
        return;
    }

    const uint8_t active_index = g_seq_play_prepared_active_index;
    const uint32_t expected_generation = g_seq_play_prepared_generation;
    const seq_play_prepared_step_t prepared = g_seq_play_prepared_steps[active_index][track][step];
    if (expected_generation == 0U)
    {
        g_seq_play_diag.prepared_lots_missing++;
        return;
    }
    if (prepared.action_count == 0U)
    {
        return;
    }
    if (prepared.generation != expected_generation)
    {
        g_seq_play_diag.prepared_generation_reject_count++;
        return;
    }
    if (g_seq_play_consumed_prepared_generation != expected_generation)
    {
        g_seq_play_consumed_prepared_generation = expected_generation;
        g_seq_play_diag.prepared_lots_consumed++;
    }

    track_runtime_resolved_track_t resolved;
    if (audio_control_snapshot_resolve_track(track, &resolved) == 0U)
    {
        return;
    }
    if ((resolved.descriptor.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (audio_control_snapshot_get_effective_param_status(track, PARAM_SEQ_PLAY_V1_NOTE) == TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL))
    {
        return;
    }

    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)audio_control_snapshot_get_voice_group_role(track, &role_u8);
    if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
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

    uint8_t emitted_note = 0U;
    uint64_t first_note_sample_time = 0U;

    for (uint8_t i = 0U; i < prepared.action_count; ++i)
    {
        const seq_play_prepared_action_t *const action = &prepared.actions[i];
        if (action->type != (uint8_t)SEQ_PLAY_PREPARED_ACTION_NOTE)
        {
            continue;
        }
        if (audio_control_snapshot_get_effective_param_status(track, PARAM_SEQ_PLAY_V1_NOTE) == TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL)
        {
            continue;
        }

        int32_t microtiming_samples = (int32_t)((action->microtiming_ticks * samples_per_step_f) / 96.0f);
        if (((negative_lookahead != 0U) && (microtiming_samples >= 0))
            || ((negative_lookahead == 0U) && (microtiming_samples < 0)))
        {
            continue;
        }

        microtiming_samples = seq_play_scheduler_apply_quant_percent(microtiming_samples, track_quant);
        if (((negative_lookahead != 0U) && (microtiming_samples >= 0))
            || ((negative_lookahead == 0U) && (microtiming_samples < 0)))
        {
            continue;
        }

        uint64_t note_on_sample_time = step_sample_time;
        if (microtiming_samples < 0)
        {
            const uint64_t early = (uint64_t)(-microtiming_samples);
            note_on_sample_time = (early < step_sample_time) ? (step_sample_time - early) : 0U;
        }
        else
        {
            note_on_sample_time = step_sample_time + (uint64_t)microtiming_samples;
        }
        if ((emitted_note == 0U) || (note_on_sample_time < first_note_sample_time))
        {
            emitted_note = 1U;
            first_note_sample_time = note_on_sample_time;
        }

        uint64_t len_samples = (uint64_t)((action->len_steps * samples_per_step_f) + 0.5f);
        if (len_samples == 0U)
        {
            len_samples = 1U;
        }
        uint64_t note_off_sample_time = note_on_sample_time + len_samples;
        if (note_off_sample_time <= note_on_sample_time)
        {
            note_off_sample_time = note_on_sample_time + 1ULL;
        }

        const seq_track_id_t target_track = (seq_track_id_t)seq_play_scheduler_resolve_note_target_track(track, action->note);
        const uint32_t event_token = seq_play_scheduler_alloc_event_token();

        seq_play_scheduler_push(note_on_sample_time,
                                (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON,
                                target_track,
                                action->note,
                                action->velocity,
                                event_token);
        seq_play_scheduler_push(note_off_sample_time,
                                (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF,
                                target_track,
                                action->note,
                                0U,
                                event_token);
    }

    if (emitted_note != 0U)
    {
        for (uint8_t i = 0U; i < prepared.action_count; ++i)
        {
            const seq_play_prepared_action_t *const action = &prepared.actions[i];
            if (action->type != (uint8_t)SEQ_PLAY_PREPARED_ACTION_PROGRAM_CHANGE)
            {
                continue;
            }
            if ((g_seq_play_midi_program_valid[track] == 0U)
                || (g_seq_play_midi_program_last[track] != action->note))
            {
                seq_play_scheduler_push_program_change(first_note_sample_time, track, action->note);
            }
        }
    }

    const uint32_t place_cycles = DWT->CYCCNT - place_t0;
    if (place_cycles > g_seq_play_diag.max_place_cycles)
    {
        g_seq_play_diag.max_place_cycles = place_cycles;
    }
}

void seq_play_scheduler_schedule_step(seq_track_id_t track,
                                      seq_step_id_t step,
                                      uint16_t ticks_per_step,
                                      uint32_t step_tick,
                                      uint64_t step_sample_time,
                                      uint32_t samples_per_step_q16)
{
    /* Scheduling seam: consume resolved step boundaries and queue sample-domain events only. */
    (void)ticks_per_step;
    (void)step_tick;
    seq_play_scheduler_schedule_step_filtered(track,
                                              step,
                                              step_sample_time,
                                              samples_per_step_q16,
                                              0U);
}

void seq_play_scheduler_schedule_step_lookahead_negative(seq_track_id_t track,
                                                         seq_step_id_t step,
                                                         uint64_t step_sample_time,
                                                         uint32_t samples_per_step_q16)
{
    seq_play_scheduler_schedule_step_filtered(track,
                                              step,
                                              step_sample_time,
                                              samples_per_step_q16,
                                              1U);
}
uint16_t seq_play_scheduler_audio_collect_block_events(seq_play_scheduler_audio_event_t *out_events,
                                                       uint16_t max_events,
                                                       uint16_t block_frames,
                                                       uint64_t block_start_sample)
{
    /* Projection seam: expose due queued events for the current audio block without changing timeline ownership. */
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
        out_evt.event_token = evt.event_token;
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
        out_evt.sample_time = block_start_sample + (uint64_t)out_evt.sample_offset_in_block;
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
    /* Diagnostics mirror only: clear accumulated queue stats without affecting scheduler ownership. */
    const uint32_t primask = seq_play_scheduler_enter_critical();
    g_seq_play_diag = (seq_play_scheduler_diag_t){0};
    g_seq_play_diag.queue_high_water = g_seq_play_event_count;
    seq_play_scheduler_exit_critical(primask);
}

void seq_play_scheduler_diag_snapshot(seq_play_scheduler_diag_t *out_diag)
{
    /* Diagnostics mirror only: expose queue stats as read-only projection. */
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
    /* Apply seam: dispatch a scheduler event to engines/output only. */
    if (event == NULL)
    {
        return;
    }

    if (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_PROGRAM_CHANGE)
    {
        seq_play_scheduler_emit_midi_program_audio((seq_track_id_t)event->track, event->note);
        return;
    }

    const uint8_t is_note_on = (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON) ? 1U : 0U;
    const uint8_t valid_note_key = ((event->track < SEQ_TRACK_COUNT) && (event->note < 128U)) ? 1U : 0U;

    if (is_note_on == 0U)
    {
        if (valid_note_key != 0U)
        {
            if (g_seq_play_active_event_token[event->track][event->note] != event->event_token)
            {
                return;
            }
            g_seq_play_active_event_token[event->track][event->note] = 0U;
        }
    }
    else if (valid_note_key != 0U)
    {
        if (g_seq_play_active_event_token[event->track][event->note] != 0U)
        {
            seq_play_scheduler_audio_event_t forced_off = *event;
            forced_off.type = (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF;
            forced_off.velocity = 0U;
            seq_play_scheduler_emit_midi_note_raw(&forced_off);
            seq_play_scheduler_emit_engine_note((seq_track_id_t)event->track,
                                                event->note,
                                                0U,
                                                0U);
            if (g_seq_play_group_track_active_count[event->track] > 0U)
            {
                g_seq_play_group_track_active_count[event->track]--;
            }
        }
        g_seq_play_active_event_token[event->track][event->note] = event->event_token;
    }

    if (event->track < SEQ_TRACK_COUNT)
    {
        if (is_note_on != 0U)
        {
            if (g_seq_play_group_track_active_count[event->track] < 255U)
            {
                g_seq_play_group_track_active_count[event->track]++;
            }
        }
        else if (g_seq_play_group_track_active_count[event->track] > 0U)
        {
            g_seq_play_group_track_active_count[event->track]--;
        }
    }
    seq_play_scheduler_emit_midi_note_raw(event);
    seq_play_scheduler_emit_engine_note((seq_track_id_t)event->track,
                                        event->note,
                                        event->velocity,
                                        is_note_on);
}

void seq_play_scheduler_live_midi_program_changed(seq_track_id_t track, float program_value)
{
    /* Post-commit seam: runtime already committed the change; scheduler only refreshes emit mirrors. */
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
    /* Post-commit seam: transport start re-seeds scheduler-side program state only. */
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
    /* Post-commit seam: pattern change publishes a fresh prepared action lot. */
    seq_play_scheduler_prepare_all_steps_from_control();
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
