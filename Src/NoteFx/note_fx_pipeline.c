#include "NoteFx/note_fx_pipeline.h"
#include <string.h>

#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"
#include "NoteFx/note_fx_engine.h"
#include "NoteFx/note_fx_state.h"
#include "Core/track_runtime.h"
#include "Seq/seq_runtime_exec.h"

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

static note_fx_source_record_t
    g_note_fx_source_ledger[NOTE_FX_TRACK_COUNT][NOTE_FX_ARP_MAX_SOURCES];
static uint32_t g_note_fx_source_generation[NOTE_FX_TRACK_COUNT];
static uint32_t g_note_fx_next_source_token;
static note_fx_pipeline_diag_t g_note_fx_pipeline_diag[NOTE_FX_TRACK_COUNT];

#define NOTE_FX_COMMAND_CAPACITY 32U

typedef enum
{
    NOTE_FX_COMMAND_SOURCE_EVENT = 0,
    NOTE_FX_COMMAND_SOURCE_RAW,
    NOTE_FX_COMMAND_APPLY_PARAM,
    NOTE_FX_COMMAND_RELEASE_PARAM,
    NOTE_FX_COMMAND_SYNC_TRACK,
    NOTE_FX_COMMAND_TRANSITION_TRACK,
    NOTE_FX_COMMAND_TRANSITION_ALL,
    NOTE_FX_COMMAND_RESET_TRACK,
    NOTE_FX_COMMAND_RESET_ALL
} note_fx_command_kind_t;

typedef struct
{
    uint8_t kind;
    uint8_t track;
    uint8_t slot;
    uint8_t param;
    uint8_t value;
    uint8_t is_note_on;
    uint8_t provenance;
    uint8_t reserved;
    uint64_t sample_time;
    note_fx_transition_policy_t policy;
    note_fx_event_t event;
    uint8_t note;
    uint8_t velocity;
} note_fx_command_t;

static note_fx_command_t g_note_fx_commands[NOTE_FX_COMMAND_CAPACITY];
static volatile uint8_t g_note_fx_command_head;
static volatile uint8_t g_note_fx_command_tail;
static uint16_t g_note_fx_command_high_water;
static uint32_t g_note_fx_command_drop_count;

typedef struct
{
    uint8_t active;
    uint16_t frames;
    uint16_t emissions_used;
    uint16_t off_remaining;
    uint16_t commands_used;
    uint8_t on_remaining[NOTE_FX_TRACK_COUNT];
} note_fx_half_budget_t;

static note_fx_half_budget_t g_note_fx_half_budget;
static uint16_t g_note_fx_last_half_emissions;
static uint16_t g_note_fx_max_half_emissions;

static note_fx_result_t note_fx_pipeline_stage_emit(const note_event_t *event,
                                                     void *context);

