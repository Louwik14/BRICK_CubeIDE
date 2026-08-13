/*
 * Module: seq_play_scheduler
 * Role: Scheduler PLAY des événements note-on/note-off en sample-domain.
 * Responsibilities: lire steps/plocks, dériver voix note/vel/len/mictim,
 * planifier des événements horodatés en samples et les appliquer via le chemin audio.
 * Integration: scheduling déclenché par seq_runtime aux boundaries de step, collecte/apply en IRQ audio.
 */
#define SEQ_PLAY_SCHEDULER_IMPLEMENTATION 1
#include "Seq/seq_play_scheduler.h"

#include "Storage/memory_layout.h"

#include <stdint.h>
#include <string.h>
#include "stm32h7xx_hal.h"
#include "Core/track_runtime.h"
#include "Core/live_clock.h"
#include "Core/track_mute.h"
#include "Audio/control_audio_queue.h"
#include "NoteFx/note_fx_pipeline.h"
#include "param_registry.h"
#include "midi.h"

#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_output_guard.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_runtime_exec.h"

#define SEQ_PLAY_SCHEDULER_EVENT_CAP 512U
#define SEQ_PLAY_SCHEDULER_ACTIVE_TOKEN_CAPACITY 64U


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
    uint8_t control_dispatched;
    uint32_t generation;
    uint8_t track_generation;
    uint32_t event_token;
} seq_play_scheduler_evt_t;

typedef struct
{
    seq_track_id_t target_track;
    seq_track_id_t source_track;
    seq_step_id_t source_step;
    uint8_t target_voice;
    uint8_t source_voice;
} seq_play_scheduler_play_item_t;

typedef struct
{
    seq_track_id_t scheduler_track;
    seq_track_id_t source_track;
    seq_step_id_t source_step;
    uint8_t source_roll;
    uint8_t item_count;
    seq_play_scheduler_play_item_t items[SEQ_PLAY_MAX_CAPACITY];
} seq_play_scheduler_play_context_t;

SEQ_STATE_D2 static seq_play_scheduler_evt_t g_seq_play_events[SEQ_PLAY_SCHEDULER_EVENT_CAP];
static uint16_t g_seq_play_event_count;
static uint8_t g_seq_play_generation;
static uint8_t g_seq_play_midi_program_valid[SEQ_LANE_CAPACITY];
static uint8_t g_seq_play_midi_program_last[SEQ_LANE_CAPACITY];
static uint32_t g_seq_play_next_event_token;
static uint8_t g_seq_play_control_window_active;
static uint16_t g_seq_play_control_window_remaining;
static uint16_t g_seq_play_control_window_used;

/*
 * Runtime-only occurrence records retained by the scheduler active-note
 * ledger. Each record is exact token + generation + note metadata and is
 * distinct from pitch and persisted step data. The Multi sampler receives
 * the same occurrence token and resolves it to its voice-generation handle.
 */
typedef struct
{
    uint8_t active;
    uint8_t note;
    uint16_t reserved;
    uint32_t token;
    uint32_t generation;
} seq_play_active_occurrence_t;

SEQ_STATE_D2 static seq_play_active_occurrence_t
    g_seq_play_active_occurrence[SEQ_LANE_CAPACITY][SEQ_PLAY_SCHEDULER_ACTIVE_TOKEN_CAPACITY];
