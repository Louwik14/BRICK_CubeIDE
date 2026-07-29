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
#include "Core/brick6_deluge_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Core/brick6_sampler_runtime.h"
#include "Audio/drum_synth.h"
#include "Audio/mixer.h"
#include "Keyboard/keyboard_arp.h"
#include "Mod/mod_lfo_v1.h"
#include "param_registry.h"
#include "midi.h"

#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_output_guard.h"
#include "Seq/seq_plock_route.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"

#define SEQ_PLAY_SCHEDULER_VOICE_COUNT 4U
#define SEQ_PLAY_SCHEDULER_EVENT_CAP 512U
#define SEQ_PLAY_SCHEDULER_ARP_NOTE_CAP 64U
#define SEQ_PLAY_SCHEDULER_GROUP_MEMBER_MAX 8U



typedef enum
{
    SEQ_PLAY_SCHEDULER_EVT_NOTE_ON = 0,
    SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF,
    SEQ_PLAY_SCHEDULER_EVT_PROGRAM_CHANGE
} seq_play_scheduler_evt_type_t;

typedef enum
{
    SEQ_PLAY_SCHEDULER_PLAY_PARAM_NOTE = 0,
    SEQ_PLAY_SCHEDULER_PLAY_PARAM_VEL,
    SEQ_PLAY_SCHEDULER_PLAY_PARAM_LEN,
    SEQ_PLAY_SCHEDULER_PLAY_PARAM_MICTIM
} seq_play_scheduler_play_param_t;

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

typedef struct
{
    uint8_t active;
    uint8_t count;
    uint8_t revision;
    uint8_t notes[SEQ_PLAY_SCHEDULER_VOICE_COUNT];
    uint8_t velocities[SEQ_PLAY_SCHEDULER_VOICE_COUNT];
    uint64_t end_sample_time;
    uint64_t next_due_sample_q16;
} seq_play_scheduler_arp_window_t;

typedef struct
{
    seq_track_id_t target_track;
    seq_track_id_t source_track;
    seq_step_id_t source_step;
    uint8_t target_voice;
    uint8_t source_voice;
    uint8_t linked;
} seq_play_scheduler_play_item_t;

typedef struct
{
    seq_track_id_t scheduler_track;
    seq_track_id_t source_track;
    seq_step_id_t source_step;
    uint8_t source_roll;
    uint8_t linked;
    uint8_t group_master;
    uint8_t item_count;
    seq_play_scheduler_play_item_t items[SEQ_PLAY_SCHEDULER_GROUP_MEMBER_MAX];
} seq_play_scheduler_play_context_t;

static seq_play_scheduler_evt_t g_seq_play_events[SEQ_PLAY_SCHEDULER_EVENT_CAP];
static seq_play_scheduler_arp_window_t g_seq_play_arp_windows[SEQ_TRACK_COUNT];
static uint16_t g_seq_play_event_count;
static uint8_t g_seq_play_generation;
static uint8_t g_seq_play_midi_program_valid[SEQ_TRACK_COUNT];
static uint8_t g_seq_play_midi_program_last[SEQ_TRACK_COUNT];
static seq_play_scheduler_diag_t g_seq_play_diag;
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
static void seq_play_scheduler_push_note_retrigs(uint64_t note_on_sample_time,
                                                 uint64_t len_samples,
                                                 uint64_t step_span_q16,
                                                 uint8_t roll,
                                                 seq_track_id_t target_track,
                                                 uint8_t note,
                                                 uint8_t velocity);
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