static note_fx_result_t note_fx_pipeline_budget_admit(const note_event_t *event)
{
    if ((g_note_fx_half_budget.active == 0U)
            || ((event->flags & NOTE_EVENT_FLAG_GENERATED) == 0U))
    {
        return NOTE_EVENT_RESULT_ACCEPTED;
    }

    if (event->kind == NOTE_EVENT_KIND_OFF)
    {
        if (g_note_fx_half_budget.off_remaining == 0U)
            return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
        --g_note_fx_half_budget.off_remaining;
    }
    else
    {
        if ((event->track >= NOTE_FX_TRACK_COUNT)
                || (g_note_fx_half_budget.on_remaining[event->track] == 0U))
            return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
        --g_note_fx_half_budget.on_remaining[event->track];
    }
    ++g_note_fx_half_budget.emissions_used;
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static uint8_t note_fx_pipeline_enqueue(const note_fx_command_t *command)
{
    if (command == NULL)
    {
        return 0U;
    }

    if ((command->kind == NOTE_FX_COMMAND_TRANSITION_TRACK)
            || (command->kind == NOTE_FX_COMMAND_TRANSITION_ALL)
            || (command->kind == NOTE_FX_COMMAND_RESET_TRACK)
            || (command->kind == NOTE_FX_COMMAND_RESET_ALL))
    {
        uint8_t cursor = g_note_fx_command_tail;
        while (cursor != g_note_fx_command_head)
        {
            const note_fx_command_t *const pending = &g_note_fx_commands[cursor];
            if ((pending->kind == command->kind)
                    && (pending->track == command->track)
                    && (pending->policy == command->policy))
            {
                return 1U;
            }
            cursor = (uint8_t)((cursor + 1U) % NOTE_FX_COMMAND_CAPACITY);
        }
    }

    const uint8_t head = g_note_fx_command_head;
    const uint8_t next = (uint8_t)((head + 1U) % NOTE_FX_COMMAND_CAPACITY);
    if (next == g_note_fx_command_tail)
    {
        ++g_note_fx_command_drop_count;
        return 0U;
    }

    g_note_fx_commands[head] = *command;
    g_note_fx_command_head = next;
    const uint8_t depth = (uint8_t)((next + NOTE_FX_COMMAND_CAPACITY
                                     - g_note_fx_command_tail)
                                    % NOTE_FX_COMMAND_CAPACITY);
    if (depth > g_note_fx_command_high_water)
    {
        g_note_fx_command_high_water = depth;
    }
    return 1U;
}

static uint8_t note_fx_pipeline_dequeue(note_fx_command_t *command)
{
    const uint8_t tail = g_note_fx_command_tail;
    if ((command == NULL) || (tail == g_note_fx_command_head))
    {
        return 0U;
    }

    *command = g_note_fx_commands[tail];
    g_note_fx_command_tail = (uint8_t)((tail + 1U) % NOTE_FX_COMMAND_CAPACITY);
    return 1U;
}

static void note_fx_pipeline_record_result(uint8_t track, note_fx_result_t result)
{
    if (track >= NOTE_FX_TRACK_COUNT)
        return;
    if (result == NOTE_EVENT_RESULT_ACCEPTED)
        ++g_note_fx_pipeline_diag[track].accepted;
    else if (result == NOTE_EVENT_RESULT_REJECTED_STALE)
        ++g_note_fx_pipeline_diag[track].stale;
    else
        ++g_note_fx_pipeline_diag[track].rejected;
}

static int8_t note_fx_pipeline_find_source(uint8_t track, uint8_t note,
                                            note_event_provenance_t provenance)
{
    for (uint8_t i = 0U; i < NOTE_FX_ARP_MAX_SOURCES; ++i)
    {
        const note_fx_source_record_t *const record =
            &g_note_fx_source_ledger[track][i];
        if ((record->active != 0U) && (record->note == note)
                && (record->provenance == (uint8_t)provenance))
            return (int8_t)i;
    }
    return -1;
}

static note_fx_result_t note_fx_pipeline_terminal(const note_fx_event_t *event, void *context)
{
    (void)context;
    if (!note_event_is_valid(event)
            || (event->track >= NOTE_FX_TRACK_COUNT)
            || (event->stage > NOTE_EVENT_STAGE_TERMINAL))
    {
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    }
    note_fx_event_t terminal = *event;
    terminal.stage = NOTE_EVENT_STAGE_TERMINAL;
    terminal.flags |= NOTE_EVENT_FLAG_TERMINAL;
    return seq_play_scheduler_dispatch_terminal_event(&terminal);
}

static note_fx_result_t note_fx_pipeline_stage_emit(const note_event_t *event,
                                                     void *context)
{
    (void)context;
    if (!note_event_is_valid(event) || (event->track >= NOTE_FX_TRACK_COUNT))
    {
        if ((event != NULL) && (event->track < NOTE_FX_TRACK_COUNT))
            ++g_note_fx_pipeline_diag[event->track].continuation_drop_count;
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    }
    const note_fx_result_t budget_result = note_fx_pipeline_budget_admit(event);
    if (budget_result != NOTE_EVENT_RESULT_ACCEPTED)
    {
        if (event->kind == NOTE_EVENT_KIND_OFF)
            ++g_note_fx_pipeline_diag[event->track].budget_off_drop_count;
        else
            ++g_note_fx_pipeline_diag[event->track].budget_on_drop_count;
        ++g_note_fx_pipeline_diag[event->track].continuation_drop_count;
        return budget_result;
    }
    ++g_note_fx_pipeline_diag[event->track].stage_emissions[event->stage];
    if (event->stage > g_note_fx_pipeline_diag[event->track].max_stage_reached)
        g_note_fx_pipeline_diag[event->track].max_stage_reached = event->stage;
    if (event->stage >= NOTE_EVENT_STAGE_TERMINAL)
    {
        return note_fx_pipeline_terminal(event, NULL);
    }
    return note_fx_engine_stage_source(event, event->stage,
                                       note_fx_pipeline_stage_emit, NULL);
}

static void note_fx_pipeline_sync_track_owner(uint8_t track)
{
    note_fx_track_state_t state;
    if (note_fx_state_capture_track(track, &state) == 0U)
    {
        return;
    }
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        uint8_t value[NOTE_FX_PARAM_COUNT];
        for (uint8_t param = 0U; param < NOTE_FX_PARAM_COUNT; ++param)
            value[param] = g_note_fx_override_valid[track][slot][param]
                ? g_note_fx_override_value[track][slot][param]
                : state.value[slot][param];
        note_fx_engine_configure(track, slot, value[3], value[0], value[1], value[2]);
    }
}

