#include "NoteFx/note_fx_pipeline.h"
#include <string.h>
#include "stm32h7xx.h"

#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_runtime.h"
#include "NoteFx/note_fx_engine.h"
#include "NoteFx/note_fx_state.h"
#include "ControlRT/control_rt_publication.h"
#include "Storage/project_load_quiesce.h"
#include "Track/track_runtime.h"
#include "Track/control_music_output.h"
#include "Track/entity_topology.h"
#include "Seq/seq_runtime_exec.h"
#include "Seq/seq_model.h"
#include "IPC/control_music_capacity.h"
#include "Platform/memory_layout.h"
#include "Seq/seq_note_trace.h"
#include "main.h"

static uint8_t g_note_fx_override_valid[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT][NOTE_FX_PARAM_COUNT];
static uint8_t g_note_fx_override_value[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT][NOTE_FX_PARAM_COUNT];
static uint32_t g_note_fx_source_generation[NOTE_FX_TRACK_COUNT];

#define NOTE_FX_FUTURE_CAPACITY \
    (NOTE_FX_TRACK_COUNT * NOTE_FX_SLOT_COUNT * NOTE_FX_HELD_PITCH_CAPACITY)

typedef struct
{
    note_event_t event;
    uint8_t resume_slot;
} note_fx_future_t;

static note_event_t g_note_fx_buffer_a[NOTE_FX_BATCH_CAPACITY];
static note_event_t g_note_fx_buffer_b[NOTE_FX_BATCH_CAPACITY];
CONTROL_M4_SRAM2 static note_fx_future_t
    g_note_fx_future[NOTE_FX_FUTURE_CAPACITY];
static uint16_t g_note_fx_future_count;
static uint16_t g_note_fx_chain_generation[NOTE_FX_TRACK_COUNT];
static uint8_t g_note_fx_applied[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT]
                                [NOTE_FX_PARAM_COUNT];
static uint8_t g_note_fx_applied_valid[NOTE_FX_TRACK_COUNT];
static uint16_t g_note_fx_admitted_future[NOTE_FX_TRACK_COUNT];
static uint8_t g_note_fx_source_note_limit[NOTE_FX_TRACK_COUNT];
static uint8_t g_note_fx_admitted_fanout[NOTE_FX_TRACK_COUNT];
static note_event_t g_note_fx_held_source[NOTE_FX_TRACK_COUNT]
                                         [NOTE_FX_HELD_PITCH_CAPACITY];
static uint8_t g_note_fx_held_source_count[NOTE_FX_TRACK_COUNT];
static uint64_t g_note_fx_window_start;
static uint64_t g_note_fx_window_end;
static uint8_t g_note_fx_window_active;

_Static_assert(NOTE_FX_FUTURE_CAPACITY == 512U,
               "Note FX future proof changed");

#define NOTE_FX_COMMAND_CAPACITY 32U
#define NOTE_FX_LIVE_QUEUE_CAPACITY (NOTE_FX_COMMAND_CAPACITY - 1U)
#define NOTE_FX_LIVE_STALE_THRESHOLD_SAMPLES 48000ULL

