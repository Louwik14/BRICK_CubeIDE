#include "NoteFx/note_fx_pipeline.h"
#include <string.h>
#include "stm32h7xx.h"

#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_runtime.h"
#include "NoteFx/note_fx_engine.h"
#include "NoteFx/note_fx_state.h"
#include "IPC/live_clock.h"
#include "Storage/project_load_quiesce.h"
#include "Track/track_runtime.h"
#include "Track/control_music_output.h"
#include "Seq/seq_runtime_exec.h"
#include "Platform/memory_layout.h"
#include "UI/ui_sampler_playhead.h"

static uint8_t g_note_fx_override_valid[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT][NOTE_FX_PARAM_COUNT];
static uint8_t g_note_fx_override_value[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT][NOTE_FX_PARAM_COUNT];
typedef struct
{
    uint8_t active;
    uint8_t note;
    uint8_t provenance;
    uint8_t reserved;
    uint32_t token;
    uint32_t generation;
} note_fx_source_record_t;

SEQ_STATE_D2 static note_fx_source_record_t
    g_note_fx_source_ledger[NOTE_FX_TRACK_COUNT][NOTE_FX_ARP_MAX_SOURCES];
static uint32_t g_note_fx_source_generation[NOTE_FX_TRACK_COUNT];

#define NOTE_FX_COMMAND_CAPACITY 32U
#define NOTE_FX_LIVE_QUEUE_CAPACITY (NOTE_FX_COMMAND_CAPACITY - 1U)
#define NOTE_FX_LIVE_STALE_THRESHOLD_SAMPLES 48000ULL

typedef enum
{
    NOTE_FX_COMMAND_SOURCE_EVENT = 0,
    NOTE_FX_COMMAND_SOURCE_RAW,
    NOTE_FX_COMMAND_CONFIGURE_TRACK
} note_fx_command_kind_t;

typedef struct
{
    uint8_t kind;
    uint8_t track;
    uint8_t is_note_on;
    uint8_t provenance;
    uint8_t reserved[3];
    uint64_t sample_time;
    uint32_t source_occurrence_id;
    uint32_t ingress_serial;
    uint32_t capture_tick;
    uint8_t capture_tick_valid;
    note_event_t event;
    uint8_t note;
    uint8_t velocity;
    uint8_t track_state_valid;
    note_fx_track_state_t track_state;
} note_fx_command_t;

CONTROL_M4_SRAM2 static note_fx_command_t g_note_fx_commands[NOTE_FX_COMMAND_CAPACITY];
static volatile uint8_t g_note_fx_command_head;
static volatile uint8_t g_note_fx_command_tail;
static live_note_event_t g_note_fx_live_queue[NOTE_FX_LIVE_QUEUE_CAPACITY];
static uint8_t g_note_fx_live_queue_count;
static uint32_t g_note_fx_live_fallback_serial;

static void note_fx_pipeline_output_died(brick_entity_id_t entity_id,
                                         uint32_t output_id)
{
    note_fx_engine_forget_output((uint8_t)entity_id, output_id);
}

uint8_t note_fx_pipeline_is_generated_occurrence_current(
    uint8_t track, uint32_t occurrence_id, uint32_t generation)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (occurrence_id == 0U)
            || (generation == 0U))
    {
        return 0U;
    }

    return note_fx_engine_is_generated_occurrence_current(
        track, occurrence_id, generation);
}

static uint32_t note_fx_pipeline_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return primask;
}

static void note_fx_pipeline_exit_critical(uint32_t primask)
{
    __DMB();
    __set_PRIMASK(primask);
}

static note_event_result_t note_fx_pipeline_stage_emit(const note_event_t *event,
                                                     void *context);

static uint8_t note_fx_pipeline_enqueue(const note_fx_command_t *command)
{
    if (command == NULL)
    {
        return 0U;
    }

    const uint32_t primask = note_fx_pipeline_enter_critical();
    const uint8_t head = g_note_fx_command_head;
    const uint8_t next = (uint8_t)((head + 1U) % NOTE_FX_COMMAND_CAPACITY);
    if (next == g_note_fx_command_tail)
    {
        note_fx_pipeline_exit_critical(primask);
        return 0U;
    }

    g_note_fx_commands[head] = *command;
    g_note_fx_command_head = next;
    note_fx_pipeline_exit_critical(primask);
    return 1U;
}