static uint8_t g_seq_play_track_generation[SEQ_LANE_CAPACITY];
static uint8_t g_seq_play_track_suspended[SEQ_LANE_CAPACITY];
static uint8_t g_seq_play_track_closing[SEQ_LANE_CAPACITY];
static const param_id_t g_seq_play_voice_note_ids[SEQ_PLAY_MAX_CAPACITY] = {
    PARAM_SEQ_PLAY_V1_NOTE, PARAM_SEQ_PLAY_V2_NOTE, PARAM_SEQ_PLAY_V3_NOTE, PARAM_SEQ_PLAY_V4_NOTE,
    PARAM_SEQ_PLAY_V5_NOTE, PARAM_SEQ_PLAY_V6_NOTE, PARAM_SEQ_PLAY_V7_NOTE, PARAM_SEQ_PLAY_V8_NOTE
};
static const param_id_t g_seq_play_voice_vel_ids[SEQ_PLAY_MAX_CAPACITY] = {
    PARAM_SEQ_PLAY_V1_VEL, PARAM_SEQ_PLAY_V2_VEL, PARAM_SEQ_PLAY_V3_VEL, PARAM_SEQ_PLAY_V4_VEL,
    PARAM_SEQ_PLAY_V5_VEL, PARAM_SEQ_PLAY_V6_VEL, PARAM_SEQ_PLAY_V7_VEL, PARAM_SEQ_PLAY_V8_VEL
};
static const param_id_t g_seq_play_voice_len_ids[SEQ_PLAY_MAX_CAPACITY] = {
    PARAM_SEQ_PLAY_V1_LEN, PARAM_SEQ_PLAY_V2_LEN, PARAM_SEQ_PLAY_V3_LEN, PARAM_SEQ_PLAY_V4_LEN,
    PARAM_SEQ_PLAY_V5_LEN, PARAM_SEQ_PLAY_V6_LEN, PARAM_SEQ_PLAY_V7_LEN, PARAM_SEQ_PLAY_V8_LEN
};
static const param_id_t g_seq_play_voice_mictim_ids[SEQ_PLAY_MAX_CAPACITY] = {
    PARAM_SEQ_PLAY_V1_MICTIM, PARAM_SEQ_PLAY_V2_MICTIM, PARAM_SEQ_PLAY_V3_MICTIM, PARAM_SEQ_PLAY_V4_MICTIM,
    PARAM_SEQ_PLAY_V5_MICTIM, PARAM_SEQ_PLAY_V6_MICTIM, PARAM_SEQ_PLAY_V7_MICTIM, PARAM_SEQ_PLAY_V8_MICTIM
};
static void seq_play_scheduler_refresh_track(uint8_t track);
static void seq_play_scheduler_push(uint64_t due_sample_time,
                                    uint8_t type,
                                    seq_track_id_t track,
                                    uint8_t note,
                                    uint8_t velocity,
                                    uint32_t event_token);
static uint8_t seq_play_scheduler_push_note_pair(uint64_t note_on_sample_time,
                                                 uint64_t note_off_sample_time,
                                                 seq_track_id_t target_track,
                                                 uint8_t note,
                                                 uint8_t velocity);
static void seq_play_scheduler_push_note_retrigs(uint64_t note_on_sample_time,
                                                 uint64_t len_samples,
                                                 uint64_t step_span_q16,
                                                 uint8_t roll,
                                                 seq_track_id_t target_track,
                                                 uint8_t note,
                                                 uint8_t velocity);
static uint32_t seq_play_scheduler_alloc_event_token(void);
static uint8_t seq_play_scheduler_active_occurrence_add(seq_track_id_t track,
                                                         uint8_t note,
                                                         uint32_t token,
                                                         uint32_t generation)
{
    if ((track >= SEQ_LANE_CAPACITY) || (note >= 128U)
            || (token == 0U) || (generation == 0U))
        return 0U;

    for (uint8_t i = 0U; i < SEQ_PLAY_SCHEDULER_ACTIVE_TOKEN_CAPACITY; ++i)
    {
        seq_play_active_occurrence_t *const record = &g_seq_play_active_occurrence[track][i];
        if ((record->active != 0U) && (record->token == token)
                && (record->generation == generation))
        {
            return 1U;
        }
    }
    for (uint8_t i = 0U; i < SEQ_PLAY_SCHEDULER_ACTIVE_TOKEN_CAPACITY; ++i)
    {
        seq_play_active_occurrence_t *const record = &g_seq_play_active_occurrence[track][i];
        if (record->active == 0U)
        {
            *record = (seq_play_active_occurrence_t){
                .active = 1U,
                .note = note,
                .token = token,
                .generation = generation
            };
            return 1U;
        }
    }
    return 0U;
}