typedef enum
{
    NOTE_FX_COMMAND_SOURCE_RAW = 0,
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
static note_event_result_t note_fx_pipeline_schedule_future(
    const note_event_t *event);
static note_event_result_t note_fx_pipeline_run_batch(const note_event_t *events,
                                                       uint8_t event_count);

static int8_t note_fx_pipeline_held_source_find(uint8_t track,
                                                uint32_t occurrence_id)
{
    for (uint8_t i = 0U; i < g_note_fx_held_source_count[track]; ++i)
        if (g_note_fx_held_source[track][i].occurrence_id == occurrence_id)
            return (int8_t)i;
    return -1;
}

static int8_t note_fx_pipeline_held_pitch_find(const note_event_t *event)
{
    for (uint8_t i = 0U;
         i < g_note_fx_held_source_count[event->track]; ++i)
        if ((g_note_fx_held_source[event->track][i].note == event->note)
                && (g_note_fx_held_source[event->track][i].destination_id
                    == event->destination_id))
            return (int8_t)i;
    return -1;
}

static uint8_t note_fx_pipeline_held_source_update(const note_event_t *event)
{
    if ((event == NULL) || (event->track >= NOTE_FX_TRACK_COUNT)
            || (event->stage != NOTE_EVENT_STAGE_SOURCE))
        return 0U;
    const uint8_t track = event->track;
    const int8_t found = note_fx_pipeline_held_source_find(
        track, event->occurrence_id);
    if (event->kind == NOTE_EVENT_KIND_OFF)
    {
        if (found >= 0)
        {
            const uint8_t index = (uint8_t)found;
            --g_note_fx_held_source_count[track];
            g_note_fx_held_source[track][index] =
                g_note_fx_held_source[track][g_note_fx_held_source_count[track]];
        }
        return 1U;
    }
    uint8_t index;
    const int8_t pitch = note_fx_pipeline_held_pitch_find(event);
    if (found >= 0)
        index = (uint8_t)found;
    else if (pitch >= 0)
        index = (uint8_t)pitch;
    else
    {
        if (g_note_fx_held_source_count[track]
                >= NOTE_FX_HELD_PITCH_CAPACITY)
            return 0U;
        index = g_note_fx_held_source_count[track]++;
    }
    g_note_fx_held_source[track][index] = *event;
    return 1U;
}

static note_event_result_t note_fx_pipeline_replay_grouped(
    note_event_t *events, uint8_t count, uint8_t stage, uint64_t sample,
    uint16_t chain_generation)
{
    uint8_t consumed_mask = 0U;
    uint8_t consumed_count = 0U;
    while (consumed_count != count)
    {
        note_event_t group[NOTE_FX_HELD_PITCH_CAPACITY];
        uint8_t group_count = 0U;
        uint8_t first = 0U;
        while ((consumed_mask & (uint8_t)(1U << first)) != 0U) ++first;
        const uint32_t group_id = events[first].group_id;
        for (uint8_t read = first; read < count; ++read)
            if (((consumed_mask & (uint8_t)(1U << read)) == 0U)
                    && (events[read].group_id == group_id))
            {
                note_event_t event = events[read];
                event.sample_abs = sample;
                event.duration_samples = NOTE_EVENT_DURATION_OPEN;
                event.chain_generation = chain_generation;
                event.stage = stage;
                event.kind = NOTE_EVENT_KIND_ON;
                event.flags &= (uint8_t)~(NOTE_EVENT_FLAG_FUTURE
                    | NOTE_EVENT_FLAG_TERMINAL);
                group[group_count++] = event;
                consumed_mask |= (uint8_t)(1U << read);
                ++consumed_count;
            }
        const note_event_result_t result = note_fx_pipeline_run_batch(
            group, group_count);
        if (result != NOTE_EVENT_RESULT_ACCEPTED) return result;
    }
    return NOTE_EVENT_RESULT_ACCEPTED;
}

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
        if ((source_id != 0U)
                && (note_fx_pipeline_source_id_selected(
                    source_id, causal_source_ids, source_count) != 0U))
            continue;
        g_note_fx_commands[write_index] = command;
        write_index = (uint8_t)((write_index + 1U) % NOTE_FX_COMMAND_CAPACITY);
    }
    g_note_fx_command_head = write_index;
    uint16_t future_write = 0U;
    for (uint16_t i = 0U; i < g_note_fx_future_count; ++i)
    {
        if ((g_note_fx_future[i].event.track == track)
                && (note_fx_pipeline_source_id_selected(
                    g_note_fx_future[i].event.source_token,
                    causal_source_ids, source_count) != 0U))
            continue;
        g_note_fx_future[future_write++] = g_note_fx_future[i];
    }
    g_note_fx_future_count = future_write;
    note_fx_pipeline_exit_critical(primask);
    for (uint16_t i = 0U; i < source_count; ++i)
    {
        note_fx_engine_forget_causal_source(track, causal_source_ids[i]);
        const int8_t held = note_fx_pipeline_held_source_find(
            track, causal_source_ids[i]);
        if (held >= 0)
        {
            --g_note_fx_held_source_count[track];
            g_note_fx_held_source[track][(uint8_t)held] =
                g_note_fx_held_source[track]
                    [g_note_fx_held_source_count[track]];
        }
    }
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
        (((terminal.source_token
            & (uint32_t)~NOTE_EVENT_OCCURRENCE_COUNTER_MASK)
            == NOTE_EVENT_OCCURRENCE_NAMESPACE_KEY)
        || ((terminal.source_token
            & (uint32_t)~NOTE_EVENT_OCCURRENCE_COUNTER_MASK)
            == NOTE_EVENT_OCCURRENCE_NAMESPACE_MIDI))
        ? CONTROL_MUSIC_ACTION_EXTERNAL_FLAG : 0U);
    const control_music_action_t audio_event = {
        .due_sample = terminal.sample_abs,
        .output_id = terminal.occurrence_id,
        .kind = (uint8_t)(((terminal.kind == NOTE_EVENT_KIND_ON)
            ? (((terminal.flags & NOTE_EVENT_FLAG_RETRIGGER) != 0U)
                ? CONTROL_MUSIC_ACTION_RETRIGGER : CONTROL_MUSIC_ACTION_START)
            : CONTROL_MUSIC_ACTION_STOP)
            | external_flag
            | (uint8_t)(channel << CONTROL_MUSIC_ACTION_CHANNEL_SHIFT)),
        .entity_id = terminal.track,
        .note = terminal.note,
        .velocity = terminal.velocity
    };
    const uint8_t submitted = ((terminal.kind == NOTE_EVENT_KIND_ON)
            && ((terminal.flags & NOTE_EVENT_FLAG_LEGATO) != 0U))
        ? control_music_output_legato(
            &audio_event, terminal.source_token, terminal.generation)
        : control_music_output_submit(
            &audio_event, terminal.source_token, terminal.generation);
    uint8_t trace_track = 0U;
    uint8_t trace_step = 0U;
    if (seq_note_trace_output_is_watched(terminal.occurrence_id,
                                         &trace_track, &trace_step) != 0U)
        seq_note_trace_record(
            (submitted == 0U)
                ? SEQ_NOTE_TRACE_REJECT_TERMINAL
                : ((terminal.kind == NOTE_EVENT_KIND_ON)
                    ? SEQ_NOTE_TRACE_TERMINAL_ON
                    : SEQ_NOTE_TRACE_TERMINAL_OFF),
            trace_track, trace_step, terminal.sample_abs,
            terminal.generation, terminal.occurrence_id,
            terminal.source_token);
    if (submitted == 0U)
    {
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    }
    if (terminal.kind == NOTE_EVENT_KIND_ON)
    {
        return NOTE_EVENT_RESULT_ACCEPTED;
    }
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static note_event_result_t note_fx_pipeline_run_batch(const note_event_t *events,
                                                       uint8_t event_count)
{
    if ((events == NULL) || (event_count == 0U)
            || (event_count > NOTE_FX_BATCH_CAPACITY))
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    const uint8_t start_stage = events[0].stage;
    for (uint8_t i = 0U; i < event_count; ++i)
    {
        if (!note_event_is_valid(&events[i])
                || (events[i].track >= NOTE_FX_TRACK_COUNT)
                || (events[i].track != events[0].track)
                || (events[i].group_id != events[0].group_id)
                || (events[i].kind != events[0].kind)
                || (events[i].stage != start_stage))
            return NOTE_EVENT_RESULT_DROPPED_POLICY;
        g_note_fx_buffer_a[i] = events[i];
    }
    uint8_t count = event_count;
    note_event_t *input = g_note_fx_buffer_a;
    note_event_t *output = g_note_fx_buffer_b;
    for (uint8_t slot = start_stage; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        uint8_t output_count = 0U;
        const note_event_result_t result = note_fx_engine_transform(
            slot, input, count, output, NOTE_FX_BATCH_CAPACITY, &output_count);
        if (result != NOTE_EVENT_RESULT_ACCEPTED) return result;
        if (output_count != 0U)
        {
            uint8_t write = 0U;
            for (uint8_t i = 0U; i < output_count; ++i)
            {
                if ((g_note_fx_window_active != 0U)
                        && (output[i].sample_abs < g_note_fx_window_start))
                    output[i].sample_abs = g_note_fx_window_start;
                if (((output[i].flags & NOTE_EVENT_FLAG_FUTURE) != 0U)
                        || ((g_note_fx_window_active != 0U)
                            && (output[i].sample_abs >= g_note_fx_window_end)))
                {
                    output[i].flags &= (uint8_t)~NOTE_EVENT_FLAG_FUTURE;
                    const note_event_result_t scheduled =
                        note_fx_pipeline_schedule_future(&output[i]);
                    if (scheduled != NOTE_EVENT_RESULT_ACCEPTED)
                        return scheduled;
                }
                else
                    output[write++] = output[i];
            }
            output_count = write;
        }
        count = output_count;
        note_event_t *const swap = input;
        input = output;
        output = swap;
        if (count == 0U) return NOTE_EVENT_RESULT_ACCEPTED;
    }
    for (uint8_t i = 0U; i < count; ++i)
    {
        const note_event_result_t result = note_fx_pipeline_terminal(&input[i], NULL);
        if (result != NOTE_EVENT_RESULT_ACCEPTED) return result;
    }
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static note_event_result_t note_fx_pipeline_run(const note_event_t *event)
{
    return note_fx_pipeline_run_batch(event, 1U);
}

static uint8_t note_fx_future_precedes(const note_fx_future_t *left,
                                       const note_fx_future_t *right)
{
    if (left->event.sample_abs != right->event.sample_abs)
        return left->event.sample_abs < right->event.sample_abs;
    if (left->event.kind != right->event.kind)
        return left->event.kind < right->event.kind;
    if (left->event.track != right->event.track)
        return left->event.track < right->event.track;
    if (left->resume_slot != right->resume_slot)
        return left->resume_slot < right->resume_slot;
    if (left->event.group_id != right->event.group_id)
        return left->event.group_id < right->event.group_id;
    return left->event.occurrence_id < right->event.occurrence_id;
}

static note_event_result_t note_fx_pipeline_schedule_future(
    const note_event_t *event)
{
    if (g_note_fx_future_count >= NOTE_FX_FUTURE_CAPACITY)
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    note_fx_future_t item = { .event = *event,
        .resume_slot = event->stage };
    uint16_t position = g_note_fx_future_count;
    while ((position != 0U)
            && note_fx_future_precedes(
                &item, &g_note_fx_future[position - 1U]))
    {
        g_note_fx_future[position] = g_note_fx_future[position - 1U];
        --position;
    }
    g_note_fx_future[position] = item;
    ++g_note_fx_future_count;
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static note_event_result_t note_fx_pipeline_stage_emit(const note_event_t *event,
                                                     void *context)
{
    (void)context;
    if (!note_event_is_valid(event) || (event->track >= NOTE_FX_TRACK_COUNT))
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    if ((g_note_fx_window_active != 0U)
            && (event->sample_abs >= g_note_fx_window_end))
        return note_fx_pipeline_schedule_future(event);
    return note_fx_pipeline_run(event);
}

static void note_fx_pipeline_purge_future_track(uint8_t track)
{
    uint16_t write = 0U;
    for (uint16_t read = 0U; read < g_note_fx_future_count; ++read)
        if (g_note_fx_future[read].event.track != track)
            g_note_fx_future[write++] = g_note_fx_future[read];
    g_note_fx_future_count = write;
}

static note_event_result_t note_fx_pipeline_apply_due_future(uint64_t end)
{
    while ((g_note_fx_future_count != 0U)
            && (g_note_fx_future[0].event.sample_abs < end))
    {
        const note_fx_future_t first = g_note_fx_future[0];
        note_event_t group[NOTE_FX_BATCH_CAPACITY];
        uint8_t group_count = 0U;
        uint16_t consumed = 0U;
        while ((consumed < g_note_fx_future_count)
                && (group_count < NOTE_FX_BATCH_CAPACITY))
        {
            const note_fx_future_t *const item = &g_note_fx_future[consumed];
            if ((item->event.sample_abs != first.event.sample_abs)
                    || (item->event.kind != first.event.kind)
                    || (item->event.track != first.event.track)
                    || (item->event.group_id != first.event.group_id)
                    || (item->resume_slot != first.resume_slot))
                break;
            group[group_count] = item->event;
            group[group_count].stage = item->resume_slot;
            ++group_count;
            ++consumed;
        }
        memmove(g_note_fx_future,
                &g_note_fx_future[consumed],
                (g_note_fx_future_count - consumed)
                    * sizeof(g_note_fx_future[0]));
        g_note_fx_future_count = (uint16_t)(g_note_fx_future_count - consumed);
        if (first.event.chain_generation
                != g_note_fx_chain_generation[first.event.track])
            continue;
        const note_event_result_t result = note_fx_pipeline_run_batch(
            group, group_count);
        if (result != NOTE_EVENT_RESULT_ACCEPTED) return result;
    }
    return NOTE_EVENT_RESULT_ACCEPTED;
}

typedef struct
{
    uint16_t future;
    uint8_t source_note_limit;
    uint8_t fanout;
} note_fx_admission_t;

static uint8_t note_fx_pipeline_admit_effective(
    uint8_t track,
    const uint8_t effective[NOTE_FX_SLOT_COUNT][NOTE_FX_PARAM_COUNT],
    note_fx_admission_t *out)
{
    uint8_t echo_count = 0U;
    uint8_t harmonizer_count = 0U;
    uint8_t instant_fanout = 1U;
    uint8_t temporal_fanout = 1U;
    uint8_t stage_fanout = 1U;
    uint8_t maximum_stage_fanout = 1U;
    uint8_t group_source_limit = NOTE_FX_HELD_PITCH_CAPACITY;
    uint16_t future_per_track = 0U;
    uint8_t has_euclid = 0U;
    if ((track >= NOTE_FX_TRACK_COUNT) || (effective == NULL) || (out == NULL))
        return 0U;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        note_fx_capacity_desc_t capacity;
        const uint8_t model = effective[slot][NOTE_FX_PARAM_COUNT - 1U];
        if (note_fx_engine_capacity(model, &capacity) == 0U) return 0U;
        if (model == NOTE_FX_MODEL_ECHO) ++echo_count;
        if (model == NOTE_FX_MODEL_HARMONIZER) ++harmonizer_count;
        if (model == NOTE_FX_MODEL_EUCLID) has_euclid = 1U;
        const uint8_t model_temporal = (model == NOTE_FX_MODEL_ECHO)
            ? (uint8_t)(effective[slot][1] + 1U)
            : capacity.temporal_fanout;
        if (((uint16_t)instant_fanout * capacity.instant_fanout > UINT8_MAX)
                || ((uint16_t)temporal_fanout * model_temporal > UINT8_MAX)
                || ((uint16_t)stage_fanout * capacity.instant_fanout
                    * model_temporal > NOTE_FX_BATCH_CAPACITY))
            return 0U;
        instant_fanout = (uint8_t)(instant_fanout * capacity.instant_fanout);
        temporal_fanout = (uint8_t)(temporal_fanout * model_temporal);
        stage_fanout = (uint8_t)(stage_fanout
            * capacity.instant_fanout * model_temporal);
        if (stage_fanout > maximum_stage_fanout)
            maximum_stage_fanout = stage_fanout;
        if (capacity.max_notes_per_group == 0U) return 0U;
        const uint8_t model_group_limit = (uint8_t)(
            capacity.max_notes_per_group / stage_fanout);
        if (group_source_limit > model_group_limit)
            group_source_limit = model_group_limit;
        future_per_track = (uint16_t)(future_per_track
            + ((uint16_t)capacity.max_future_pending
                * (stage_fanout / model_temporal)));
    }
    if ((echo_count > 1U) || (harmonizer_count > 1U)) return 0U;
    if ((echo_count != 0U) && (has_euclid != 0U)
            && (future_per_track < 256U))
        future_per_track = 256U;
    if (future_per_track > NOTE_FX_FUTURE_CAPACITY) return 0U;
    const uint16_t composed_fanout = (uint16_t)instant_fanout
        * temporal_fanout;
    /* 31 live commands x four actions fit the fixed 128-action staging area.
     * Larger composed chains are rejected before their state becomes active. */
    if ((composed_fanout == 0U) || (composed_fanout > 4U)) return 0U;
    entity_topology_descriptor_t topology;
    if ((entity_topology_get((brick_entity_id_t)track, &topology) == 0U)
            || ((topology.role == ENTITY_ROLE_GROUP_CHILD)
                && (composed_fanout != 1U)))
        return 0U;
    uint8_t source_note_limit = (uint8_t)(
        NOTE_FX_HELD_PITCH_CAPACITY / composed_fanout);
    const uint8_t stage_note_limit = (uint8_t)(
        NOTE_FX_BATCH_CAPACITY / maximum_stage_fanout);
    if (source_note_limit > stage_note_limit)
        source_note_limit = stage_note_limit;
    if (source_note_limit > group_source_limit)
        source_note_limit = group_source_limit;
    if (source_note_limit == 0U) return 0U;
    const uint8_t play_capacity = seq_model_play_capacity(track);
    const uint16_t action_reservation = (uint16_t)(4U
        * ((play_capacity < source_note_limit)
            ? play_capacity : source_note_limit)
        * composed_fanout);
    uint16_t global_actions = action_reservation;
    uint16_t global_future = future_per_track;
    for (uint8_t owner = 0U; owner < NOTE_FX_TRACK_COUNT; ++owner)
    {
        if (owner == track) continue;
        const uint8_t owner_capacity = seq_model_play_capacity(owner);
        const uint8_t owner_limit = g_note_fx_source_note_limit[owner];
        const uint8_t owner_sources = (owner_capacity < owner_limit)
            ? owner_capacity : owner_limit;
        global_actions = (uint16_t)(global_actions + 4U * owner_sources
            * g_note_fx_admitted_fanout[owner]);
        global_future = (uint16_t)(global_future
            + g_note_fx_admitted_future[owner]);
    }
    if ((global_actions > CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST)
            || (global_future > NOTE_FX_FUTURE_CAPACITY))
        return 0U;
    out->future = future_per_track;
    out->source_note_limit = source_note_limit;
    out->fanout = (uint8_t)composed_fanout;
    return 1U;
}

static uint8_t note_fx_pipeline_resolve_effective(
    uint8_t track, const note_fx_track_state_t *state,
    uint8_t out[NOTE_FX_SLOT_COUNT][NOTE_FX_PARAM_COUNT])
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (state == NULL) || (out == NULL))
        return 0U;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        for (uint8_t param = 0U; param < NOTE_FX_PARAM_COUNT; ++param)
            out[slot][param] = g_note_fx_override_valid[track][slot][param]
                ? g_note_fx_override_value[track][slot][param]
                : state->value[slot][param];
        if (out[slot][3] >= NOTE_FX_MODEL_COUNT)
            out[slot][3] = NOTE_FX_MODEL_OFF;
        for (uint8_t param = 0U; param < 3U; ++param)
        {
            note_fx_param_schema_t schema;
            (void)note_fx_state_get_param_schema(out[slot][3], param, &schema);
            if ((out[slot][param] < schema.min)
                    || (out[slot][param] > schema.max))
                out[slot][param] = schema.default_value;
        }
        if ((out[slot][3] == NOTE_FX_MODEL_EUCLID)
                && (out[slot][1] > out[slot][0]))
            out[slot][1] = out[slot][0];
    }
    return 1U;
}