static uint8_t note_fx_pipeline_dequeue(note_fx_command_t *command)
{
    if (command == NULL)
        return 0U;
    const uint32_t primask = note_fx_pipeline_enter_critical();
    const uint8_t tail = g_note_fx_command_tail;
    if (tail == g_note_fx_command_head)
    {
        note_fx_pipeline_exit_critical(primask);
        return 0U;
    }

    *command = g_note_fx_commands[tail];
    g_note_fx_command_tail = (uint8_t)((tail + 1U) % NOTE_FX_COMMAND_CAPACITY);
    note_fx_pipeline_exit_critical(primask);
    return 1U;
}

static int8_t note_fx_pipeline_find_source(uint8_t track,
                                            uint32_t source_occurrence_id,
                                            note_event_provenance_t provenance)
{
    for (uint8_t i = 0U; i < NOTE_FX_ARP_MAX_SOURCES; ++i)
    {
        const note_fx_source_record_t *const record =
            &g_note_fx_source_ledger[track][i];
        if ((record->active != 0U) && (record->token == source_occurrence_id)
                && (record->provenance == (uint8_t)provenance))
            return (int8_t)i;
    }
    return -1;
}

static uint8_t note_fx_pipeline_source_id_selected(
    uint32_t source_id, const uint32_t *source_ids, uint16_t source_count)
{
    for (uint16_t i = 0U; i < source_count; ++i)
        if (source_ids[i] == source_id)
            return 1U;
    return 0U;
}

uint8_t note_fx_pipeline_forget_causal_sources(
    uint8_t track, const uint32_t *causal_source_ids, uint16_t source_count)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (causal_source_ids == NULL)
            || (source_count == 0U))
        return 0U;
    const uint32_t primask = note_fx_pipeline_enter_critical();
    uint8_t read_index = g_note_fx_command_tail;
    uint8_t write_index = g_note_fx_command_tail;
    while (read_index != g_note_fx_command_head)
    {
        const note_fx_command_t command = g_note_fx_commands[read_index];
        read_index = (uint8_t)((read_index + 1U) % NOTE_FX_COMMAND_CAPACITY);
        uint32_t source_id = 0U;
        if ((command.kind == NOTE_FX_COMMAND_SOURCE_RAW)
                && (command.track == track))
            source_id = command.source_occurrence_id;
        else if ((command.kind == NOTE_FX_COMMAND_SOURCE_EVENT)
                && (command.event.track == track))
            source_id = command.event.source_token;
        if ((source_id != 0U)
                && (note_fx_pipeline_source_id_selected(
                    source_id, causal_source_ids, source_count) != 0U))
            continue;
        g_note_fx_commands[write_index] = command;
        write_index = (uint8_t)((write_index + 1U) % NOTE_FX_COMMAND_CAPACITY);
    }
    g_note_fx_command_head = write_index;
    for (uint8_t i = 0U; i < NOTE_FX_ARP_MAX_SOURCES; ++i)
        if ((g_note_fx_source_ledger[track][i].active != 0U)
                && (note_fx_pipeline_source_id_selected(
                    g_note_fx_source_ledger[track][i].token,
                    causal_source_ids, source_count) != 0U))
            g_note_fx_source_ledger[track][i].active = 0U;
    note_fx_pipeline_exit_critical(primask);
    for (uint16_t i = 0U; i < source_count; ++i)
        note_fx_engine_forget_causal_source(track, causal_source_ids[i]);
    return 1U;
}