void note_fx_pipeline_init(void)
{
    memset(g_note_fx_override_valid, 0, sizeof(g_note_fx_override_valid));
    memset(g_note_fx_source_ledger, 0, sizeof(g_note_fx_source_ledger));
    memset(g_note_fx_pipeline_diag, 0, sizeof(g_note_fx_pipeline_diag));
    memset(g_note_fx_commands, 0, sizeof(g_note_fx_commands));
    g_note_fx_command_head = 0U;
    g_note_fx_command_tail = 0U;
    g_note_fx_command_high_water = 0U;
    g_note_fx_command_drop_count = 0U;
    memset(&g_note_fx_half_budget, 0, sizeof(g_note_fx_half_budget));
    g_note_fx_last_half_emissions = 0U;
    g_note_fx_max_half_emissions = 0U;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        g_note_fx_source_generation[track] = 1U;
    g_note_fx_next_source_token = 0U;
    note_fx_engine_init();
}

note_fx_pipeline_diag_t note_fx_pipeline_diag(uint8_t track)
{
    note_fx_pipeline_diag_t diag = {0};
    if (track < NOTE_FX_TRACK_COUNT)
    {
        diag = g_note_fx_pipeline_diag[track];
    }
    diag.command_high_water = g_note_fx_command_high_water;
    diag.command_drop_count = g_note_fx_command_drop_count;
    diag.half_emissions_last = g_note_fx_last_half_emissions;
    diag.half_emissions_high_water = g_note_fx_max_half_emissions;
    return diag;
}

uint8_t note_fx_pipeline_apply_runtime_param(uint8_t track, uint8_t slot,
                                             uint8_t param, uint8_t value)
{
    if (track >= NOTE_FX_TRACK_COUNT || slot >= NOTE_FX_SLOT_COUNT ||
        param >= NOTE_FX_PARAM_COUNT)
    {
        return 0U;
    }
    const note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_APPLY_PARAM,
        .track = track,
        .slot = slot,
        .param = param,
        .value = value
    };
    return note_fx_pipeline_enqueue(&command);
}

uint8_t note_fx_pipeline_release_runtime_param(uint8_t track, uint8_t slot,
                                               uint8_t param)
{
    if (track >= NOTE_FX_TRACK_COUNT || slot >= NOTE_FX_SLOT_COUNT ||
        param >= NOTE_FX_PARAM_COUNT)
    {
        return 0U;
    }
    const note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_RELEASE_PARAM,
        .track = track,
        .slot = slot,
        .param = param
    };
    return note_fx_pipeline_enqueue(&command);
}