uint8_t note_fx_pipeline_reserve_state(
    uint8_t track, const note_fx_track_state_t *state)
{
    uint8_t effective[NOTE_FX_SLOT_COUNT][NOTE_FX_PARAM_COUNT];
    note_fx_admission_t admission;
    if ((note_fx_pipeline_resolve_effective(track, state, effective) == 0U)
            || (note_fx_pipeline_admit_effective(
                track, effective, &admission) == 0U))
        return 0U;
    g_note_fx_admitted_future[track] = admission.future;
    g_note_fx_source_note_limit[track] = admission.source_note_limit;
    g_note_fx_admitted_fanout[track] = admission.fanout;
    return 1U;
}

static uint8_t note_fx_pipeline_configure_track_owner(
    uint8_t track, const note_fx_track_state_t *state)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (state == NULL))
    {
        return 0U;
    }
    uint64_t now_sample = 0U;
    if (control_rt_now_sample(&now_sample) == 0U)
    {
        Error_Handler();
        return 0U;
    }
    const uint64_t sample = control_music_output_first_unpublished_sample(
        now_sample);
    uint8_t next[NOTE_FX_SLOT_COUNT][NOTE_FX_PARAM_COUNT];
    uint8_t model_change = 0U;
    uint8_t first_model_change = NOTE_FX_SLOT_COUNT;
    uint8_t revoice_slot = NOTE_FX_SLOT_COUNT;
    note_event_t cutover_held[NOTE_FX_HELD_PITCH_CAPACITY];
    uint8_t cutover_held_count = 0U;
    if (note_fx_pipeline_resolve_effective(track, state, next) == 0U)
        return 0U;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        if ((g_note_fx_applied_valid[track] != 0U)
                && (next[slot][NOTE_FX_PARAM_COUNT - 1U]
                    != g_note_fx_applied[track][slot]
                        [NOTE_FX_PARAM_COUNT - 1U]))
        {
            model_change = 1U;
            if (slot < first_model_change) first_model_change = slot;
        }
        if ((g_note_fx_applied_valid[track] != 0U)
                && ((next[slot][NOTE_FX_PARAM_COUNT - 1U]
                        == NOTE_FX_MODEL_CHORD)
                    || (next[slot][NOTE_FX_PARAM_COUNT - 1U]
                        == NOTE_FX_MODEL_HARMONIZER)))
            for (uint8_t param = 0U; param < NOTE_FX_PARAM_COUNT - 1U;
                 ++param)
                if ((next[slot][param]
                        != g_note_fx_applied[track][slot][param])
                        && (slot < revoice_slot))
                    revoice_slot = slot;
    }
    note_fx_admission_t admission;
    if (note_fx_pipeline_admit_effective(track, next, &admission) == 0U)
        return 0U;
    if (model_change != 0U)
    {
        if (note_fx_engine_collect_held(track, first_model_change, sample,
                cutover_held, NOTE_FX_HELD_PITCH_CAPACITY,
                &cutover_held_count) != NOTE_EVENT_RESULT_ACCEPTED)
            return 0U;
        if (control_music_output_close_entity(track, sample) == 0U)
            return 0U;
        note_fx_pipeline_purge_future_track(track);
        if (note_fx_engine_reset_from_slot(track, first_model_change)
                != NOTE_EVENT_RESULT_ACCEPTED)
            return 0U;
        ++g_note_fx_chain_generation[track];
        if (g_note_fx_chain_generation[track] == 0U)
            g_note_fx_chain_generation[track] = 1U;
    }
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        if (note_fx_engine_configure(
                track, slot, next[slot][3], next[slot][0], next[slot][1],
                next[slot][2])
                != NOTE_EVENT_RESULT_ACCEPTED)
            return 0U;
    }
    memcpy(g_note_fx_applied[track], next, sizeof(next));
    g_note_fx_applied_valid[track] = 1U;
    g_note_fx_admitted_future[track] = admission.future;
    g_note_fx_source_note_limit[track] = admission.source_note_limit;
    g_note_fx_admitted_fanout[track] = admission.fanout;
    if (model_change != 0U)
    {
        note_event_t held[NOTE_FX_HELD_PITCH_CAPACITY];
        const uint8_t held_count = cutover_held_count;
        memcpy(held, cutover_held,
               held_count * sizeof(held[0]));
        if (note_fx_pipeline_replay_grouped(
                held, held_count, first_model_change, sample,
                g_note_fx_chain_generation[track])
                != NOTE_EVENT_RESULT_ACCEPTED)
            return 0U;
    }
    else if (revoice_slot < NOTE_FX_SLOT_COUNT)
    {
        note_event_t held[NOTE_FX_HELD_PITCH_CAPACITY];
        uint8_t held_count = 0U;
        if (note_fx_engine_collect_held(track, revoice_slot, sample,
                held, NOTE_FX_HELD_PITCH_CAPACITY, &held_count)
                != NOTE_EVENT_RESULT_ACCEPTED)
            return 0U;
        uint32_t source_ids[NOTE_FX_HELD_PITCH_CAPACITY];
        uint16_t source_count = 0U;
        for (uint8_t i = 0U; i < held_count; ++i)
        {
            uint8_t duplicate = 0U;
            for (uint16_t j = 0U; j < source_count; ++j)
                if (source_ids[j] == held[i].source_token)
                    duplicate = 1U;
            if (duplicate == 0U)
                source_ids[source_count++] = held[i].source_token;
        }
        if ((source_count != 0U)
                && (control_music_output_close_causal_sources(
                    source_ids, source_count, sample) == 0U))
            return 0U;
        if (note_fx_pipeline_replay_grouped(
                held, held_count, revoice_slot, sample,
                g_note_fx_chain_generation[track])
                != NOTE_EVENT_RESULT_ACCEPTED)
            return 0U;
    }
    return 1U;
}