static note_event_result_t note_fx_pipeline_terminal(const note_event_t *event, void *context)
{
    (void)context;
    if (!note_event_is_valid(event)
            || (event->track >= NOTE_FX_TRACK_COUNT)
            || (event->stage > NOTE_EVENT_STAGE_TERMINAL))
    {
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    }
    note_event_t terminal = *event;
    terminal.stage = NOTE_EVENT_STAGE_TERMINAL;
    terminal.flags |= NOTE_EVENT_FLAG_TERMINAL;
    const uint8_t channel = (terminal.destination_id == NOTE_EVENT_DESTINATION_DEFAULT)
        ? track_runtime_get_midi_channel_zero_based(terminal.track)
        : terminal.destination_id;
    const uint8_t external_flag = (uint8_t)(
        (((terminal.occurrence_id
            & (uint32_t)~NOTE_EVENT_OCCURRENCE_COUNTER_MASK)
            == NOTE_EVENT_OCCURRENCE_NAMESPACE_KEY)
        || ((terminal.occurrence_id
            & (uint32_t)~NOTE_EVENT_OCCURRENCE_COUNTER_MASK)
            == NOTE_EVENT_OCCURRENCE_NAMESPACE_MIDI))
        ? CONTROL_MUSIC_ACTION_EXTERNAL_FLAG : 0U);
    const control_music_action_t audio_event = {
        .due_sample = terminal.sample_abs,
        .output_id = terminal.occurrence_id,
        .kind = (uint8_t)(((terminal.kind == NOTE_EVENT_KIND_ON)
            ? CONTROL_MUSIC_ACTION_START : CONTROL_MUSIC_ACTION_STOP)
            | external_flag
            | (uint8_t)(channel << CONTROL_MUSIC_ACTION_CHANNEL_SHIFT)),
        .entity_id = terminal.track,
        .note = terminal.note,
        .velocity = terminal.velocity
    };
    if (control_music_output_submit(&audio_event, terminal.source_token,
                                    terminal.generation) == 0U)
    {
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    }
    if (terminal.kind == NOTE_EVENT_KIND_ON)
        ui_sampler_playhead_note_trigger(terminal.track, terminal.sample_abs);
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static note_event_result_t note_fx_pipeline_stage_emit(const note_event_t *event,
                                                     void *context)
{
    (void)context;
    if (!note_event_is_valid(event) || (event->track >= NOTE_FX_TRACK_COUNT))
    {
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    }
    note_event_t admitted = *event;

    note_event_result_t result;
    /* Stage 3 is emitted after the third slot and must enter the common
     * terminal ledger; there is no fourth engine slot. */
    if (note_event_is_terminal_handoff(&admitted) != 0U)
    {
        result = note_fx_pipeline_terminal(&admitted, NULL);
    }
    else
    {
        result = note_fx_engine_stage_source(&admitted, admitted.stage,
                                             note_fx_pipeline_stage_emit, NULL);
    }
    return result;
}

static uint8_t note_fx_pipeline_configure_track_owner(
    uint8_t track, const note_fx_track_state_t *state)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (state == NULL))
    {
        return 0U;
    }
    const uint64_t sample = control_music_output_first_unpublished_sample(
        live_clock_audio_sample());
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        uint8_t value[NOTE_FX_PARAM_COUNT];
        for (uint8_t param = 0U; param < NOTE_FX_PARAM_COUNT; ++param)
            value[param] = g_note_fx_override_valid[track][slot][param]
                ? g_note_fx_override_value[track][slot][param]
                : state->value[slot][param];
        if (note_fx_engine_configure(
                track, slot, value[3], value[0], value[1], value[2],
                sample, note_fx_pipeline_stage_emit, NULL)
                != NOTE_EVENT_RESULT_ACCEPTED)
            return 0U;
    }
    return 1U;
}

void note_fx_pipeline_init(void)
{
    memset(g_note_fx_override_valid, 0, sizeof(g_note_fx_override_valid));
    memset(g_note_fx_source_ledger, 0, sizeof(g_note_fx_source_ledger));
    memset(g_note_fx_commands, 0, sizeof(g_note_fx_commands));
    memset(g_note_fx_live_queue, 0, sizeof(g_note_fx_live_queue));
    g_note_fx_command_head = 0U;
    g_note_fx_command_tail = 0U;
    g_note_fx_live_queue_count = 0U;
    g_note_fx_live_fallback_serial = 0U;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        g_note_fx_source_generation[track] = 1U;
    note_fx_engine_init();
    (void)control_music_output_register_death_observer(
        note_fx_pipeline_output_died);
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        note_fx_track_state_t state;
        if (note_fx_state_capture_track(track, &state) != 0U)
            (void)note_fx_pipeline_configure_track_owner(track, &state);
    }
}

