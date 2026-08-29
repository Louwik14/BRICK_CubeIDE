/*
 * Module: seq_play_scheduler
 * Role: Scheduler PLAY des événements note-on/note-off en sample-domain.
 * Responsibilities: lire steps/plocks, dériver voix note/vel/len/mictim,
 * planifier des événements horodatés en samples et les appliquer via le chemin audio.
 * Integration: scheduling déclenché par seq_runtime aux boundaries de step, collecte/apply en IRQ audio.
 */
#define SEQ_PLAY_SCHEDULER_IMPLEMENTATION 1
#include "Seq/seq_play_scheduler.h"

#include "Platform/memory_layout.h"

#include <stdint.h>
#include <string.h>
#include "stm32h7xx_hal.h"
#include "Track/track_runtime.h"
#include "Core/live_clock.h"
#include "Track/track_mute.h"
#include "Core/control_music_output.h"
#include "NoteFx/note_fx_pipeline.h"
#include "param_registry.h"
#include "Param/param_registry_runtime_state.h"
#include "midi.h"

#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_runtime_exec.h"

#define SEQ_PLAY_SCHEDULER_SOURCE_CAPACITY \
    (SEQ_LANE_CAPACITY * SEQ_PLAY_MAX_CAPACITY * 3U)
#define SEQ_PLAY_SCHEDULER_ACTIVE_OUTPUT_CAPACITY \
    (SEQ_LANE_CAPACITY * SEQ_PLAY_MAX_CAPACITY)
#define SEQ_PLAY_SCHEDULER_IMMINENT_CAPACITY 512U
#define SEQ_PLAY_SCHEDULER_HORIZON_FRAMES 64U
#define SEQ_PLAY_SCHEDULER_PRIORITY_COUNT 3U


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
    uint32_t generation;
    uint8_t track_generation;
    uint32_t event_token;
} seq_play_scheduler_evt_t;

typedef struct
{
    uint64_t step_origin_sample;
    uint64_t committed_until_sample;
    uint64_t step_span_q16;
    uint32_t samples_per_step_q16;
    seq_track_id_t target_track;
    seq_track_id_t source_track;
    seq_step_id_t source_step;
    uint8_t target_voice;
    uint8_t source_voice;
    uint8_t track_generation;
    uint8_t active;
    uint8_t swing_phase;
    uint32_t generation;
} seq_play_scheduler_source_t;

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
    uint8_t item_count;
    seq_play_scheduler_play_item_t items[SEQ_PLAY_MAX_CAPACITY];
} seq_play_scheduler_play_context_t;

SEQ_STATE_D2 static seq_play_scheduler_source_t
    g_seq_play_sources[SEQ_PLAY_SCHEDULER_SOURCE_CAPACITY];
SEQ_STATE_D2 static uint16_t
    g_seq_play_active_source[SEQ_PLAY_SCHEDULER_SOURCE_CAPACITY];
static uint16_t g_seq_play_active_source_count;
SEQ_STATE_D2 static seq_play_scheduler_evt_t
    g_seq_play_imminent[SEQ_PLAY_SCHEDULER_IMMINENT_CAPACITY];
static uint16_t g_seq_play_imminent_count;
SEQ_STATE_D2 static uint16_t
    g_seq_play_imminent_next[SEQ_PLAY_SCHEDULER_IMMINENT_CAPACITY];
SEQ_STATE_D2 static uint16_t
    g_seq_play_bucket_head[SEQ_PLAY_SCHEDULER_HORIZON_FRAMES]
                          [SEQ_PLAY_SCHEDULER_PRIORITY_COUNT];
SEQ_STATE_D2 static uint16_t
    g_seq_play_bucket_tail[SEQ_PLAY_SCHEDULER_HORIZON_FRAMES]
                          [SEQ_PLAY_SCHEDULER_PRIORITY_COUNT];
static uint16_t g_seq_play_imminent_cursor;
static uint8_t g_seq_play_bucket_frame;
static uint8_t g_seq_play_bucket_priority;
static uint64_t g_seq_play_imminent_block_start;
static uint16_t g_seq_play_imminent_block_frames;
static uint8_t g_seq_play_imminent_valid;
static uint8_t g_seq_play_generation;
static uint8_t g_seq_play_midi_program_valid[SEQ_LANE_CAPACITY];
static uint8_t g_seq_play_midi_program_last[SEQ_LANE_CAPACITY];
static uint32_t g_seq_play_next_event_token;

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
    uint8_t track;
    uint8_t source_track;
    uint8_t source_step;
    uint8_t source_voice;
    uint8_t target_voice;
    uint32_t output_id;
    uint32_t generation;
    uint32_t track_generation;
    uint64_t start_sample;
    uint64_t deadline_sample;
    uint32_t samples_per_step_q16;
} seq_play_active_occurrence_t;