note_fx_result_t note_fx_pipeline_submit_audio(const note_fx_event_t *event)
{
    if (!note_event_is_valid(event) || (event->track >= NOTE_FX_TRACK_COUNT))
    {
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    }

    note_fx_event_t source = *event;
    if (source.destination_id == NOTE_EVENT_DESTINATION_DEFAULT)
    {
        source.destination_id = track_runtime_get_midi_channel_zero_based(source.track);
    }
    if (source.occurrence_id == 0U)
    {
        source.occurrence_id = source.source_token;
    }
    note_fx_pipeline_sync_track_owner(source.track);
    const note_fx_result_t result =
        note_fx_engine_stage_source(&source, 0U, note_fx_pipeline_stage_emit, 0);
    note_fx_pipeline_record_result(source.track, result);
    return result;
}

note_fx_result_t note_fx_pipeline_submit(const note_fx_event_t *event)
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
    if (command.event.occurrence_id == 0U)
    {
        command.event.occurrence_id = command.event.source_token;
    }
    return (note_fx_pipeline_enqueue(&command) != 0U)
        ? NOTE_EVENT_RESULT_ACCEPTED
        : NOTE_EVENT_RESULT_REJECTED_CAPACITY;
}

static note_fx_result_t note_fx_pipeline_submit_source_audio(uint8_t track, uint8_t note,
                                                              uint8_t velocity, uint8_t is_note_on,
                                                              uint64_t sample_time,
                                                              note_event_provenance_t provenance)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (note >= 128U)
            || (provenance >= NOTE_EVENT_SOURCE_COUNT))
    {
        return NOTE_EVENT_RESULT_DROPPED_POLICY;
    }

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
            ++g_note_fx_pipeline_diag[track].rejected;
            return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
        }
        ++g_note_fx_next_source_token;
        if (g_note_fx_next_source_token == 0U)
            g_note_fx_next_source_token = 1U;
        token = g_note_fx_next_source_token;
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
        source_index = note_fx_pipeline_find_source(track, note, provenance);
        if (source_index < 0)
        {
            ++g_note_fx_pipeline_diag[track].stale;
            return NOTE_EVENT_RESULT_REJECTED_STALE;
        }
        token = g_note_fx_source_ledger[track][(uint8_t)source_index].token;
        generation = g_note_fx_source_ledger[track][(uint8_t)source_index].generation;
    }

    const note_fx_event_t event = {
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
    const note_fx_result_t result = note_fx_pipeline_submit_audio(&event);
    if (is_note_on == 0U)
    {
        if ((result == NOTE_EVENT_RESULT_ACCEPTED) && (source_index >= 0))
            g_note_fx_source_ledger[track][(uint8_t)source_index].active = 0U;
    }
    else if (result != NOTE_EVENT_RESULT_ACCEPTED)
    {
        g_note_fx_source_ledger[track][(uint8_t)source_index].active = 0U;
    }
    return result;
}

note_fx_result_t note_fx_pipeline_submit_source(uint8_t track, uint8_t note,
                                                 uint8_t velocity, uint8_t is_note_on,
                                                 uint64_t sample_time,
                                                 note_event_provenance_t provenance)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (note >= 128U)
            || (provenance >= NOTE_EVENT_SOURCE_COUNT))
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
        .sample_time = sample_time
    };
    return (note_fx_pipeline_enqueue(&command) != 0U)
        ? NOTE_EVENT_RESULT_ACCEPTED
        : NOTE_EVENT_RESULT_REJECTED_CAPACITY;
}

static void note_fx_pipeline_cleanup_track_owner(uint8_t track)
{
    if (track >= NOTE_FX_TRACK_COUNT)
    {
        return;
    }

    memset(g_note_fx_source_ledger[track], 0,
           sizeof(g_note_fx_source_ledger[track]));
    ++g_note_fx_source_generation[track];
    if (g_note_fx_source_generation[track] == 0U)
        g_note_fx_source_generation[track] = 1U;
    note_fx_engine_cleanup(track, seq_runtime_exec_get_audio_timeline_sample(),
                           note_fx_pipeline_stage_emit, 0);
}