uint16_t note_fx_pipeline_diagnostic_queue_depth(void)
{
    const uint32_t primask = note_fx_pipeline_enter_critical();
    const uint16_t depth = (uint16_t)((g_note_fx_command_head
        + NOTE_FX_COMMAND_CAPACITY - g_note_fx_command_tail)
        % NOTE_FX_COMMAND_CAPACITY);
    note_fx_pipeline_exit_critical(primask);
    return depth;
}

uint8_t note_fx_pipeline_apply_control_override(uint8_t track, uint8_t slot,
                                                uint8_t param, uint8_t value)
{
    if (track >= NOTE_FX_TRACK_COUNT || slot >= NOTE_FX_SLOT_COUNT ||
        param >= NOTE_FX_PARAM_COUNT)
    {
        return 0U;
    }
    note_fx_track_state_t state;
    if (note_fx_state_capture_track(track, &state) == 0U)
        return 0U;
    g_note_fx_override_valid[track][slot][param] = 1U;
    g_note_fx_override_value[track][slot][param] = value;
    return note_fx_pipeline_configure_track_owner(track, &state);
}

uint8_t note_fx_pipeline_release_control_override(uint8_t track, uint8_t slot,
                                                  uint8_t param)
{
    if (track >= NOTE_FX_TRACK_COUNT || slot >= NOTE_FX_SLOT_COUNT ||
        param >= NOTE_FX_PARAM_COUNT)
    {
        return 0U;
    }
    note_fx_track_state_t state;
    if (note_fx_state_capture_track(track, &state) == 0U)
        return 0U;
    g_note_fx_override_valid[track][slot][param] = 0U;
    return note_fx_pipeline_configure_track_owner(track, &state);
}

note_event_result_t note_fx_pipeline_submit_control(const note_event_t *event)
{
    if (!note_event_is_valid(event) || (event->track >= NOTE_FX_TRACK_COUNT))
    {
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    }

    note_event_t source = *event;
    if (source.destination_id == NOTE_EVENT_DESTINATION_DEFAULT)
    {
        source.destination_id = track_runtime_get_midi_channel_zero_based(source.track);
    }
    const note_event_result_t result =
        note_fx_engine_stage_source(&source, 0U, note_fx_pipeline_stage_emit, 0);
    return result;
}

note_event_result_t note_fx_pipeline_submit(const note_event_t *event)
{
    if (!note_event_is_valid(event) || (event->track >= NOTE_FX_TRACK_COUNT))
    {
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    }

    note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_SOURCE_EVENT,
        .track = event->track,
        .event = *event
    };
    if (command.event.destination_id == NOTE_EVENT_DESTINATION_DEFAULT)
    {
        command.event.destination_id =
            track_runtime_get_midi_channel_zero_based(command.event.track);
    }
    return (note_fx_pipeline_enqueue(&command) != 0U)
        ? NOTE_EVENT_RESULT_ACCEPTED
        : NOTE_EVENT_RESULT_REJECTED_CAPACITY;
}