static uint8_t seq_play_scheduler_active_occurrence_remove(seq_track_id_t track,
                                                            uint8_t note,
                                                            uint32_t token,
                                                            uint32_t generation)
{
    if ((track >= SEQ_LANE_CAPACITY) || (note >= 128U)
            || (token == 0U) || (generation == 0U))
        return 0U;

    for (uint8_t i = 0U; i < SEQ_PLAY_SCHEDULER_ACTIVE_TOKEN_CAPACITY; ++i)
    {
        seq_play_active_occurrence_t *const record = &g_seq_play_active_occurrence[track][i];
        if ((record->active != 0U) && (record->note == note)
                && (record->token == token) && (record->generation == generation))
        {
            record->active = 0U;
            return 1U;
        }
    }
    return 0U;
}

static uint32_t seq_play_scheduler_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void seq_play_scheduler_exit_critical(uint32_t primask)
{
    if (primask == 0U)
        __enable_irq();
}

static void seq_play_scheduler_next_generation(void)
{
    ++g_seq_play_generation;
    if (g_seq_play_generation == 0U)
        g_seq_play_generation = 1U;
}

static void seq_play_scheduler_next_track_generation(seq_track_id_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
        return;
    ++g_seq_play_track_generation[track];
    if (g_seq_play_track_generation[track] == 0U)
        g_seq_play_track_generation[track] = 1U;
}

static uint32_t seq_play_scheduler_alloc_event_token(void)
{
    g_seq_play_next_event_token =
        (g_seq_play_next_event_token + 1U) & NOTE_EVENT_OCCURRENCE_COUNTER_MASK;
    if (g_seq_play_next_event_token == 0U)
        g_seq_play_next_event_token = 1U;
    return NOTE_EVENT_OCCURRENCE_NAMESPACE_STEP | g_seq_play_next_event_token;
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

    if (track < SEQ_LANE_CAPACITY)
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

    if (track >= SEQ_LANE_CAPACITY)
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

    if ((descriptor->family == TRACK_RUNTIME_FAMILY_EXTERNAL)
            && (descriptor->type == TRACK_RUNTIME_TYPE_EXTERNAL))
    {
        return 1U;
    }

    return 0U;
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
    if ((track >= SEQ_LANE_CAPACITY) || (g_seq_play_track_suspended[track] != 0U))
    {
        seq_play_scheduler_exit_critical(primask);
        return;
    }
    if (g_seq_play_event_count >= SEQ_PLAY_SCHEDULER_EVENT_CAP)
    {
        seq_play_scheduler_exit_critical(primask);
        return;
    }

    seq_play_scheduler_evt_t *const evt = &g_seq_play_events[g_seq_play_event_count++];
    evt->due_sample_time = due_sample_time;
    evt->type = type;
    evt->track = track;
    evt->note = note;
    evt->velocity = velocity;
    evt->control_dispatched = 0U;
    evt->generation = g_seq_play_generation;
    evt->track_generation = g_seq_play_track_generation[track];
    evt->event_token = event_token;
    seq_play_scheduler_exit_critical(primask);
}