void note_fx_pipeline_init(void)
{
    memset(g_note_fx_override_valid, 0, sizeof(g_note_fx_override_valid));
    memset(g_note_fx_commands, 0, sizeof(g_note_fx_commands));
    memset(g_note_fx_live_queue, 0, sizeof(g_note_fx_live_queue));
    memset(g_note_fx_future, 0, sizeof(g_note_fx_future));
    memset(g_note_fx_applied_valid, 0, sizeof(g_note_fx_applied_valid));
    memset(g_note_fx_admitted_future, 0, sizeof(g_note_fx_admitted_future));
    memset(g_note_fx_admitted_fanout, 0, sizeof(g_note_fx_admitted_fanout));
    memset(g_note_fx_source_note_limit, NOTE_FX_HELD_PITCH_CAPACITY,
           sizeof(g_note_fx_source_note_limit));
    memset(g_note_fx_held_source, 0, sizeof(g_note_fx_held_source));
    memset(g_note_fx_held_source_count, 0,
           sizeof(g_note_fx_held_source_count));
    g_note_fx_command_head = 0U;
    g_note_fx_command_tail = 0U;
    g_note_fx_live_queue_count = 0U;
    g_note_fx_future_count = 0U;
    g_note_fx_window_active = 0U;
    g_note_fx_live_fallback_serial = 0U;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        g_note_fx_source_generation[track] = 1U;
        g_note_fx_chain_generation[track] = 1U;
    }
    note_fx_engine_init();
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
    const uint8_t old_valid = g_note_fx_override_valid[track][slot][param];
    const uint8_t old_value = g_note_fx_override_value[track][slot][param];
    g_note_fx_override_valid[track][slot][param] = 1U;
    g_note_fx_override_value[track][slot][param] = value;
    if (note_fx_pipeline_configure_track_owner(track, &state) != 0U)
        return 1U;
    g_note_fx_override_valid[track][slot][param] = old_valid;
    g_note_fx_override_value[track][slot][param] = old_value;
    (void)note_fx_pipeline_configure_track_owner(track, &state);
    return 0U;
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
    const uint8_t old_value = g_note_fx_override_value[track][slot][param];
    g_note_fx_override_valid[track][slot][param] = 0U;
    if (note_fx_pipeline_configure_track_owner(track, &state) != 0U)
        return 1U;
    g_note_fx_override_valid[track][slot][param] = 1U;
    g_note_fx_override_value[track][slot][param] = old_value;
    (void)note_fx_pipeline_configure_track_owner(track, &state);
    return 0U;
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
    if (source.group_id == 0U) source.group_id = source.occurrence_id;
    source.chain_generation = g_note_fx_chain_generation[source.track];
    source.stage = NOTE_EVENT_STAGE_SOURCE;
    const note_event_result_t result = note_fx_pipeline_run(&source);
    if ((result == NOTE_EVENT_RESULT_ACCEPTED)
            && (note_fx_pipeline_held_source_update(&source) == 0U))
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    return result;
}