static void seq_play_scheduler_push_note_pair(uint64_t note_on_sample_time,
                                              uint64_t note_off_sample_time,
                                              seq_track_id_t target_track,
                                              uint8_t note,
                                              uint8_t velocity)
{
    const uint32_t event_token = seq_play_scheduler_alloc_event_token();
    seq_play_scheduler_push(note_on_sample_time,
                            (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON,
                            target_track,
                            note,
                            velocity,
                            event_token);
    seq_play_scheduler_push(note_off_sample_time,
                            (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF,
                            target_track,
                            note,
                            0U,
                            event_token);
}

static void seq_play_scheduler_push_note_retrigs(uint64_t note_on_sample_time,
                                                 uint64_t len_samples,
                                                 uint64_t step_span_q16,
                                                 uint8_t roll,
                                                 seq_track_id_t target_track,
                                                 uint8_t note,
                                                 uint8_t velocity)
{
    if (len_samples == 0U)
    {
        len_samples = 1U;
    }

    seq_play_scheduler_push_note_pair(note_on_sample_time,
                                      note_on_sample_time + len_samples,
                                      target_track,
                                      note,
                                      velocity);

    const uint16_t divisor = seq_model_step_roll_divisor(roll);
    if (divisor == 0U)
    {
        return;
    }

    const uint64_t interval_q16 = (step_span_q16 * 16ULL) / (uint64_t)divisor;
    if (interval_q16 == 0U)
    {
        return;
    }

    uint64_t offset_q16 = interval_q16;
    while (offset_q16 < step_span_q16)
    {
        const uint64_t offset_samples = (offset_q16 + 0x8000ULL) >> 16;
        if (offset_samples != 0U)
        {
            const uint64_t retrig_on = note_on_sample_time + offset_samples;
            seq_play_scheduler_push_note_pair(retrig_on,
                                              retrig_on + len_samples,
                                              target_track,
                                              note,
                                              velocity);
        }
        offset_q16 += interval_q16;
    }
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

static param_id_t seq_play_scheduler_param_for_play_kind(seq_play_scheduler_play_param_t kind,
                                                         uint8_t voice)
{
    switch (kind)
    {
        case SEQ_PLAY_SCHEDULER_PLAY_PARAM_NOTE:
            return seq_play_scheduler_param_note(voice);
        case SEQ_PLAY_SCHEDULER_PLAY_PARAM_VEL:
            return seq_play_scheduler_param_vel(voice);
        case SEQ_PLAY_SCHEDULER_PLAY_PARAM_LEN:
            return seq_play_scheduler_param_len(voice);
        case SEQ_PLAY_SCHEDULER_PLAY_PARAM_MICTIM:
            return seq_play_scheduler_param_mictim(voice);
        default:
            break;
    }

    return seq_play_scheduler_param_note(0U);
}

static uint8_t seq_play_scheduler_resolve_note_target_track(seq_track_id_t source_track, uint8_t note)
{
    (void)note;
    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(source_track, &role_u8);
    if (role_u8 != (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER)
    {
        return source_track;
    }

    uint8_t members[SEQ_TRACK_COUNT];
    uint8_t member_count = 0U;
    if ((track_runtime_collect_voice_group_members(source_track,
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
    if (track_runtime_resolve_track(track, &resolved) == 0U)
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
    else
    {
        mod_lfo_v1_note_release(track);
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
    else if (resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_PRISM)
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
    else if (resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_STACK)
    {
        if (is_note_on != 0U)
        {
            brick6_stack_runtime_note_on(resolved.descriptor.instance_id, note, velocity);
        }
        else
        {
            brick6_stack_runtime_note_off(resolved.descriptor.instance_id, note);
        }
    }
    else if (resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_WAVE)
    {
        if (is_note_on != 0U)
        {
            brick6_wave_runtime_note_on(resolved.descriptor.instance_id, note, velocity);
        }
        else
        {
            brick6_wave_runtime_note_off(resolved.descriptor.instance_id, note);
        }
    }
    else if (resolved.descriptor.engine == TRACK_RUNTIME_ENGINE_DELUGE)
    {
        if (is_note_on != 0U)
        {
            brick6_deluge_runtime_note_on(resolved.descriptor.instance_id, note, velocity);
        }
        else
        {
            brick6_deluge_runtime_note_off(resolved.descriptor.instance_id, note);
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

    const uint8_t channel = track_runtime_get_midi_channel_zero_based(event->track);
    if (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON)
    {
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

static seq_value16_t seq_play_scheduler_get_play_locked_or_default(const seq_play_scheduler_play_item_t *item,
                                                                   seq_play_scheduler_play_param_t kind)
{
    if (item == NULL)
    {
        return seq_param_iface_encode_param_value(PARAM_SEQ_PLAY_V1_NOTE,
                                                  param_registry[PARAM_SEQ_PLAY_V1_NOTE].default_value);
    }

    const param_id_t source_param =
        seq_play_scheduler_param_for_play_kind(kind, item->source_voice);
    const param_id_t target_param =
        seq_play_scheduler_param_for_play_kind(kind, item->target_voice);

    seq_param_slot_t source_slot = 0U;
    if (seq_param_iface_param_to_slot(item->source_track,
                                      (uint8_t)SEQ_PLOCK_SET_PLAY,
                                      source_param,
                                      &source_slot) != 0U)
    {
        seq_plock_entry_t entry;
        if (seq_model_step_plock_find(item->source_track,
                                      item->source_step,
                                      (uint8_t)SEQ_PLOCK_SET_PLAY,
                                      source_slot,
                                      &entry) != 0U)
        {
            return entry.value16;
        }
    }

    seq_param_slot_t target_slot = 0U;
    if ((seq_param_iface_param_to_slot(item->target_track,
                                       (uint8_t)SEQ_PLOCK_SET_PLAY,
                                       target_param,
                                       &target_slot) != 0U))
    {
        seq_value16_t base_value16 = 0U;
        if (seq_param_iface_get_play_base_value(item->target_track, target_slot, &base_value16) != 0U)
        {
            return base_value16;
        }
    }

    return seq_param_iface_encode_param_value(target_param, param_registry[target_param].default_value);
}

void seq_play_scheduler_init(void)
{
    g_seq_play_event_count = 0U;
    g_seq_play_generation = 1U;
    g_seq_play_next_event_token = 0U;
    g_seq_play_diag = (seq_play_scheduler_diag_t){0};
    memset(g_seq_play_arp_windows, 0, sizeof(g_seq_play_arp_windows));
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
    memset(g_seq_play_arp_windows, 0, sizeof(g_seq_play_arp_windows));
    memset(g_seq_play_group_rr_cursor, 0, sizeof(g_seq_play_group_rr_cursor));
    memset(g_seq_play_group_track_active_count, 0, sizeof(g_seq_play_group_track_active_count));
    memset(g_seq_play_active_event_token, 0, sizeof(g_seq_play_active_event_token));
    seq_play_scheduler_next_generation();
    seq_play_scheduler_exit_critical(primask);
}

void seq_play_scheduler_clear_tracks(const seq_track_id_t *tracks, uint8_t track_count)
{
    if ((tracks == NULL) || (track_count == 0U))
    {
        return;
    }

    uint8_t clear_track[SEQ_TRACK_COUNT];
    memset(clear_track, 0, sizeof(clear_track));
    for (uint8_t i = 0U; i < track_count; ++i)
    {
        if (tracks[i] < SEQ_TRACK_COUNT)
        {
            clear_track[tracks[i]] = 1U;
        }
    }

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if (clear_track[track] == 0U)
        {
            continue;
        }

        for (uint8_t note = 0U; note < 128U; ++note)
        {
            uint32_t token = 0U;
            uint32_t primask = seq_play_scheduler_enter_critical();
            token = g_seq_play_active_event_token[track][note];
            g_seq_play_active_event_token[track][note] = 0U;
            seq_play_scheduler_exit_critical(primask);

            if (token == 0U)
            {
                continue;
            }

            const seq_play_scheduler_audio_event_t forced_off = {
                .type = (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF,
                .track = track,
                .note = note,
                .velocity = 0U,
                .sample_offset_in_block = 0U,
                .event_token = token,
            };
            seq_play_scheduler_emit_midi_note_raw(&forced_off);
            seq_play_scheduler_emit_engine_note(track, note, 0U, 0U);
        }
    }

    const uint32_t primask = seq_play_scheduler_enter_critical();
    uint16_t write_index = 0U;
    for (uint16_t read_index = 0U; read_index < g_seq_play_event_count; ++read_index)
    {
        const seq_play_scheduler_evt_t event = g_seq_play_events[read_index];
        if ((event.track < SEQ_TRACK_COUNT) && (clear_track[event.track] != 0U))
        {
            continue;
        }

        if (write_index != read_index)
        {
            g_seq_play_events[write_index] = event;
        }
        write_index++;
    }
    g_seq_play_event_count = write_index;

    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if (clear_track[track] == 0U)
        {
            continue;
        }
        memset(&g_seq_play_arp_windows[track], 0, sizeof(g_seq_play_arp_windows[track]));
        g_seq_play_group_rr_cursor[track] = 0U;
        g_seq_play_group_track_active_count[track] = 0U;
    }
    seq_play_scheduler_exit_critical(primask);
}

static uint64_t seq_play_scheduler_track_step_span_samples_q16(seq_track_id_t track,
                                                               uint32_t samples_per_step_q16)
{
    uint8_t div = 1U;
    (void)seq_runtime_get_track_div(track, &div);
    if ((div != 1U) && (div != 2U) && (div != 4U) && (div != 8U))
    {
        div = 1U;
    }

    const uint32_t sps_q16 = (samples_per_step_q16 == 0U) ? 1U : samples_per_step_q16;
    return (uint64_t)sps_q16 * (uint64_t)div;
}

static uint32_t seq_play_scheduler_samples_ceil_from_q16(uint64_t value_q16)
{
    return (uint32_t)((value_q16 + 0xFFFFULL) >> 16);
}

static uint64_t seq_play_scheduler_slice_end_sample(seq_track_id_t track,
                                                    uint64_t step_sample_time,
                                                    uint32_t samples_per_step_q16)
{
    const uint32_t span_samples =
        seq_play_scheduler_samples_ceil_from_q16(seq_play_scheduler_track_step_span_samples_q16(track,
                                                                                                samples_per_step_q16));
    return step_sample_time + (uint64_t)((span_samples == 0U) ? 1U : span_samples);
}

static void seq_play_scheduler_push_arp_note(seq_track_id_t track,
                                             uint64_t base_sample_time,
                                             const keyboard_arp_scheduled_note_t *arp_note)
{
    if (arp_note == NULL)
    {
        return;
    }

    const seq_track_id_t target_track =
        (seq_track_id_t)seq_play_scheduler_resolve_note_target_track(track, arp_note->note);
    const uint32_t event_token = seq_play_scheduler_alloc_event_token();
    const uint64_t note_on_sample_time = base_sample_time + (uint64_t)arp_note->on_offset_samples;
    uint64_t note_off_sample_time = base_sample_time + (uint64_t)arp_note->off_offset_samples;
    if (note_off_sample_time <= note_on_sample_time)
    {
        note_off_sample_time = note_on_sample_time + 1ULL;
    }

    seq_play_scheduler_push(note_on_sample_time,
                            (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON,
                            target_track,
                            arp_note->note,
                            arp_note->velocity,
                            event_token);
    seq_play_scheduler_push(note_off_sample_time,
                            (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF,
                            target_track,
                            arp_note->note,
                            0U,
                            event_token);
}

static void seq_play_scheduler_schedule_arp_window_slice(seq_track_id_t track,
                                                         uint64_t slice_start_sample,
                                                         uint64_t slice_end_sample,
                                                         uint32_t samples_per_step_q16)
{
    if ((track >= SEQ_TRACK_COUNT) || (slice_end_sample <= slice_start_sample))
    {
        return;
    }

    seq_play_scheduler_arp_window_t *const window = &g_seq_play_arp_windows[track];
    if (window->active == 0U)
    {
        return;
    }

    if ((keyboard_arp_get_hold_for_track(track) == false)
            || (window->count == 0U)
            || (window->end_sample_time <= slice_start_sample))
    {
        window->active = 0U;
        return;
    }

    const uint8_t revision = keyboard_arp_get_revision_for_track(track);
    const uint64_t slice_start_q16 = slice_start_sample << 16;
    if (window->revision != revision)
    {
        window->revision = revision;
        window->next_due_sample_q16 = slice_start_q16;
    }
    else if (window->next_due_sample_q16 < slice_start_q16)
    {
        window->next_due_sample_q16 = slice_start_q16;
    }

    const uint64_t due_sample_time = window->next_due_sample_q16 >> 16;
    const uint64_t limit_sample_time =
        (window->end_sample_time < slice_end_sample) ? window->end_sample_time : slice_end_sample;
    if (due_sample_time >= limit_sample_time)
    {
        return;
    }

    uint64_t duration = limit_sample_time - due_sample_time;
    if (duration > 0xFFFFFFFFULL)
    {
        duration = 0xFFFFFFFFULL;
    }

    keyboard_arp_scheduled_note_t arp_notes[SEQ_PLAY_SCHEDULER_ARP_NOTE_CAP];
    uint64_t next_offset_q16 = 0ULL;
    const uint8_t arp_note_count =
        keyboard_arp_seq_step_render_for_track(track,
                                               window->notes,
                                               window->velocities,
                                               window->count,
                                               samples_per_step_q16,
                                               (uint32_t)duration,
                                               arp_notes,
                                               SEQ_PLAY_SCHEDULER_ARP_NOTE_CAP,
                                               &next_offset_q16);
    for (uint8_t i = 0U; i < arp_note_count; ++i)
    {
        seq_play_scheduler_push_arp_note(track, due_sample_time, &arp_notes[i]);
    }

    if (next_offset_q16 == 0ULL)
    {
        window->next_due_sample_q16 = limit_sample_time << 16;
    }
    else
    {
        window->next_due_sample_q16 += next_offset_q16;
    }

    if ((window->next_due_sample_q16 >> 16) >= window->end_sample_time)
    {
        window->active = 0U;
    }
}

static void seq_play_scheduler_begin_arp_window(seq_track_id_t track,
                                                const uint8_t *notes,
                                                const uint8_t *velocities,
                                                uint8_t count,
                                                uint64_t start_sample_time,
                                                uint64_t end_sample_time)
{
    if ((track >= SEQ_TRACK_COUNT) || (notes == NULL) || (velocities == NULL) || (count == 0U)
            || (end_sample_time <= start_sample_time))
    {
        return;
    }

    seq_play_scheduler_arp_window_t *const window = &g_seq_play_arp_windows[track];
    window->active = 1U;
    window->count = (count > SEQ_PLAY_SCHEDULER_VOICE_COUNT) ? SEQ_PLAY_SCHEDULER_VOICE_COUNT : count;
    window->revision = keyboard_arp_get_revision_for_track(track);
    window->end_sample_time = end_sample_time;
    window->next_due_sample_q16 = start_sample_time << 16;
    for (uint8_t i = 0U; i < window->count; ++i)
    {
        window->notes[i] = notes[i];
        window->velocities[i] = velocities[i];
    }
}

static void seq_play_scheduler_context_add_item(seq_play_scheduler_play_context_t *context,
                                                seq_track_id_t target_track,
                                                uint8_t target_voice,
                                                seq_track_id_t source_track,
                                                seq_step_id_t source_step,
                                                uint8_t source_voice)
{
    if ((context == NULL) || (context->item_count >= SEQ_PLAY_SCHEDULER_GROUP_MEMBER_MAX))
    {
        return;
    }

    seq_play_scheduler_play_item_t *const item = &context->items[context->item_count++];
    item->target_track = target_track;
    item->target_voice = target_voice;
    item->source_track = source_track;
    item->source_step = source_step;
    item->source_voice = source_voice;
    item->linked = context->linked;
}

static uint8_t seq_play_scheduler_resolve_play_context(seq_track_id_t scheduler_track,
                                                       seq_step_id_t scheduler_step,
                                                       seq_play_scheduler_play_context_t *out_context)
{
    if ((out_context == NULL)
            || (scheduler_track >= SEQ_TRACK_COUNT)
            || (seq_model_is_step_editable_index(scheduler_step) == 0U))
    {
        return 0U;
    }

    memset(out_context, 0, sizeof(*out_context));
    out_context->scheduler_track = scheduler_track;

    seq_plock_route_t route;
    if (seq_plock_route_resolve(scheduler_track, scheduler_step, &route) == 0U)
    {
        return 0U;
    }

    out_context->source_track = scheduler_track;
    out_context->source_step = scheduler_step;
    out_context->linked = 0U;
    out_context->group_master = route.group_master;
    out_context->source_roll = seq_model_get_step_roll(out_context->source_track,
                                                       out_context->source_step);

    if (route.target_count == 0U)
    {
        return 1U;
    }

    if (route.group_master != 0U)
    {
        for (uint8_t i = 0U; i < route.target_count; ++i)
        {
            seq_play_scheduler_context_add_item(out_context,
                                                route.targets[i],
                                                0U,
                                                route.targets[i],
                                                scheduler_step,
                                                0U);
        }
        return 1U;
    }

    for (uint8_t voice = 0U; voice < SEQ_PLAY_SCHEDULER_VOICE_COUNT; ++voice)
    {
        seq_play_scheduler_context_add_item(out_context,
                                            scheduler_track,
                                            voice,
                                            out_context->source_track,
                                            out_context->source_step,
                                            voice);
    }
    return 1U;
}

static uint8_t seq_play_scheduler_context_has_play_plock(const seq_play_scheduler_play_context_t *context)
{
    if (context == NULL)
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < context->item_count; ++i)
    {
        const seq_play_scheduler_play_item_t *const item = &context->items[i];
        if (seq_model_step_has_play_plock(item->source_track, item->source_step) != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

static void seq_play_scheduler_schedule_step_filtered(seq_track_id_t track,
                                                     seq_step_id_t step,
                                                     uint64_t step_sample_time,
                                                     uint32_t samples_per_step_q16,
                                                     uint8_t negative_lookahead)
{
    seq_play_scheduler_refresh_track(track);
    const uint64_t arp_slice_end_sample =
        seq_play_scheduler_slice_end_sample(track, step_sample_time, samples_per_step_q16);
    uint8_t schedule_existing_arp_slice = (negative_lookahead == 0U) ? 1U : 0U;

    seq_play_scheduler_play_context_t play_context;
    if ((seq_play_scheduler_resolve_play_context(track, step, &play_context) == 0U)
            || (play_context.item_count == 0U))
    {
        goto finish;
    }

    if (seq_model_step_is_active(play_context.source_track, play_context.source_step) == 0U)
    {
        goto finish;
    }

    track_runtime_resolved_track_t resolved;
    if (track_runtime_resolve_track(track, &resolved) == 0U)
    {
        goto finish;
    }
    if ((resolved.descriptor.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (track_runtime_get_effective_param_status(track, PARAM_SEQ_PLAY_V1_NOTE) == TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL))
    {
        goto finish;
    }

    if (seq_play_scheduler_context_has_play_plock(&play_context) == 0U)
    {
        goto finish;
    }

    const float samples_per_step_f = ((float)samples_per_step_q16) / 65536.0f;
    const uint64_t track_step_span_q16 = seq_play_scheduler_track_step_span_samples_q16(track, samples_per_step_q16);
    const uint8_t step_roll = play_context.source_roll;
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
    const uint8_t arp_hold_step =
        ((negative_lookahead == 0U) && keyboard_arp_get_hold_for_track(track)) ? 1U : 0U;
    uint8_t arp_step_notes[SEQ_PLAY_SCHEDULER_VOICE_COUNT];
    uint8_t arp_step_vel[SEQ_PLAY_SCHEDULER_VOICE_COUNT];
    uint8_t arp_step_count = 0U;
    uint8_t arp_window_valid = 0U;
    uint64_t arp_window_start_sample_time = 0U;
    uint64_t arp_window_end_sample_time = 0U;

    for (uint8_t voice = 0U; voice < play_context.item_count; ++voice)
    {
        const seq_play_scheduler_play_item_t *const item = &play_context.items[voice];
        const param_id_t note_id = seq_play_scheduler_param_note(item->target_voice);
        const param_id_t vel_id = seq_play_scheduler_param_vel(item->target_voice);
        const param_id_t len_id = seq_play_scheduler_param_len(item->target_voice);
        const param_id_t mictim_id = seq_play_scheduler_param_mictim(item->target_voice);
        /* Projection read: per-param status is a runtime guard, not a local recomputation. */
        if (track_runtime_get_effective_param_status(item->target_track, note_id) == TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL)
        {
            continue;
        }

        const float vel_f = seq_param_iface_decode_param_value(vel_id,
                                                               seq_play_scheduler_get_play_locked_or_default(item,
                                                                                                             SEQ_PLAY_SCHEDULER_PLAY_PARAM_VEL));
        const uint8_t vel = (uint8_t)(vel_f + 0.5f);
        if (vel == 0U)
        {
            continue;
        }

        const float note_f = seq_param_iface_decode_param_value(note_id,
                                                                seq_play_scheduler_get_play_locked_or_default(item,
                                                                                                              SEQ_PLAY_SCHEDULER_PLAY_PARAM_NOTE));
        const uint8_t note = (uint8_t)(note_f + 0.5f);
        if (note >= 128U)
        {
            continue;
        }

        const float len_f = seq_param_iface_decode_param_value(len_id,
                                                               seq_play_scheduler_get_play_locked_or_default(item,
                                                                                                             SEQ_PLAY_SCHEDULER_PLAY_PARAM_LEN));
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
                                                                  seq_play_scheduler_get_play_locked_or_default(item,
                                                                                                                SEQ_PLAY_SCHEDULER_PLAY_PARAM_MICTIM));
        int32_t microtiming_samples = (int32_t)((mictim_f * samples_per_step_f) / 96.0f);
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
        if (note_off_sample_time <= note_on_sample_time)
        {
            note_off_sample_time = note_on_sample_time + 1ULL;
        }

        if (arp_hold_step != 0U)
        {
            if (arp_step_count < SEQ_PLAY_SCHEDULER_VOICE_COUNT)
            {
                arp_step_notes[arp_step_count] = note;
                arp_step_vel[arp_step_count] = vel;
                arp_step_count++;
            }
            if ((arp_window_valid == 0U) || (note_on_sample_time < arp_window_start_sample_time))
            {
                arp_window_start_sample_time = note_on_sample_time;
            }
            if ((arp_window_valid == 0U) || (note_off_sample_time > arp_window_end_sample_time))
            {
                arp_window_end_sample_time = note_off_sample_time;
            }
            arp_window_valid = 1U;
            continue;
        }

        const seq_track_id_t target_track = (play_context.group_master != 0U)
            ? item->target_track
            : (seq_track_id_t)seq_play_scheduler_resolve_note_target_track(track, note);
        seq_play_scheduler_push_note_retrigs(note_on_sample_time,
                                             len_samples,
                                             track_step_span_q16,
                                             step_roll,
                                             target_track,
                                             note,
                                             vel);
    }

    if (has_first_note != 0U)
    {
        const float program_f = seq_param_iface_decode_param_value(PARAM_MIDI_PROGRAM,
                                                                   seq_play_scheduler_get_locked_or_default(play_context.source_track,
                                                                                                            play_context.source_step,
                                                                                                            PARAM_MIDI_PROGRAM));
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

    if ((arp_step_count > 0U) && (arp_window_valid != 0U) && (arp_window_end_sample_time > arp_window_start_sample_time))
    {
        seq_play_scheduler_begin_arp_window(track,
                                            arp_step_notes,
                                            arp_step_vel,
                                            arp_step_count,
                                            arp_window_start_sample_time,
                                            arp_window_end_sample_time);
        seq_play_scheduler_schedule_arp_window_slice(track,
                                                     arp_window_start_sample_time,
                                                     arp_slice_end_sample,
                                                     samples_per_step_q16);
        schedule_existing_arp_slice = 0U;
    }

finish:
    if (schedule_existing_arp_slice != 0U)
    {
        seq_play_scheduler_schedule_arp_window_slice(track,
                                                     step_sample_time,
                                                     arp_slice_end_sample,
                                                     samples_per_step_q16);
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
        seq_play_scheduler_emit_midi_program((seq_track_id_t)event->track, event->note);
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
    /* Post-commit seam: pattern change re-seeds scheduler-side program state only. */
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