static uint8_t seq_play_scheduler_push_note_pair(uint64_t note_on_sample_time,
                                                 uint64_t note_off_sample_time,
                                                 seq_track_id_t target_track,
                                                 uint8_t note,
                                                 uint8_t velocity)
{
    const uint32_t primask = seq_play_scheduler_enter_critical();
    if ((target_track >= SEQ_LANE_CAPACITY)
            || (g_seq_play_track_suspended[target_track] != 0U))
    {
        seq_play_scheduler_exit_critical(primask);
        return 0U;
    }

    if ((uint16_t)(SEQ_PLAY_SCHEDULER_EVENT_CAP - g_seq_play_event_count) < 2U)
    {
        seq_play_scheduler_exit_critical(primask);
        return 0U;
    }

    const uint32_t event_token = seq_play_scheduler_alloc_event_token();
    seq_play_scheduler_evt_t *const note_on = &g_seq_play_events[g_seq_play_event_count++];
    *note_on = (seq_play_scheduler_evt_t){
        .due_sample_time = note_on_sample_time,
        .track = target_track,
        .note = note,
        .velocity = velocity,
        .type = (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON,
        .control_dispatched = 0U,
        .generation = g_seq_play_generation,
        .track_generation = g_seq_play_track_generation[target_track],
        .event_token = event_token,
    };
    seq_play_scheduler_evt_t *const note_off = &g_seq_play_events[g_seq_play_event_count++];
    *note_off = (seq_play_scheduler_evt_t){
        .due_sample_time = note_off_sample_time,
        .track = target_track,
        .note = note,
        .velocity = 0U,
        .type = (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF,
        .control_dispatched = 0U,
        .generation = g_seq_play_generation,
        .track_generation = g_seq_play_track_generation[target_track],
        .event_token = event_token,
    };
    seq_play_scheduler_exit_critical(primask);
    return 1U;
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
    return (voice_ids != NULL && voice < SEQ_PLAY_MAX_CAPACITY) ? voice_ids[voice] : fallback;
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

static seq_step_play_field_t seq_play_scheduler_field_for_play_kind(
    seq_play_scheduler_play_param_t kind)
{
    switch (kind)
    {
        case SEQ_PLAY_SCHEDULER_PLAY_PARAM_NOTE:
            return SEQ_STEP_PLAY_FIELD_NOTE;
        case SEQ_PLAY_SCHEDULER_PLAY_PARAM_VEL:
            return SEQ_STEP_PLAY_FIELD_VELOCITY;
        case SEQ_PLAY_SCHEDULER_PLAY_PARAM_LEN:
            return SEQ_STEP_PLAY_FIELD_LENGTH;
        case SEQ_PLAY_SCHEDULER_PLAY_PARAM_MICTIM:
            return SEQ_STEP_PLAY_FIELD_MICROTIMING;
        default:
            return SEQ_STEP_PLAY_FIELD_NOTE;
    }
}

static seq_value16_t seq_play_scheduler_get_locked_or_default(seq_track_id_t track,
                                                              seq_step_id_t step,
                                                              param_id_t param_id)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param_id);
    uint8_t set_id = 0U;
    if ((param_id >= PARAM_FM_OPERATOR_FIRST) && (param_id <= PARAM_FM_OPERATOR_LAST))
    {
        set_id = (uint8_t)SEQ_PLOCK_SET_FM_OPERATOR;
    }
    else switch (rule.domain)
    {
        case TRACK_RUNTIME_PARAM_DOMAIN_TONE:
            set_id = (uint8_t)SEQ_PLOCK_SET_TONE;
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

    int16_t stored_value = 0;
    if (seq_model_play_get(item->source_track,
                                item->source_step,
                                item->source_voice,
                                seq_play_scheduler_field_for_play_kind(kind),
                                &stored_value) != 0U)
    {
        return seq_param_iface_encode_param_value(source_param, (float)stored_value);
    }

    {
        seq_value16_t base_value16 = 0U;
        if (seq_param_iface_get_play_base_param(item->target_track, target_param, &base_value16) != 0U)
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
    g_seq_play_control_window_active = 0U;
    g_seq_play_control_window_remaining = 0U;
    g_seq_play_control_window_used = 0U;
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        g_seq_play_midi_program_valid[track] = 0U;
        g_seq_play_midi_program_last[track] = 0U;
    }
    memset(g_seq_play_active_occurrence, 0, sizeof(g_seq_play_active_occurrence));
    memset(g_seq_play_track_suspended, 0, sizeof(g_seq_play_track_suspended));
    memset(g_seq_play_track_closing, 0, sizeof(g_seq_play_track_closing));
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        g_seq_play_track_generation[track] = 1U;
    }
}

static note_fx_transition_policy_t seq_play_scheduler_note_fx_policy(
    seq_play_transition_policy_t policy)
{
    switch (policy)
    {
        case SEQ_PLAY_TRANSITION_STOP_CLOSE:
            return NOTE_FX_TRANSITION_STOP_CLOSE;
        case SEQ_PLAY_TRANSITION_PANIC_CLOSE_ALL:
            return NOTE_FX_TRANSITION_PANIC_CLOSE_ALL;
        case SEQ_PLAY_TRANSITION_PATTERN_REPLACE:
            return NOTE_FX_TRANSITION_PATTERN_REPLACE;
        case SEQ_PLAY_TRANSITION_MODEL_RECONFIGURE:
            return NOTE_FX_TRANSITION_MODEL_RECONFIGURE;
        case SEQ_PLAY_TRANSITION_DESTINATION_REBIND:
            return NOTE_FX_TRANSITION_DESTINATION_REBIND;
        case SEQ_PLAY_TRANSITION_SOURCE_SWITCH:
            return NOTE_FX_TRANSITION_SOURCE_CLOCK_CHANGE;
        case SEQ_PLAY_TRANSITION_MUTE_TRIGS:
        case SEQ_PLAY_TRANSITION_RESUME_TRIGS:
        default:
            return NOTE_FX_TRANSITION_MUTE_TRIGS;
    }
}

static uint8_t seq_play_scheduler_destructive_transition(
    const seq_track_id_t *tracks, uint8_t track_count,
    seq_play_transition_policy_t policy)
{
    uint8_t selected[SEQ_LANE_CAPACITY] = {0U};
    for (uint8_t i = 0U; i < track_count; ++i)
        if (tracks[i] < SEQ_LANE_CAPACITY)
            selected[tracks[i]] = 1U;

    uint32_t primask = seq_play_scheduler_enter_critical();
    for (seq_track_id_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        if (selected[track] != 0U)
            g_seq_play_track_closing[track] = 1U;
    seq_play_scheduler_exit_critical(primask);


    const note_fx_transition_policy_t note_fx_policy =
        seq_play_scheduler_note_fx_policy(policy);
    for (seq_track_id_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        if ((selected[track] != 0U)
                && (note_fx_pipeline_transition_track(track, note_fx_policy) == 0U))
            return 0U;

    uint64_t close_sample = 0U;
    if (!live_clock_read_audio_sample(&close_sample))
        return 0U;
    for (seq_track_id_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        if (selected[track] == 0U)
            continue;
        const control_audio_event_t close_entity = {
            .due_sample = close_sample,
            .entity_id = track,
            .kind = (uint8_t)CONTROL_AUDIO_EVENT_CLOSE_ENTITY
        };
        if (control_audio_queue_publish(&close_entity) == 0U)
            return 0U;
    }

    const uint8_t suspend_after = (uint8_t)(
        (policy == SEQ_PLAY_TRANSITION_MODEL_RECONFIGURE)
        || (policy == SEQ_PLAY_TRANSITION_DESTINATION_REBIND));
    primask = seq_play_scheduler_enter_critical();
    uint16_t write_index = 0U;
    for (uint16_t read_index = 0U; read_index < g_seq_play_event_count;
         ++read_index)
    {
        const seq_play_scheduler_evt_t event = g_seq_play_events[read_index];
        if ((event.track < SEQ_LANE_CAPACITY)
                && (selected[event.track] != 0U))
            continue;
        if (write_index != read_index)
            g_seq_play_events[write_index] = event;
        ++write_index;
    }
    g_seq_play_event_count = write_index;
    uint8_t selected_count = 0U;
    for (seq_track_id_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        if (selected[track] == 0U)
            continue;
        ++selected_count;
        for (uint8_t i = 0U; i < SEQ_PLAY_SCHEDULER_ACTIVE_TOKEN_CAPACITY; ++i)
        {
            g_seq_play_active_occurrence[track][i].active = 0U;
        }
        g_seq_play_track_suspended[track] = suspend_after;
        g_seq_play_track_closing[track] = 0U;
        seq_play_scheduler_next_track_generation(track);
    }
    if (selected_count == SEQ_LANE_CAPACITY)
        seq_play_scheduler_next_generation();
    seq_play_scheduler_exit_critical(primask);
    return 1U;
}

void seq_play_scheduler_clear(void)
{
    (void)seq_play_scheduler_transition_all(
        SEQ_PLAY_TRANSITION_PANIC_CLOSE_ALL);
}

void seq_play_scheduler_clear_tracks(const seq_track_id_t *tracks, uint8_t track_count)
{
    if ((tracks == NULL) || (track_count == 0U))
        return;

    (void)seq_play_scheduler_destructive_transition(
        tracks, track_count, SEQ_PLAY_TRANSITION_STOP_CLOSE);
}
void seq_play_scheduler_suspend_tracks(const seq_track_id_t *tracks, uint8_t track_count)
{
    if ((tracks == NULL) || (track_count == 0U))
    {
        return;
    }

    const uint32_t primask = seq_play_scheduler_enter_critical();
    for (uint8_t i = 0U; i < track_count; ++i)
    {
        const seq_track_id_t track = tracks[i];
        if (track < SEQ_LANE_CAPACITY)
        {
            g_seq_play_track_suspended[track] = 1U;
        }
    }
    seq_play_scheduler_exit_critical(primask);
}

void seq_play_scheduler_resume_tracks(const seq_track_id_t *tracks, uint8_t track_count)
{
    if ((tracks == NULL) || (track_count == 0U))
    {
        return;
    }

    const uint32_t primask = seq_play_scheduler_enter_critical();
    for (uint8_t i = 0U; i < track_count; ++i)
    {
        const seq_track_id_t track = tracks[i];
        if (track < SEQ_LANE_CAPACITY)
        {
            g_seq_play_track_suspended[track] = 0U;
        }
    }
    seq_play_scheduler_exit_critical(primask);
}

uint8_t seq_play_scheduler_transition_tracks(const seq_track_id_t *tracks,
                                             uint8_t track_count,
                                             seq_play_transition_policy_t policy)
{
    if ((tracks == NULL) || (track_count == 0U)
            || (policy > SEQ_PLAY_TRANSITION_SOURCE_SWITCH))
        return 0U;

    if (policy == SEQ_PLAY_TRANSITION_MUTE_TRIGS)
    {
        for (uint8_t i = 0U; i < track_count; ++i)
            if ((tracks[i] < SEQ_LANE_CAPACITY)
                    && (note_fx_pipeline_transition_track(
                        tracks[i], NOTE_FX_TRANSITION_MUTE_TRIGS) == 0U))
                return 0U;
        seq_play_scheduler_suspend_tracks(tracks, track_count);
        return 1U;
    }
    if (policy == SEQ_PLAY_TRANSITION_RESUME_TRIGS)
    {
        seq_play_scheduler_resume_tracks(tracks, track_count);
        return 1U;
    }

    return seq_play_scheduler_destructive_transition(
        tracks, track_count, policy);
}

uint8_t seq_play_scheduler_transition_all(seq_play_transition_policy_t policy)
{
    if (policy > SEQ_PLAY_TRANSITION_SOURCE_SWITCH)
        return 0U;
    seq_track_id_t tracks[SEQ_LANE_CAPACITY];
    for (seq_track_id_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        tracks[track] = track;
    return seq_play_scheduler_transition_tracks(
        tracks, SEQ_LANE_CAPACITY, policy);
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

static void seq_play_scheduler_context_add_item(seq_play_scheduler_play_context_t *context,
                                                seq_track_id_t target_track,
                                                uint8_t target_voice,
                                                seq_track_id_t source_track,
                                                seq_step_id_t source_step,
                                                uint8_t source_voice)
{
    if ((context == NULL) || (context->item_count >= SEQ_PLAY_MAX_CAPACITY))
    {
        return;
    }

    seq_play_scheduler_play_item_t *const item = &context->items[context->item_count++];
    item->target_track = target_track;
    item->target_voice = target_voice;
    item->source_track = source_track;
    item->source_step = source_step;
    item->source_voice = source_voice;
}

static uint8_t seq_play_scheduler_resolve_play_context(seq_track_id_t scheduler_track,
                                                       seq_step_id_t scheduler_step,
                                                       seq_play_scheduler_play_context_t *out_context)
{
    if ((out_context == NULL)
            || (scheduler_track >= SEQ_LANE_CAPACITY)
            || (seq_model_is_step_editable_index(scheduler_step) == 0U))
    {
        return 0U;
    }

    memset(out_context, 0, sizeof(*out_context));
    out_context->scheduler_track = scheduler_track;

    out_context->source_track = scheduler_track;
    out_context->source_step = scheduler_step;
    out_context->source_roll = seq_model_get_step_roll(out_context->source_track,
                                                       out_context->source_step);

    const uint8_t play_capacity = seq_model_play_capacity(out_context->source_track);
    for (uint8_t voice = 0U; voice < play_capacity; ++voice)
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

static void seq_play_scheduler_schedule_step_filtered(seq_track_id_t track,
                                                     seq_step_id_t step,
                                                     uint64_t step_sample_time,
                                                     uint32_t samples_per_step_q16,
                                                     uint8_t negative_lookahead)
{
    entity_topology_descriptor_t entity;
    if ((entity_topology_get((brick_entity_id_t)track, &entity) == 0U)
            || (entity_topology_can_emit_notes(&entity) == 0U))
    {
        return;
    }

    seq_play_scheduler_refresh_track(track);

    if (track_runtime_has_capability(track, TRACK_CAPABILITY_NOTES) == 0U)
    {
        if (track < SEQ_LANE_CAPACITY)
        {
        }
        return;
    }

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

    if (track_runtime_get_effective_param_status(
            track, PARAM_SEQ_PLAY_V1_NOTE)
            == TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL)
    {
        goto finish;
    }

    const float samples_per_step_f = ((float)samples_per_step_q16) / 65536.0f;
    const uint64_t track_step_span_q16 = seq_play_scheduler_track_step_span_samples_q16(track, samples_per_step_q16);
    const uint8_t step_roll = play_context.source_roll;
    uint8_t track_quant = 0U;
    if (track < SEQ_LANE_CAPACITY)
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

        seq_play_scheduler_push_note_retrigs(note_on_sample_time,
                                             len_samples,
                                             track_step_span_q16,
                                             step_roll,
                                             track,
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
            if ((track < SEQ_LANE_CAPACITY)
                    && ((g_seq_play_midi_program_valid[track] == 0U)
                        || (g_seq_play_midi_program_last[track] != program_0_127)))
            {
                seq_play_scheduler_push_program_change(first_note_sample_time, track, program_0_127);
            }
        }
    }

finish:
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
uint16_t seq_play_scheduler_collect_due_events(seq_play_scheduler_event_t *out_events,
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
    uint16_t collection_limit = max_events;
    if (g_seq_play_control_window_active != 0U)
    {
        if (g_seq_play_control_window_remaining == 0U)
        {
            seq_play_scheduler_exit_critical(primask);
            return 0U;
        }
        if (collection_limit > g_seq_play_control_window_remaining)
            collection_limit = g_seq_play_control_window_remaining;
    }
    while (count < collection_limit)
    {
        uint16_t selected_index = UINT16_MAX;
        uint64_t selected_sample = 0U;

        for (uint16_t i = 0U; i < g_seq_play_event_count; ++i)
        {
            const seq_play_scheduler_evt_t *const candidate = &g_seq_play_events[i];
            if (candidate->control_dispatched != 0U)
            {
                continue;
            }
            if (candidate->generation != g_seq_play_generation)
            {
                g_seq_play_events[i].control_dispatched = 1U;
                continue;
            }
            if ((candidate->track >= SEQ_LANE_CAPACITY)
                    || (candidate->track_generation != g_seq_play_track_generation[candidate->track])
                    || ((g_seq_play_track_suspended[candidate->track] != 0U)
                        && (candidate->type != (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF)))
            {
                g_seq_play_events[i].control_dispatched = 1U;
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
        seq_play_scheduler_event_t out_evt;
        out_evt.type = evt.type;
        out_evt.track = evt.track;
        out_evt.note = evt.note;
        out_evt.velocity = evt.velocity;
        out_evt.track_generation = evt.track_generation;
        out_evt.reserved = 0U;
        out_evt.sample_abs = evt.due_sample_time;
        out_evt.generation = evt.generation;
        out_evt.event_token = evt.event_token;
        if (evt.due_sample_time < block_start_sample)
        {
            out_evt.sample_offset_in_block = 0U;
        }
        else
        {
            out_evt.sample_offset_in_block = (uint16_t)(evt.due_sample_time - block_start_sample);
        }
        if (out_evt.sample_offset_in_block >= block_frames)
        {
            out_evt.sample_offset_in_block = (uint16_t)(block_frames - 1U);
        }
        out_events[count++] = out_evt;

        g_seq_play_events[selected_index].control_dispatched = 1U;
    }

    uint16_t write = 0U;
    for (uint16_t read = 0U; read < g_seq_play_event_count; ++read)
    {
        const seq_play_scheduler_evt_t evt = g_seq_play_events[read];
        if (evt.control_dispatched != 0U)
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
    if (g_seq_play_control_window_active != 0U)
    {
        g_seq_play_control_window_remaining = (uint16_t)(
            g_seq_play_control_window_remaining - count);
        g_seq_play_control_window_used = (uint16_t)(g_seq_play_control_window_used + count);
    }
    seq_play_scheduler_exit_critical(primask);

    return count;
}

void seq_play_scheduler_control_begin_window(uint16_t event_quota)
{
    const uint32_t primask = seq_play_scheduler_enter_critical();
    g_seq_play_control_window_active = 1U;
    g_seq_play_control_window_remaining = event_quota;
    g_seq_play_control_window_used = 0U;
    seq_play_scheduler_exit_critical(primask);
}

void seq_play_scheduler_control_end_window(void)
{
    const uint32_t primask = seq_play_scheduler_enter_critical();
    g_seq_play_control_window_active = 0U;
    seq_play_scheduler_exit_critical(primask);
}

static void seq_play_scheduler_control_apply_internal(
    const seq_play_scheduler_event_t *event)
{
    /* Apply seam: dispatch a scheduler event to engines/output only. */
    if (event == NULL)
    {
        return;
    }

    if ((event->track >= SEQ_LANE_CAPACITY)
            || (event->generation != g_seq_play_generation)
            || (event->track_generation != g_seq_play_track_generation[event->track])
            || ((g_seq_play_track_suspended[event->track] != 0U)
                && (event->type != (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF)))
    {
        return;
    }

    if (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_PROGRAM_CHANGE)
    {
        seq_play_scheduler_emit_midi_program((seq_track_id_t)event->track, event->note);
        return;
    }

    const uint8_t is_note_on = (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON) ? 1U : 0U;
    if ((is_note_on != 0U) && (track_mute_should_suppress_note_on(event->track) != 0U))
    {
        return;
    }
    const uint8_t valid_note_key = ((event->track < SEQ_LANE_CAPACITY) && (event->note < 128U)) ? 1U : 0U;

    if (is_note_on == 0U)
    {
        if (valid_note_key != 0U)
        {
            if (seq_play_scheduler_active_occurrence_remove(event->track,
                                                       event->note,
                                                       event->event_token,
                                                       event->generation) == 0U)
            {
                return;
            }
        }
    }
    else if (valid_note_key != 0U)
    {
        if (seq_play_scheduler_active_occurrence_add(event->track,
                                                event->note,
                                                event->event_token,
                                                event->generation) == 0U)
        {
            return;
        }
    }

    const note_fx_event_t note_event = {
        .sample_abs = event->sample_abs,
        .track = event->track,
        .destination_id = track_runtime_get_midi_channel_zero_based(event->track),
        .note = event->note,
        .velocity = is_note_on ? event->velocity : 0U,
        .kind = is_note_on ? NOTE_EVENT_KIND_ON : NOTE_EVENT_KIND_OFF,
        .provenance = NOTE_EVENT_SOURCE_STEP,
        .stage = NOTE_EVENT_STAGE_SOURCE,
        .flags = 0U,
        .source_token = event->event_token,
        .occurrence_id = event->event_token,
        .generation = event->generation
    };
    (void)note_fx_pipeline_submit_control(&note_event);
}

void seq_play_scheduler_control_apply_event(const seq_play_scheduler_event_t *event)
{
    if (event == NULL)
        return;

    /* MIDI never enters the CONTROL/AUDIO queue. */
    if (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_PROGRAM_CHANGE)
    {
        if ((event->track < SEQ_LANE_CAPACITY)
                && (event->generation == g_seq_play_generation)
                && (event->track_generation
                    == g_seq_play_track_generation[event->track]))
            seq_play_scheduler_emit_midi_program(
                (seq_track_id_t)event->track, event->note);
        return;
    }

    seq_play_scheduler_control_apply_internal(event);
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

    for (seq_track_id_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
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
    if (track >= SEQ_LANE_CAPACITY)
        return;

    (void)seq_play_scheduler_transition_tracks(
        &track, 1U, SEQ_PLAY_TRANSITION_PATTERN_REPLACE);
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