SEQ_STATE_D2 static seq_play_active_occurrence_t
    g_seq_play_active_occurrence[SEQ_PLAY_SCHEDULER_ACTIVE_OUTPUT_CAPACITY];
static uint8_t g_seq_play_track_generation[SEQ_LANE_CAPACITY];
static uint8_t g_seq_play_track_suspended[SEQ_LANE_CAPACITY];

static void seq_play_scheduler_output_died(brick_entity_id_t entity_id,
                                           uint32_t output_id)
{
    (void)entity_id;
    for (uint16_t i = 0U;
         i < SEQ_PLAY_SCHEDULER_ACTIVE_OUTPUT_CAPACITY; ++i)
        if ((g_seq_play_active_occurrence[i].active != 0U)
                && (g_seq_play_active_occurrence[i].output_id == output_id))
            g_seq_play_active_occurrence[i].active = 0U;
    for (uint16_t i = 0U; i < g_seq_play_imminent_count; ++i)
        if ((g_seq_play_imminent[i].type
                == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF)
                && (g_seq_play_imminent[i].event_token == output_id))
            g_seq_play_imminent[i].event_token = 0U;
}
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
static uint32_t seq_play_scheduler_alloc_event_token(void);

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

static uint8_t seq_play_scheduler_track_supports_program_change(const track_runtime_descriptor_t *descriptor)
{
    if ((descriptor == NULL) || (descriptor->active == 0U))
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
    return (int32_t)((scaled + ((scaled >= 0LL) ? 50LL : -50LL)) / 100LL);
}

static uint64_t seq_play_scheduler_resolve_first_on_sample(
    seq_track_id_t track,
    uint8_t swing_phase,
    uint64_t nominal_sample,
    uint64_t step_span_q16,
    uint32_t samples_per_step_q16,
    float microtiming)
{
    uint8_t quant = 0U;
    uint8_t swing = 0U;
    (void)seq_runtime_get_track_quant(track, &quant);
    (void)seq_runtime_get_track_swing(track, &swing);
    if (quant > 100U) quant = 100U;
    if (swing > 100U) swing = 100U;

    const float samples_per_step =
        (float)samples_per_step_q16 / 65536.0f;
    const int32_t raw_microtiming_samples = (int32_t)(
        (microtiming * samples_per_step) / 96.0f);
    const int32_t effective_microtiming_samples =
        seq_play_scheduler_apply_quant_percent(raw_microtiming_samples, quant);

    uint64_t swing_samples = 0U;
    if (((swing_phase & 1U) != 0U) && (swing != 0U))
    {
        const uint64_t swing_q16 =
            ((step_span_q16 * (uint64_t)swing) + 100ULL) / 200ULL;
        swing_samples = (swing_q16 + 0x8000ULL) >> 16;
    }

    const uint64_t swung_sample = nominal_sample + swing_samples;
    if (effective_microtiming_samples >= 0)
        return swung_sample + (uint64_t)effective_microtiming_samples;

    const uint64_t early = (uint64_t)(-effective_microtiming_samples);
    return (early < swung_sample) ? (swung_sample - early) : 0U;
}

static void seq_play_scheduler_deactivate_source_at(uint16_t active_position)
{
    if (active_position >= g_seq_play_active_source_count)
        return;
    const uint16_t source_index = g_seq_play_active_source[active_position];
    g_seq_play_sources[source_index].active = 0U;
    --g_seq_play_active_source_count;
    g_seq_play_active_source[active_position] =
        g_seq_play_active_source[g_seq_play_active_source_count];
}