static void note_fx_pipeline_transition_track_owner(uint8_t track,
                                                     note_fx_transition_policy_t policy)
{
    if (track >= NOTE_FX_TRACK_COUNT)
    {
        return;
    }

    if (policy == NOTE_FX_TRANSITION_MUTE_TRIGS)
    {
        /* Mute is a source admission policy; it never closes an owned occurrence. */
        return;
    }
    note_fx_pipeline_cleanup_track_owner(track);
}

static void note_fx_pipeline_apply_runtime_param_owner(uint8_t track, uint8_t slot,
                                                        uint8_t param, uint8_t value)
{
    note_fx_track_state_t state;
    if ((track >= NOTE_FX_TRACK_COUNT) || (slot >= NOTE_FX_SLOT_COUNT)
            || (param >= NOTE_FX_PARAM_COUNT)
            || (note_fx_state_capture_track(track, &state) == 0U))
    {
        return;
    }

    const uint8_t previous = g_note_fx_override_valid[track][slot][param]
        ? g_note_fx_override_value[track][slot][param] : state.value[slot][param];
    if ((param == 3U) && (previous != value))
    {
        note_fx_pipeline_cleanup_track_owner(track);
    }
    g_note_fx_override_valid[track][slot][param] = 1U;
    g_note_fx_override_value[track][slot][param] = value;
    note_fx_pipeline_sync_track_owner(track);
}

static void note_fx_pipeline_release_runtime_param_owner(uint8_t track, uint8_t slot,
                                                          uint8_t param)
{
    note_fx_track_state_t state;
    if ((track >= NOTE_FX_TRACK_COUNT) || (slot >= NOTE_FX_SLOT_COUNT)
            || (param >= NOTE_FX_PARAM_COUNT)
            || (note_fx_state_capture_track(track, &state) == 0U))
    {
        return;
    }

    if ((param == 3U) && (g_note_fx_override_valid[track][slot][param] != 0U)
            && (g_note_fx_override_value[track][slot][param] != state.value[slot][param]))
    {
        note_fx_pipeline_cleanup_track_owner(track);
    }
    g_note_fx_override_valid[track][slot][param] = 0U;
    note_fx_pipeline_sync_track_owner(track);
}

static void note_fx_pipeline_reset_runtime_overrides_owner(uint8_t track)
{
    if (track >= NOTE_FX_TRACK_COUNT)
    {
        return;
    }
    note_fx_pipeline_cleanup_track_owner(track);
    memset(g_note_fx_override_valid[track], 0, sizeof(g_note_fx_override_valid[track]));
    note_fx_pipeline_sync_track_owner(track);
}

static void note_fx_pipeline_apply_pending_commands(void)
{
    note_fx_command_t command;
    while (((g_note_fx_half_budget.active == 0U)
                || (g_note_fx_half_budget.commands_used < NOTE_FX_HALF_COMMAND_QUOTA))
            && (note_fx_pipeline_dequeue(&command) != 0U))
    {
        if (g_note_fx_half_budget.active != 0U)
            ++g_note_fx_half_budget.commands_used;
        switch ((note_fx_command_kind_t)command.kind)
        {
            case NOTE_FX_COMMAND_SOURCE_EVENT:
                (void)note_fx_pipeline_submit_audio(&command.event);
                break;
            case NOTE_FX_COMMAND_SOURCE_RAW:
                (void)note_fx_pipeline_submit_source_audio(command.track,
                                                            command.note,
                                                            command.velocity,
                                                            command.is_note_on,
                                                            command.sample_time,
                                                            (note_event_provenance_t)command.provenance);
                break;
            case NOTE_FX_COMMAND_APPLY_PARAM:
                note_fx_pipeline_apply_runtime_param_owner(command.track,
                                                            command.slot,
                                                            command.param,
                                                            command.value);
                break;
            case NOTE_FX_COMMAND_RELEASE_PARAM:
                note_fx_pipeline_release_runtime_param_owner(command.track,
                                                              command.slot,
                                                              command.param);
                break;
            case NOTE_FX_COMMAND_SYNC_TRACK:
                note_fx_pipeline_sync_track_owner(command.track);
                break;
            case NOTE_FX_COMMAND_TRANSITION_TRACK:
                note_fx_pipeline_transition_track_owner(command.track, command.policy);
                break;
            case NOTE_FX_COMMAND_TRANSITION_ALL:
                for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
                    note_fx_pipeline_transition_track_owner(track, command.policy);
                break;
            case NOTE_FX_COMMAND_RESET_TRACK:
                note_fx_pipeline_reset_runtime_overrides_owner(command.track);
                break;
            case NOTE_FX_COMMAND_RESET_ALL:
                for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
                    note_fx_pipeline_reset_runtime_overrides_owner(track);
                break;
            default:
                break;
        }
    }
}