static note_event_result_t note_fx_pipeline_submit_source_control(uint8_t track, uint8_t note,
                                                              uint8_t velocity, uint8_t is_note_on,
                                                              uint64_t sample_time,
                                                              note_event_provenance_t provenance,
                                                              uint32_t source_occurrence_id)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (note >= 128U)
            || (provenance >= NOTE_EVENT_SOURCE_COUNT)
            || (source_occurrence_id == 0U))
    {
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    }

    if (sample_time == NOTE_FX_SAMPLE_TIME_CONTROL_ANCHOR)
    {
        sample_time = live_clock_audio_sample();
    }
    sample_time = control_music_output_first_unpublished_sample(sample_time);

    int8_t source_index = -1;
    uint32_t token = 0U;
    uint32_t generation = g_note_fx_source_generation[track];
    if (is_note_on != 0U)
    {
        for (uint8_t i = 0U; i < NOTE_FX_ARP_MAX_SOURCES; ++i)
        {
            if (g_note_fx_source_ledger[track][i].active == 0U)
            {
                source_index = (int8_t)i;
                break;
            }
        }
        if (source_index < 0)
        {
            return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
        }
        if (note_fx_pipeline_find_source(track, source_occurrence_id,
                                         provenance) >= 0)
        {
            return NOTE_EVENT_RESULT_REJECTED_STALE;
        }
        token = source_occurrence_id;
        g_note_fx_source_ledger[track][(uint8_t)source_index] = (note_fx_source_record_t){
            .active = 1U,
            .note = note,
            .provenance = (uint8_t)provenance,
            .token = token,
            .generation = generation
        };
    }
    else
    {
        source_index = note_fx_pipeline_find_source(track, source_occurrence_id,
                                                     provenance);
        if (source_index < 0)
        {
            return NOTE_EVENT_RESULT_REJECTED_STALE;
        }
        token = g_note_fx_source_ledger[track][(uint8_t)source_index].token;
        generation = g_note_fx_source_ledger[track][(uint8_t)source_index].generation;
    }

    const note_event_t event = {
        .sample_abs = sample_time,
        .track = track,
        .destination_id = NOTE_EVENT_DESTINATION_DEFAULT,
        .note = note,
        .velocity = (is_note_on != 0U) ? velocity : 0U,
        .kind = (is_note_on != 0U) ? NOTE_EVENT_KIND_ON : NOTE_EVENT_KIND_OFF,
        .provenance = (uint8_t)provenance,
        .stage = NOTE_EVENT_STAGE_SOURCE,
        .flags = 0U,
        .source_token = token,
        .occurrence_id = token,
        .generation = generation
    };
    const note_event_result_t result = note_fx_pipeline_submit_control(&event);
    if (is_note_on == 0U)
    {
        if ((result == NOTE_EVENT_RESULT_ACCEPTED) && (source_index >= 0))
        {
            g_note_fx_source_ledger[track][(uint8_t)source_index].active = 0U;
        }
    }
    else if (result != NOTE_EVENT_RESULT_ACCEPTED)
    {
        g_note_fx_source_ledger[track][(uint8_t)source_index].active = 0U;
    }
    return result;
}

note_event_result_t note_fx_pipeline_submit_source_occurrence(
    uint8_t track, uint8_t note, uint8_t velocity, uint8_t is_note_on,
    uint64_t sample_time, note_event_provenance_t provenance,
    uint32_t source_occurrence_id)
{
    if (project_load_ingress_is_open() == 0U)
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    if ((track >= NOTE_FX_TRACK_COUNT) || (note >= 128U)
            || (provenance >= NOTE_EVENT_SOURCE_COUNT)
            || (source_occurrence_id == 0U))
    {
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    }

    const note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_SOURCE_RAW,
        .track = track,
        .note = note,
        .velocity = velocity,
        .is_note_on = (is_note_on != 0U) ? 1U : 0U,
        .provenance = (uint8_t)provenance,
        .sample_time = sample_time,
        .source_occurrence_id = source_occurrence_id
    };
    return (note_fx_pipeline_enqueue(&command) != 0U)
        ? NOTE_EVENT_RESULT_ACCEPTED
        : NOTE_EVENT_RESULT_REJECTED_CAPACITY;
}

note_event_result_t note_fx_pipeline_submit_source_capture_tick(
    uint8_t track, uint8_t note, uint8_t velocity, uint8_t is_note_on,
    uint32_t capture_tick, uint32_t ingress_serial,
    note_event_provenance_t provenance,
    uint32_t source_occurrence_id)
{
    if (project_load_ingress_is_open() == 0U)
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    if ((track >= NOTE_FX_TRACK_COUNT) || (note >= 128U)
            || (provenance >= NOTE_EVENT_SOURCE_COUNT)
            || (source_occurrence_id == 0U))
    {
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    }

    const note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_SOURCE_RAW,
        .track = track,
        .note = note,
        .velocity = velocity,
        .is_note_on = (is_note_on != 0U) ? 1U : 0U,
        .provenance = (uint8_t)provenance,
        .sample_time = NOTE_FX_SAMPLE_TIME_CONTROL_ANCHOR,
        .source_occurrence_id = source_occurrence_id,
        .ingress_serial = ingress_serial,
        .capture_tick = capture_tick,
        .capture_tick_valid = 1U
    };
    return (note_fx_pipeline_enqueue(&command) != 0U)
        ? NOTE_EVENT_RESULT_ACCEPTED
        : NOTE_EVENT_RESULT_REJECTED_CAPACITY;
}