static uint8_t seq_play_scheduler_register_source(
    const seq_play_scheduler_play_item_t *item,
    uint64_t step_origin_sample,
    uint64_t step_span_q16,
    uint32_t samples_per_step_q16,
    uint8_t swing_phase)
{
    if ((item == NULL) || (item->target_track >= SEQ_LANE_CAPACITY)
            || (g_seq_play_track_suspended[item->target_track] != 0U))
        return 0U;

    for (uint16_t active = 0U; active < g_seq_play_active_source_count; ++active)
    {
        const uint16_t i = g_seq_play_active_source[active];
        seq_play_scheduler_source_t *const source = &g_seq_play_sources[i];
        if ((source->active != 0U)
                && (source->target_track == item->target_track)
                && (source->source_track == item->source_track)
                && (source->source_step == item->source_step)
                && (source->source_voice == item->source_voice)
                && (source->step_origin_sample == step_origin_sample))
            return 1U;
    }
    for (uint16_t i = 0U; i < SEQ_PLAY_SCHEDULER_SOURCE_CAPACITY; ++i)
    {
        if (g_seq_play_sources[i].active != 0U)
            continue;
        g_seq_play_sources[i] = (seq_play_scheduler_source_t){
            .step_origin_sample = step_origin_sample,
            .committed_until_sample = 0U,
            .step_span_q16 = step_span_q16,
            .samples_per_step_q16 = samples_per_step_q16,
            .target_track = item->target_track,
            .source_track = item->source_track,
            .source_step = item->source_step,
            .target_voice = item->target_voice,
            .source_voice = item->source_voice,
            .track_generation = g_seq_play_track_generation[item->target_track],
            .generation = g_seq_play_generation,
            .active = 1U,
            .swing_phase = swing_phase & 1U
        };
        g_seq_play_active_source[g_seq_play_active_source_count++] = i;
        g_seq_play_imminent_valid = 0U;
        return 1U;
    }
    return 0U;
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
    memset(g_seq_play_sources, 0, sizeof(g_seq_play_sources));
    g_seq_play_active_source_count = 0U;
    memset(g_seq_play_imminent, 0, sizeof(g_seq_play_imminent));
    g_seq_play_imminent_count = 0U;
    g_seq_play_imminent_cursor = 0U;
    g_seq_play_imminent_valid = 0U;
    g_seq_play_generation = 1U;
    g_seq_play_next_event_token = 0U;
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        g_seq_play_midi_program_valid[track] = 0U;
        g_seq_play_midi_program_last[track] = 0U;
    }
    memset(g_seq_play_active_occurrence, 0, sizeof(g_seq_play_active_occurrence));
    memset(g_seq_play_track_suspended, 0, sizeof(g_seq_play_track_suspended));
    (void)control_music_output_register_death_observer(
        seq_play_scheduler_output_died);
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        g_seq_play_track_generation[track] = 1U;
    }
}