note_event_result_t note_fx_pipeline_submit_control_batch(
    const note_event_t *events, uint8_t event_count)
{
    if ((events == NULL) || (event_count == 0U)
            || (event_count > NOTE_FX_BATCH_CAPACITY))
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    note_event_t source[NOTE_FX_BATCH_CAPACITY];
    for (uint8_t i = 0U; i < event_count; ++i)
    {
        if (!note_event_is_valid(&events[i])
                || (events[i].track >= NOTE_FX_TRACK_COUNT)
                || (events[i].track != events[0].track)
                || (events[i].group_id != events[0].group_id)
                || (events[i].kind != events[0].kind))
            return NOTE_EVENT_RESULT_DROPPED_POLICY;
        source[i] = events[i];
        if (source[i].destination_id == NOTE_EVENT_DESTINATION_DEFAULT)
            source[i].destination_id =
                track_runtime_get_midi_channel_zero_based(source[i].track);
        if (source[i].group_id == 0U)
            source[i].group_id = source[i].occurrence_id;
        source[i].chain_generation =
            g_note_fx_chain_generation[source[i].track];
        source[i].stage = NOTE_EVENT_STAGE_SOURCE;
    }
    const note_event_result_t result = note_fx_pipeline_run_batch(
        source, event_count);
    if (result != NOTE_EVENT_RESULT_ACCEPTED) return result;
    for (uint8_t i = 0U; i < event_count; ++i)
        if (note_fx_pipeline_held_source_update(&source[i]) == 0U)
            return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    return NOTE_EVENT_RESULT_ACCEPTED;
}