static uint8_t note_fx_pipeline_live_source_for_provenance(
    note_event_provenance_t provenance)
{
    return (provenance == NOTE_EVENT_SOURCE_KEY)
        ? LIVE_EVENT_SOURCE_HALL
        : LIVE_EVENT_SOURCE_MIDI_DEVICE;
}

static uint8_t note_fx_pipeline_live_event_precedes(
    const live_note_event_t *left, const live_note_event_t *right)
{
    if (left->sample_time != right->sample_time)
        return (left->sample_time < right->sample_time) ? 1U : 0U;
    return (left->ingress_serial < right->ingress_serial) ? 1U : 0U;
}

static uint8_t note_fx_pipeline_live_enqueue(
    const live_note_event_t *event)
{
    if ((event == NULL)
            || (g_note_fx_live_queue_count >= NOTE_FX_LIVE_QUEUE_CAPACITY))
    {
        return 0U;
    }

    uint8_t index = g_note_fx_live_queue_count;
    while ((index > 0U)
            && note_fx_pipeline_live_event_precedes(
                event, &g_note_fx_live_queue[(uint8_t)(index - 1U)]))
    {
        g_note_fx_live_queue[index] = g_note_fx_live_queue[(uint8_t)(index - 1U)];
        --index;
    }
    g_note_fx_live_queue[index] = *event;
    ++g_note_fx_live_queue_count;
    return 1U;
}