static uint8_t seq_play_scheduler_remove_sources(
    const seq_track_id_t *tracks, uint8_t track_count, uint8_t close_terminal)
{
    if ((tracks == NULL) || (track_count == 0U))
        return 0U;
    uint8_t selected[SEQ_LANE_CAPACITY] = {0U};
    for (uint8_t i = 0U; i < track_count; ++i)
    {
        if (tracks[i] >= SEQ_LANE_CAPACITY)
            return 0U;
        selected[tracks[i]] = 1U;
    }

    uint32_t causes[SEQ_PLAY_SCHEDULER_ACTIVE_OUTPUT_CAPACITY];
    uint16_t cause_count = 0U;
    for (uint16_t i = 0U;
         i < SEQ_PLAY_SCHEDULER_ACTIVE_OUTPUT_CAPACITY; ++i)
    {
        const seq_play_active_occurrence_t *const active =
            &g_seq_play_active_occurrence[i];
        if ((active->active == 0U) || (selected[active->source_track] == 0U))
            continue;
        causes[cause_count++] = active->output_id;
    }

    uint64_t close_sample = 0U;
    (void)live_clock_read_audio_sample(&close_sample);
    close_sample = control_music_output_first_unpublished_sample(close_sample);
    if ((close_terminal != 0U) && (cause_count != 0U)
            && (control_music_output_close_causal_sources(
                causes, cause_count, close_sample) == 0U))
        return 0U;

    for (seq_track_id_t target = 0U;
         target < SEQ_LANE_CAPACITY; ++target)
    {
        uint32_t target_causes[SEQ_PLAY_MAX_CAPACITY];
        uint16_t target_count = 0U;
        for (uint16_t i = 0U;
             i < SEQ_PLAY_SCHEDULER_ACTIVE_OUTPUT_CAPACITY; ++i)
        {
            const seq_play_active_occurrence_t *const active =
                &g_seq_play_active_occurrence[i];
            if ((active->active != 0U) && (active->track == target)
                    && (selected[active->source_track] != 0U)
                    && (target_count < SEQ_PLAY_MAX_CAPACITY))
                target_causes[target_count++] = active->output_id;
        }
        if (target_count != 0U)
            (void)note_fx_pipeline_forget_causal_sources(
                target, target_causes, target_count);
    }

    uint16_t source_position = 0U;
    while (source_position < g_seq_play_active_source_count)
    {
        const uint16_t source_index = g_seq_play_active_source[source_position];
        if (selected[g_seq_play_sources[source_index].source_track] != 0U)
            seq_play_scheduler_deactivate_source_at(source_position);
        else
            ++source_position;
    }
    for (uint16_t i = 0U;
         i < SEQ_PLAY_SCHEDULER_ACTIVE_OUTPUT_CAPACITY; ++i)
        if ((g_seq_play_active_occurrence[i].active != 0U)
                && (selected[g_seq_play_active_occurrence[i].source_track] != 0U))
            g_seq_play_active_occurrence[i].active = 0U;
    for (uint16_t event = 0U; event < g_seq_play_imminent_count; ++event)
        for (uint16_t cause = 0U; cause < cause_count; ++cause)
            if (g_seq_play_imminent[event].event_token == causes[cause])
            {
                g_seq_play_imminent[event].event_token = 0U;
                break;
            }
    g_seq_play_imminent_valid = 0U;
    for (seq_track_id_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        if (selected[track] != 0U)
            g_seq_play_track_suspended[track] = 0U;
    return 1U;
}

void seq_play_scheduler_clear(void)
{
    seq_track_id_t tracks[SEQ_LANE_CAPACITY];
    for (seq_track_id_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        tracks[track] = track;
    (void)seq_play_scheduler_remove_sources(
        tracks, SEQ_LANE_CAPACITY, 0U);
    seq_play_scheduler_next_generation();
}

void seq_play_scheduler_clear_tracks(const seq_track_id_t *tracks, uint8_t track_count)
{
    if ((tracks == NULL) || (track_count == 0U))
        return;

    (void)seq_play_scheduler_remove_sources(tracks, track_count, 1U);
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
                                                     uint8_t swing_phase)
{
    entity_topology_descriptor_t entity;
    if ((entity_topology_get((brick_entity_id_t)track, &entity) == 0U)
            || (entity_topology_can_emit_notes(&entity) == 0U))
    {
        return;
    }


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
            == TRACK_RUNTIME_PARAM_UNAVAILABLE)
    {
        goto finish;
    }

    const uint64_t track_step_span_q16 = seq_play_scheduler_track_step_span_samples_q16(track, samples_per_step_q16);

    for (uint8_t voice = 0U; voice < play_context.item_count; ++voice)
    {
        const seq_play_scheduler_play_item_t *const item = &play_context.items[voice];
        const param_id_t note_id = seq_play_scheduler_param_note(item->target_voice);
        const param_id_t vel_id = seq_play_scheduler_param_vel(item->target_voice);
        /* Projection read: per-param status is a runtime guard, not a local recomputation. */
        if (track_runtime_get_effective_param_status(item->target_track, note_id) == TRACK_RUNTIME_PARAM_UNAVAILABLE)
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

        (void)seq_play_scheduler_register_source(
            item, step_sample_time, track_step_span_q16,
            samples_per_step_q16, swing_phase);
    }

finish:
}

void seq_play_scheduler_schedule_step(seq_track_id_t track,
                                      seq_step_id_t step,
                                      uint16_t ticks_per_step,
                                      uint32_t step_tick,
                                      uint64_t step_sample_time,
                                      uint32_t samples_per_step_q16,
                                      uint8_t swing_phase)
{
    /* Scheduling seam: consume resolved step boundaries and queue sample-domain events only. */
    (void)ticks_per_step;
    (void)step_tick;
    seq_play_scheduler_schedule_step_filtered(track,
                                              step,
                                              step_sample_time,
                                              samples_per_step_q16,
                                              swing_phase);
}

void seq_play_scheduler_schedule_step_lookahead_negative(seq_track_id_t track,
                                                         seq_step_id_t step,
                                                         uint64_t step_sample_time,
                                                         uint32_t samples_per_step_q16,
                                                         uint8_t swing_phase)
{
    seq_play_scheduler_schedule_step_filtered(track,
                                              step,
                                              step_sample_time,
                                              samples_per_step_q16,
                                              swing_phase);
}
uint16_t seq_play_scheduler_collect_due_events(seq_play_scheduler_event_t *out_events,
                                                       uint16_t max_events,
                                                       uint16_t block_frames,
                                                       uint64_t block_start_sample)
{
    if ((out_events == NULL) || (max_events == 0U))
        return 0U;
    if (block_frames == 0U)
        block_frames = 1U;

    if ((g_seq_play_imminent_valid == 0U)
            || (g_seq_play_imminent_block_start != block_start_sample)
            || (g_seq_play_imminent_block_frames != block_frames))
    {
        const uint64_t block_end_sample = block_start_sample + block_frames;
        g_seq_play_imminent_count = 0U;
        g_seq_play_imminent_cursor = UINT16_MAX;
        g_seq_play_bucket_frame = 0U;
        g_seq_play_bucket_priority = 0U;
        g_seq_play_imminent_block_start = block_start_sample;
        g_seq_play_imminent_block_frames = block_frames;
        g_seq_play_imminent_valid = 1U;

        /* LENGTH deadlines are the only long-lived future actions. */
        for (uint16_t i = 0U;
             i < SEQ_PLAY_SCHEDULER_ACTIVE_OUTPUT_CAPACITY; ++i)
        {
            seq_play_active_occurrence_t *const active =
                &g_seq_play_active_occurrence[i];
            if ((active->active == 0U)
                    || (active->deadline_sample >= block_end_sample))
                continue;
            if (g_seq_play_imminent_count
                    >= SEQ_PLAY_SCHEDULER_IMMINENT_CAPACITY)
                break;
            g_seq_play_imminent[g_seq_play_imminent_count++] =
                (seq_play_scheduler_evt_t){
                    .due_sample_time = (active->deadline_sample < block_start_sample)
                        ? block_start_sample : active->deadline_sample,
                    .track = active->track,
                    .note = active->note,
                    .velocity = 0U,
                    .type = (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF,
                    .generation = active->generation,
                    .track_generation = (uint8_t)active->track_generation,
                    .event_token = active->output_id
                };
        }

        uint16_t source_position = 0U;
        while (source_position < g_seq_play_active_source_count)
        {
            const uint16_t source_index =
                g_seq_play_active_source[source_position];
            seq_play_scheduler_source_t *const source =
                &g_seq_play_sources[source_index];
            if ((source->generation != g_seq_play_generation)
                    || (source->target_track >= SEQ_LANE_CAPACITY)
                    || (source->track_generation
                        != g_seq_play_track_generation[source->target_track])
                    || (g_seq_play_track_suspended[source->target_track] != 0U))
            {
                seq_play_scheduler_deactivate_source_at(source_position);
                continue;
            }

            const seq_play_scheduler_play_item_t item = {
                .target_track = source->target_track,
                .source_track = source->source_track,
                .source_step = source->source_step,
                .target_voice = source->target_voice,
                .source_voice = source->source_voice
            };
            const param_id_t note_id = seq_play_scheduler_param_note(item.target_voice);
            const param_id_t vel_id = seq_play_scheduler_param_vel(item.target_voice);
            const param_id_t len_id = seq_play_scheduler_param_len(item.target_voice);
            const param_id_t mictim_id = seq_play_scheduler_param_mictim(item.target_voice);
            const uint8_t note = (uint8_t)(seq_param_iface_decode_param_value(
                note_id, seq_play_scheduler_get_play_locked_or_default(
                    &item, SEQ_PLAY_SCHEDULER_PLAY_PARAM_NOTE)) + 0.5f);
            const uint8_t velocity = (uint8_t)(seq_param_iface_decode_param_value(
                vel_id, seq_play_scheduler_get_play_locked_or_default(
                    &item, SEQ_PLAY_SCHEDULER_PLAY_PARAM_VEL)) + 0.5f);
            float length_steps = seq_param_iface_decode_param_value(
                len_id, seq_play_scheduler_get_play_locked_or_default(
                    &item, SEQ_PLAY_SCHEDULER_PLAY_PARAM_LEN));
            const float mictim = seq_param_iface_decode_param_value(
                mictim_id, seq_play_scheduler_get_play_locked_or_default(
                    &item, SEQ_PLAY_SCHEDULER_PLAY_PARAM_MICTIM));
            if ((note >= 128U) || (velocity == 0U))
            {
                seq_play_scheduler_deactivate_source_at(source_position);
                continue;
            }
            if (length_steps < 1.0f) length_steps = 1.0f;
            if (length_steps > 64.0f) length_steps = 64.0f;
            const float samples_per_step =
                (float)source->samples_per_step_q16 / 65536.0f;
            const uint64_t first_on =
                seq_play_scheduler_resolve_first_on_sample(
                    source->target_track, source->swing_phase,
                    source->step_origin_sample, source->step_span_q16,
                    source->samples_per_step_q16, mictim);
            const uint8_t roll = seq_model_get_step_roll(
                source->source_track, source->source_step);
            const uint16_t divisor = seq_model_step_roll_divisor(roll);
            uint64_t interval_q16 = (divisor != 0U)
                ? ((source->step_span_q16 * 16ULL) / divisor)
                : source->step_span_q16;
            if (interval_q16 == 0U)
                interval_q16 = source->step_span_q16;
            const uint64_t length_samples = (uint64_t)(
                (length_steps * samples_per_step) + 0.5f);
            const uint64_t commit_floor = (source->committed_until_sample
                    > block_start_sample)
                ? source->committed_until_sample : block_start_sample;

            for (uint64_t offset_q16 = 0U;
                 offset_q16 < source->step_span_q16;
                 offset_q16 += interval_q16)
            {
                uint64_t on_sample = first_on
                    + ((offset_q16 + 0x8000ULL) >> 16);
                if (on_sample < commit_floor)
                {
                    /* A newly opened source may request a negative lead before
                     * the first mutable sample. Preserve the occurrence at the
                     * causal horizon; already-published sources stay skipped. */
                    if ((source->committed_until_sample == 0U)
                            && (offset_q16 == 0U)
                            && (source->step_origin_sample >= commit_floor))
                        on_sample = commit_floor;
                    else
                        continue;
                }
                if (on_sample >= block_end_sample)
                    continue;
                if ((g_seq_play_imminent_count + 2U)
                        > SEQ_PLAY_SCHEDULER_IMMINENT_CAPACITY)
                    break;

                uint8_t active_count = 0U;
                int16_t free_index = -1;
                int16_t oldest_index = -1;
                uint64_t oldest_start = UINT64_MAX;
                const track_runtime_ctx_t *const ctx =
                    track_runtime_get_ctx(source->target_track);
                uint8_t voice_limit = 1U;
                if ((ctx != NULL)
                        && ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                            || ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                                && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI))))
                {
                    float configured = 1.0f;
                    (void)param_registry_control_value_get(
                        source->target_track, PARAM_CFG_POLY_VOICES, &configured);
                    voice_limit = (configured >= 1.0f) ? (uint8_t)configured : 1U;
                    if (voice_limit > SEQ_PLAY_MAX_CAPACITY)
                        voice_limit = SEQ_PLAY_MAX_CAPACITY;
                }
                const uint16_t track_output_begin =
                    (uint16_t)source->target_track * SEQ_PLAY_MAX_CAPACITY;
                const uint16_t track_output_end =
                    track_output_begin + SEQ_PLAY_MAX_CAPACITY;
                for (uint16_t i = track_output_begin;
                     i < track_output_end; ++i)
                {
                    seq_play_active_occurrence_t *const active =
                        &g_seq_play_active_occurrence[i];
                    if (active->active == 0U)
                    {
                        if (free_index < 0) free_index = (int16_t)i;
                        continue;
                    }
                    if (active->track == source->target_track)
                    {
                        ++active_count;
                        if (active->start_sample < oldest_start)
                        {
                            oldest_start = active->start_sample;
                            oldest_index = (int16_t)i;
                        }
                    }
                }
                int16_t active_index = free_index;
                if (active_count >= voice_limit)
                    active_index = oldest_index;
                if (active_index < 0)
                    break;
                seq_play_active_occurrence_t *const active =
                    &g_seq_play_active_occurrence[(uint16_t)active_index];
                const uint32_t output_id = seq_play_scheduler_alloc_event_token();
                *active = (seq_play_active_occurrence_t){
                    .active = 1U,
                    .note = note,
                    .track = source->target_track,
                    .source_track = source->source_track,
                    .source_step = source->source_step,
                    .source_voice = source->source_voice,
                    .target_voice = source->target_voice,
                    .output_id = output_id,
                    .generation = source->generation,
                    .track_generation = source->track_generation,
                    .start_sample = on_sample,
                    .deadline_sample = on_sample
                        + ((length_samples != 0U) ? length_samples : 1U),
                    .samples_per_step_q16 = source->samples_per_step_q16
                };
                g_seq_play_imminent[g_seq_play_imminent_count++] =
                    (seq_play_scheduler_evt_t){
                        .due_sample_time = on_sample,
                        .track = source->target_track,
                        .note = note,
                        .velocity = velocity,
                        .type = (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON,
                        .generation = source->generation,
                        .track_generation = source->track_generation,
                        .event_token = output_id
                    };
            }
            source->committed_until_sample = block_end_sample;
            const uint64_t source_end = first_on
                + ((source->step_span_q16 + 0xFFFFULL) >> 16);
            if (block_end_sample >= source_end)
                seq_play_scheduler_deactivate_source_at(source_position);
            else
                ++source_position;
        }

        memset(g_seq_play_bucket_head, 0xFF, sizeof(g_seq_play_bucket_head));
        memset(g_seq_play_bucket_tail, 0xFF, sizeof(g_seq_play_bucket_tail));
        for (uint16_t i = 0U; i < g_seq_play_imminent_count; ++i)
        {
            const seq_play_scheduler_evt_t *const event = &g_seq_play_imminent[i];
            uint64_t offset = (event->due_sample_time <= block_start_sample)
                ? 0U : event->due_sample_time - block_start_sample;
            if (offset >= SEQ_PLAY_SCHEDULER_HORIZON_FRAMES)
                offset = SEQ_PLAY_SCHEDULER_HORIZON_FRAMES - 1U;
            const uint8_t priority = seq_play_scheduler_event_priority(event->type);
            const uint8_t frame = (uint8_t)offset;
            g_seq_play_imminent_next[i] = UINT16_MAX;
            const uint16_t tail = g_seq_play_bucket_tail[frame][priority];
            if (tail == UINT16_MAX)
                g_seq_play_bucket_head[frame][priority] = i;
            else
                g_seq_play_imminent_next[tail] = i;
            g_seq_play_bucket_tail[frame][priority] = i;
        }
    }

    uint16_t count = 0U;
    while (count < max_events)
    {
        while (g_seq_play_imminent_cursor == UINT16_MAX)
        {
            if (g_seq_play_bucket_priority >= SEQ_PLAY_SCHEDULER_PRIORITY_COUNT)
            {
                g_seq_play_bucket_priority = 0U;
                ++g_seq_play_bucket_frame;
            }
            if (g_seq_play_bucket_frame >= SEQ_PLAY_SCHEDULER_HORIZON_FRAMES)
                return count;
            g_seq_play_imminent_cursor =
                g_seq_play_bucket_head[g_seq_play_bucket_frame]
                                      [g_seq_play_bucket_priority++];
        }
        const seq_play_scheduler_evt_t *const event =
            &g_seq_play_imminent[g_seq_play_imminent_cursor];
        g_seq_play_imminent_cursor =
            g_seq_play_imminent_next[g_seq_play_imminent_cursor];
        if (event->event_token == 0U)
            continue;
        out_events[count++] = (seq_play_scheduler_event_t){
            .type = event->type,
            .track = event->track,
            .note = event->note,
            .velocity = event->velocity,
            .track_generation = event->track_generation,
            .sample_offset_in_block = (event->due_sample_time <= block_start_sample)
                ? 0U : (uint16_t)(event->due_sample_time - block_start_sample),
            .sample_abs = event->due_sample_time,
            .generation = event->generation,
            .event_token = event->event_token
        };
    }
    return count;
}