uint8_t note_fx_pipeline_source_note_limit(uint8_t track)
{
    return (track < NOTE_FX_TRACK_COUNT)
        ? g_note_fx_source_note_limit[track] : 0U;
}

uint8_t note_fx_pipeline_reset_track(uint8_t track)
{
    if (track >= NOTE_FX_TRACK_COUNT) return 0U;
    uint64_t now = 0U;
    if (control_rt_now_sample(&now) == 0U) return 0U;
    const uint64_t sample = control_music_output_first_unpublished_sample(now);
    if (control_music_output_close_entity(track, sample) == 0U) return 0U;
    const uint32_t primask = note_fx_pipeline_enter_critical();
    note_fx_pipeline_purge_future_track(track);
    uint8_t command_read = g_note_fx_command_tail;
    uint8_t command_write = g_note_fx_command_tail;
    while (command_read != g_note_fx_command_head)
    {
        const note_fx_command_t command = g_note_fx_commands[command_read];
        command_read = (uint8_t)((command_read + 1U)
            % NOTE_FX_COMMAND_CAPACITY);
        if ((command.track == track)
                && (command.kind != NOTE_FX_COMMAND_CONFIGURE_TRACK)) continue;
        g_note_fx_commands[command_write] = command;
        command_write = (uint8_t)((command_write + 1U)
            % NOTE_FX_COMMAND_CAPACITY);
    }
    g_note_fx_command_head = command_write;
    uint8_t live_write = 0U;
    for (uint8_t live = 0U; live < g_note_fx_live_queue_count; ++live)
        if (g_note_fx_live_queue[live].track != track)
            g_note_fx_live_queue[live_write++] = g_note_fx_live_queue[live];
    g_note_fx_live_queue_count = live_write;
    ++g_note_fx_source_generation[track];
    if (g_note_fx_source_generation[track] == 0U)
        g_note_fx_source_generation[track] = 1U;
    ++g_note_fx_chain_generation[track];
    if (g_note_fx_chain_generation[track] == 0U)
        g_note_fx_chain_generation[track] = 1U;
    note_fx_pipeline_exit_critical(primask);
    memset(g_note_fx_held_source[track], 0,
           sizeof(g_note_fx_held_source[track]));
    g_note_fx_held_source_count[track] = 0U;
    return note_fx_engine_cleanup(track) == NOTE_EVENT_RESULT_ACCEPTED;
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
        if (control_rt_now_sample(&sample_time) == 0U)
        {
            Error_Handler();
            return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
        }
    }
    sample_time = control_music_output_first_unpublished_sample(sample_time);

    const note_event_t event = {
        .sample_abs = sample_time,
        .duration_samples = (is_note_on != 0U)
            ? NOTE_EVENT_DURATION_OPEN : 0U,
        .source_token = source_occurrence_id,
        .occurrence_id = source_occurrence_id,
        .generation = g_note_fx_source_generation[track],
        .group_id = source_occurrence_id,
        .track = track,
        .destination_id = NOTE_EVENT_DESTINATION_DEFAULT,
        .note = note,
        .velocity = (is_note_on != 0U) ? velocity : 0U,
        .kind = (is_note_on != 0U) ? NOTE_EVENT_KIND_ON : NOTE_EVENT_KIND_OFF,
        .provenance = (uint8_t)provenance,
        .stage = NOTE_EVENT_STAGE_SOURCE,
        .flags = 0U
    };
    return note_fx_pipeline_submit_control(&event);
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
    if (!control_rt_capture_tick_to_sample(command->capture_tick, now,
                                           &sample_time))
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;

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
    uint64_t now = 0U;
    if (control_rt_now_sample(&now) == 0U)
    {
        Error_Handler();
        return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    }
    return note_fx_pipeline_submit_live_command(command, now);
}