static note_event_result_t note_fx_pipeline_submit_live_command(
    const note_fx_command_t *command, uint64_t now)
{
    if (command->capture_tick_valid == 0U)
    {
        return note_fx_pipeline_submit_source_control(
            command->track, command->note, command->velocity,
            command->is_note_on, command->sample_time,
            (note_event_provenance_t)command->provenance,
            command->source_occurrence_id);
    }

    uint64_t sample_time = 0U;
    if (!live_clock_tim5_to_guarded_sample_time(command->capture_tick,
                                                &sample_time))
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;

    if (sample_time < now)
    {
        sample_time = now;
    }

    live_note_event_t event = {
        .sample_time = sample_time,
        .ingress_serial = command->ingress_serial,
        .occurrence_id = command->source_occurrence_id,
        .type = (command->is_note_on != 0U)
            ? LIVE_NOTE_EVENT_ON : LIVE_NOTE_EVENT_OFF,
        .source = note_fx_pipeline_live_source_for_provenance(
            (note_event_provenance_t)command->provenance),
        .track = command->track,
        .note = command->note,
        .velocity = command->velocity
    };
    if (event.ingress_serial == 0U)
    {
        ++g_note_fx_live_fallback_serial;
        if (g_note_fx_live_fallback_serial == 0U)
            ++g_note_fx_live_fallback_serial;
        event.ingress_serial = g_note_fx_live_fallback_serial;
    }
    if (note_fx_pipeline_live_enqueue(&event) == 0U)
    {
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    }

    (void)seq_runtime_live_rec_submit_effective(
        (event.source == LIVE_EVENT_SOURCE_HALL)
            ? SEQ_LIVE_REC_SRC_INTERNAL : SEQ_LIVE_REC_SRC_EXTERNAL,
        (event.type == LIVE_NOTE_EVENT_ON) ? 1U : 0U,
        track_runtime_get_midi_channel_zero_based(event.track),
        event.note,
        event.velocity,
        event.sample_time,
        event.ingress_serial,
        event.occurrence_id);
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static uint8_t note_fx_pipeline_apply_due_live_events(uint64_t now)
{
    while ((g_note_fx_live_queue_count != 0U)
            && (g_note_fx_live_queue[0].sample_time <= now))
    {
        const live_note_event_t event = g_note_fx_live_queue[0];
        for (uint8_t i = 1U; i < g_note_fx_live_queue_count; ++i)
            g_note_fx_live_queue[(uint8_t)(i - 1U)] = g_note_fx_live_queue[i];
        --g_note_fx_live_queue_count;

        const note_event_provenance_t provenance =
            (event.source == LIVE_EVENT_SOURCE_HALL)
                ? NOTE_EVENT_SOURCE_KEY : NOTE_EVENT_SOURCE_MIDI;
        note_fx_command_t command = {
            .kind = NOTE_FX_COMMAND_SOURCE_RAW,
            .track = event.track,
            .note = event.note,
            .velocity = event.velocity,
            .is_note_on = (event.type == LIVE_NOTE_EVENT_ON) ? 1U : 0U,
            .provenance = (uint8_t)provenance,
            .sample_time = event.sample_time,
            .source_occurrence_id = event.occurrence_id,
            .ingress_serial = event.ingress_serial
        };
        if (note_fx_pipeline_submit_source_control(
                command.track, command.note, command.velocity,
                command.is_note_on, event.sample_time, provenance,
                command.source_occurrence_id) != NOTE_EVENT_RESULT_ACCEPTED)
            return 0U;
    }
    return 1U;
}

static note_event_result_t note_fx_pipeline_apply_source_raw_command(
    const note_fx_command_t *command)
{
    const uint64_t now = live_clock_audio_sample();
    return note_fx_pipeline_submit_live_command(command, now);
}

static uint8_t note_fx_pipeline_apply_pending_commands(void)
{
    note_fx_command_t command;
    while (note_fx_pipeline_dequeue(&command) != 0U)
    {
        switch ((note_fx_command_kind_t)command.kind)
        {
            case NOTE_FX_COMMAND_SOURCE_EVENT:
                if (note_fx_pipeline_submit_control(&command.event)
                        != NOTE_EVENT_RESULT_ACCEPTED)
                    return 0U;
                break;
            case NOTE_FX_COMMAND_SOURCE_RAW:
                if (note_fx_pipeline_apply_source_raw_command(&command)
                        != NOTE_EVENT_RESULT_ACCEPTED)
                    return 0U;
                break;
            case NOTE_FX_COMMAND_CONFIGURE_TRACK:
                if (command.track_state_valid != 0U)
                    if (note_fx_pipeline_configure_track_owner(
                            command.track, &command.track_state) == 0U)
                        return 0U;
                break;
            default:
                break;
        }
    }
    return 1U;
}

void note_fx_pipeline_panic(void)
{
    const uint32_t primask = note_fx_pipeline_enter_critical();
    g_note_fx_command_tail = g_note_fx_command_head;
    memset(g_note_fx_source_ledger, 0, sizeof(g_note_fx_source_ledger));
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        ++g_note_fx_source_generation[track];
        if (g_note_fx_source_generation[track] == 0U)
            g_note_fx_source_generation[track] = 1U;
    }
    memset(g_note_fx_live_queue, 0, sizeof(g_note_fx_live_queue));
    g_note_fx_live_queue_count = 0U;
    note_fx_pipeline_exit_critical(primask);
    const uint64_t sample = control_music_output_first_unpublished_sample(
        live_clock_audio_sample());
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        (void)note_fx_engine_cleanup(track, sample, NULL, NULL);
}

uint8_t note_fx_pipeline_configure_track(uint8_t track)
{
    if (track >= NOTE_FX_TRACK_COUNT)
    {
        return 0U;
    }
    note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_CONFIGURE_TRACK,
        .track = track
    };
    command.track_state_valid = note_fx_state_capture_track(
        track, &command.track_state);
    if (command.track_state_valid == 0U)
        return 0U;
    return note_fx_pipeline_enqueue(&command);
}

uint8_t note_fx_pipeline_process(uint64_t block_start, uint16_t frames,
                                 uint32_t samples_per_step_q16)
{
    if (frames != 0U)
        if (note_fx_pipeline_apply_due_live_events(
                block_start + frames - 1U) == 0U)
            return 0U;
    return (note_fx_engine_process(
        block_start, frames, samples_per_step_q16,
        note_fx_pipeline_stage_emit, 0) == NOTE_EVENT_RESULT_ACCEPTED) ? 1U : 0U;
}

uint8_t note_fx_pipeline_apply_pending(void)
{
    return note_fx_pipeline_apply_pending_commands();
}