static uint8_t seq_play_scheduler_control_apply_internal(
    const seq_play_scheduler_event_t *event)
{
    /* Apply seam: dispatch a scheduler event to engines/output only. */
    if (event == NULL)
    {
        return 0U;
    }

    if ((event->track >= SEQ_LANE_CAPACITY)
            || (event->generation != g_seq_play_generation)
            || (event->track_generation != g_seq_play_track_generation[event->track])
            || ((g_seq_play_track_suspended[event->track] != 0U)
                && (event->type != (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_OFF)))
    {
        return 1U;
    }

    if (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_PROGRAM_CHANGE)
    {
        seq_play_scheduler_emit_midi_program((seq_track_id_t)event->track, event->note);
        return 1U;
    }

    const uint8_t is_note_on = (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_NOTE_ON) ? 1U : 0U;
    if ((is_note_on != 0U) && (track_mute_should_suppress_note_on(event->track) != 0U))
    {
        return 1U;
    }
    const note_event_t note_event = {
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
    return (note_fx_pipeline_submit_control(&note_event)
            == NOTE_EVENT_RESULT_ACCEPTED) ? 1U : 0U;
}

uint8_t seq_play_scheduler_control_apply_event(
    const seq_play_scheduler_event_t *event)
{
    if (event == NULL)
        return 0U;

    /* MIDI never enters the CONTROL/AUDIO queue. */
    if (event->type == (uint8_t)SEQ_PLAY_SCHEDULER_EVT_PROGRAM_CHANGE)
    {
        if ((event->track < SEQ_LANE_CAPACITY)
                && (event->generation == g_seq_play_generation)
                && (event->track_generation
                    == g_seq_play_track_generation[event->track]))
            seq_play_scheduler_emit_midi_program(
                (seq_track_id_t)event->track, event->note);
        return 1U;
    }

    return seq_play_scheduler_control_apply_internal(event);
}

void seq_play_scheduler_live_midi_program_changed(seq_track_id_t track, float program_value)
{
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

    seq_play_scheduler_clear_tracks(&track, 1U);
}

void seq_play_scheduler_notify_play_changed(seq_track_id_t track,
                                            seq_step_id_t step,
                                            uint8_t voice,
                                            seq_step_play_field_t field)
{
    g_seq_play_imminent_valid = 0U;
    if ((track >= SEQ_LANE_CAPACITY) || (voice >= SEQ_PLAY_MAX_CAPACITY)
            || (field != SEQ_STEP_PLAY_FIELD_LENGTH))
        return;

    const seq_play_scheduler_play_item_t item = {
        .target_track = track,
        .source_track = track,
        .source_step = step,
        .target_voice = voice,
        .source_voice = voice
    };
    const param_id_t length_id = seq_play_scheduler_param_len(voice);
    float length_steps = seq_param_iface_decode_param_value(
        length_id, seq_play_scheduler_get_play_locked_or_default(
            &item, SEQ_PLAY_SCHEDULER_PLAY_PARAM_LEN));
    if (length_steps < 1.0f) length_steps = 1.0f;
    if (length_steps > 64.0f) length_steps = 64.0f;
    for (uint16_t i = 0U;
         i < SEQ_PLAY_SCHEDULER_ACTIVE_OUTPUT_CAPACITY; ++i)
    {
        seq_play_active_occurrence_t *const active =
            &g_seq_play_active_occurrence[i];
        if ((active->active == 0U) || (active->source_track != track)
                || (active->source_step != step)
                || (active->source_voice != voice))
            continue;
        const float samples_per_step =
            (float)active->samples_per_step_q16 / 65536.0f;
        const uint64_t span = (uint64_t)(length_steps * samples_per_step + 0.5f);
        active->deadline_sample = active->start_sample
            + ((span != 0U) ? span : 1U);
    }
}

void seq_play_scheduler_remove_play(seq_track_id_t track,
                                    seq_step_id_t step,
                                    int16_t voice)
{
    if (track >= SEQ_LANE_CAPACITY)
        return;
    uint16_t source_position = 0U;
    while (source_position < g_seq_play_active_source_count)
    {
        const uint16_t source_index = g_seq_play_active_source[source_position];
        if ((g_seq_play_sources[source_index].source_track == track)
                && (g_seq_play_sources[source_index].source_step == step)
                && ((voice < 0)
                    || (g_seq_play_sources[source_index].source_voice
                        == (uint8_t)voice)))
            seq_play_scheduler_deactivate_source_at(source_position);
        else
            ++source_position;
    }

    uint64_t first_executable = seq_runtime_exec_get_sample_timeline();
    uint64_t audio_sample = 0U;
    if ((live_clock_read_audio_sample(&audio_sample) != 0U)
            && (audio_sample > first_executable))
        first_executable = audio_sample;
    for (uint16_t i = 0U;
         i < SEQ_PLAY_SCHEDULER_ACTIVE_OUTPUT_CAPACITY; ++i)
        if ((g_seq_play_active_occurrence[i].active != 0U)
                && (g_seq_play_active_occurrence[i].source_track == track)
                && (g_seq_play_active_occurrence[i].source_step == step)
                && ((voice < 0)
                    || (g_seq_play_active_occurrence[i].source_voice
                        == (uint8_t)voice)))
            g_seq_play_active_occurrence[i].deadline_sample = first_executable;
    g_seq_play_imminent_valid = 0U;
}

void seq_play_scheduler_notify_roll_changed(seq_track_id_t track,
                                            seq_step_id_t step)
{
    (void)track;
    (void)step;
    /* Sources are model-referenced; only uncommitted grid points are rebuilt. */
    g_seq_play_imminent_valid = 0U;
}