static uint8_t note_fx_pipeline_apply_pending_commands(void)
{
    note_fx_command_t command;
    while (note_fx_pipeline_dequeue(&command) != 0U)
    {
        switch ((note_fx_command_kind_t)command.kind)
        {
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
    memset(g_note_fx_future, 0, sizeof(g_note_fx_future));
    g_note_fx_future_count = 0U;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        ++g_note_fx_source_generation[track];
        if (g_note_fx_source_generation[track] == 0U)
            g_note_fx_source_generation[track] = 1U;
        ++g_note_fx_chain_generation[track];
        if (g_note_fx_chain_generation[track] == 0U)
            g_note_fx_chain_generation[track] = 1U;
    }
    memset(g_note_fx_live_queue, 0, sizeof(g_note_fx_live_queue));
    g_note_fx_live_queue_count = 0U;
    memset(g_note_fx_held_source, 0, sizeof(g_note_fx_held_source));
    memset(g_note_fx_held_source_count, 0,
           sizeof(g_note_fx_held_source_count));
    note_fx_pipeline_exit_critical(primask);
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        (void)note_fx_engine_cleanup(track);
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
    if (frames == 0U) return 1U;
    note_fx_engine_set_samples_per_step_q16(samples_per_step_q16);
    g_note_fx_window_start = block_start;
    g_note_fx_window_end = block_start + frames;
    g_note_fx_window_active = 1U;
    note_event_result_t result = note_fx_pipeline_apply_due_future(
        g_note_fx_window_end);
    if (result == NOTE_EVENT_RESULT_ACCEPTED)
        result = note_fx_engine_process(block_start, frames,
            samples_per_step_q16, note_fx_pipeline_stage_emit, NULL);
    if (result == NOTE_EVENT_RESULT_ACCEPTED)
        result = note_fx_pipeline_apply_due_future(g_note_fx_window_end);
    g_note_fx_window_active = 0U;
    return (result == NOTE_EVENT_RESULT_ACCEPTED) ? 1U : 0U;
}

uint8_t note_fx_pipeline_apply_pending(void)
{
    return note_fx_pipeline_apply_pending_commands();
}

uint8_t note_fx_pipeline_prepare_external_window(uint64_t block_start,
                                                 uint16_t frames)
{
    if (frames == 0U) return note_fx_pipeline_apply_pending_commands();
    g_note_fx_window_start = block_start;
    g_note_fx_window_end = block_start + frames;
    g_note_fx_window_active = 1U;
    if ((note_fx_pipeline_apply_pending_commands() == 0U)
            || (note_fx_pipeline_apply_due_live_events(
                block_start + frames - 1U) == 0U))
    {
        g_note_fx_window_active = 0U;
        return 0U;
    }
    return 1U;
}