void note_fx_pipeline_begin_audio_half(uint16_t frames)
{
    memset(&g_note_fx_half_budget, 0, sizeof(g_note_fx_half_budget));
    g_note_fx_half_budget.active = 1U;
    g_note_fx_half_budget.frames = frames;
    g_note_fx_half_budget.off_remaining = NOTE_FX_HALF_OFF_RESERVE;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        g_note_fx_half_budget.on_remaining[track] = NOTE_FX_HALF_ON_QUOTA_PER_TRACK;
}

void note_fx_pipeline_end_audio_half(void)
{
    g_note_fx_last_half_emissions = g_note_fx_half_budget.emissions_used;
    if (g_note_fx_last_half_emissions > g_note_fx_max_half_emissions)
        g_note_fx_max_half_emissions = g_note_fx_last_half_emissions;
    g_note_fx_half_budget.active = 0U;
}

uint8_t note_fx_pipeline_transition_track(uint8_t track,
                                          note_fx_transition_policy_t policy)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (policy >= NOTE_FX_TRANSITION_DESTINATION_REBIND + 1U))
    {
        return 0U;
    }
    const note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_TRANSITION_TRACK,
        .track = track,
        .policy = policy
    };
    return note_fx_pipeline_enqueue(&command);
}

uint8_t note_fx_pipeline_transition_all(note_fx_transition_policy_t policy)
{
    if (policy >= NOTE_FX_TRANSITION_DESTINATION_REBIND + 1U)
    {
        return 0U;
    }
    const note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_TRANSITION_ALL,
        .policy = policy
    };
    return note_fx_pipeline_enqueue(&command);
}

uint8_t note_fx_pipeline_sync_track(uint8_t track)
{
    if (track >= NOTE_FX_TRACK_COUNT)
    {
        return 0U;
    }
    const note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_SYNC_TRACK,
        .track = track
    };
    return note_fx_pipeline_enqueue(&command);
}

void note_fx_pipeline_reset_runtime_overrides(uint8_t track)
{
    if (track >= NOTE_FX_TRACK_COUNT)
    {
        return;
    }
    const note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_RESET_TRACK,
        .track = track
    };
    (void)note_fx_pipeline_enqueue(&command);
}

void note_fx_pipeline_reset_all_runtime_overrides(void)
{
    const note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_RESET_ALL
    };
    (void)note_fx_pipeline_enqueue(&command);
}

void note_fx_pipeline_process(uint64_t block_start, uint16_t frames,
                              uint32_t samples_per_step_q16)
{
    note_fx_pipeline_apply_pending_commands();
    note_fx_engine_process(block_start, frames, samples_per_step_q16,
                           note_fx_pipeline_stage_emit, 0);
}

uint16_t note_fx_pipeline_frames_until_deadline(uint64_t block_start,
                                                uint16_t max_frames)
{
    const uint64_t deadline = note_fx_engine_next_deadline();
    if ((deadline <= block_start) || (deadline >= block_start + max_frames))
    {
        return max_frames;
    }
    return (uint16_t)(deadline - block_start);
}
