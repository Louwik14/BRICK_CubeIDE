#include "NoteFx/note_fx_pipeline.h"
#include <string.h>
#include "stm32h7xx.h"

#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_runtime.h"
#include "Seq/seq_play_scheduler.h"
#include "NoteFx/note_fx_engine.h"
#include "NoteFx/note_fx_state.h"
#include "Core/live_clock.h"
#include "Core/track_runtime.h"
#include "Seq/seq_runtime_exec.h"
#include "Storage/memory_layout.h"

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
static note_fx_pipeline_diag_t g_note_fx_pipeline_diag[NOTE_FX_TRACK_COUNT];
/* Commands carry the epoch observed at publication.  A panic advances the
 * epoch, so commands published after the request survive the owner purge. */
static uint32_t g_note_fx_panic_epoch = 1U;
static volatile uint8_t g_note_fx_panic_pending;
static uint8_t g_note_fx_panic_active;

#define NOTE_FX_COMMAND_CAPACITY 32U
#define NOTE_FX_LIVE_QUEUE_CAPACITY (NOTE_FX_COMMAND_CAPACITY - 1U)
#define NOTE_FX_LIVE_STALE_THRESHOLD_SAMPLES 48000ULL
#define NOTE_FX_SOURCE_RESERVATION_CAPACITY \
    (NOTE_FX_TRACK_COUNT * NOTE_FX_ARP_MAX_SOURCES)

typedef enum
{
    NOTE_FX_COMMAND_SOURCE_EVENT = 0,
    NOTE_FX_COMMAND_SOURCE_RAW,
    NOTE_FX_COMMAND_APPLY_PARAM,
    NOTE_FX_COMMAND_RELEASE_PARAM,
    NOTE_FX_COMMAND_SYNC_TRACK,
    NOTE_FX_COMMAND_TRANSITION_TRACK,
    NOTE_FX_COMMAND_TRANSITION_ALL,
    NOTE_FX_COMMAND_RESET_TRACK
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
    uint32_t source_occurrence_id;
    uint32_t ingress_serial;
    uint32_t capture_tick;
    uint8_t capture_tick_valid;
    uint32_t panic_epoch;
    note_fx_transition_policy_t policy;
    note_fx_event_t event;
    uint8_t note;
    uint8_t velocity;
    uint8_t track_state_valid;
    note_fx_track_state_t track_state;
} note_fx_command_t;

typedef struct
{
    uint8_t active;
    uint8_t track;
    uint8_t provenance;
    uint8_t reserved;
    uint32_t occurrence_id;
} note_fx_source_reservation_t;

static note_fx_command_t g_note_fx_commands[NOTE_FX_COMMAND_CAPACITY];
static volatile uint8_t g_note_fx_command_head;
static volatile uint8_t g_note_fx_command_tail;
static uint16_t g_note_fx_command_high_water;
static uint32_t g_note_fx_command_drop_count;
static note_fx_source_reservation_t
    g_note_fx_source_reservation[NOTE_FX_SOURCE_RESERVATION_CAPACITY];
static uint8_t g_note_fx_source_reservation_count;
static note_fx_command_t
    g_note_fx_pending_closure[NOTE_FX_SOURCE_RESERVATION_CAPACITY];
static uint8_t g_note_fx_pending_closure_count;
static live_note_event_t g_note_fx_live_queue[NOTE_FX_LIVE_QUEUE_CAPACITY];
static uint8_t g_note_fx_live_queue_count;
static uint16_t g_note_fx_live_queue_high_water;
static uint32_t g_note_fx_live_late_count;
static uint32_t g_note_fx_live_stale_count;
static uint32_t g_note_fx_live_queue_drop_count;
static uint64_t g_note_fx_live_max_lateness_samples;
static uint32_t g_note_fx_live_fallback_serial;

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

static int16_t note_fx_pipeline_find_source_reservation_locked(
    uint8_t track, uint8_t provenance, uint32_t occurrence_id)
{
    for (uint8_t i = 0U; i < NOTE_FX_SOURCE_RESERVATION_CAPACITY; ++i)
    {
        const note_fx_source_reservation_t *const reservation =
            &g_note_fx_source_reservation[i];
        if ((reservation->active != 0U)
                && (reservation->track == track)
                && (reservation->provenance == provenance)
                && (reservation->occurrence_id == occurrence_id))
        {
            return (int16_t)i;
        }
    }
    return -1;
}

static int16_t note_fx_pipeline_find_free_source_reservation_locked(void)
{
    for (uint8_t i = 0U; i < NOTE_FX_SOURCE_RESERVATION_CAPACITY; ++i)
    {
        if (g_note_fx_source_reservation[i].active == 0U)
            return (int16_t)i;
    }
    return -1;
}

static void note_fx_pipeline_release_source_reservation_locked(int16_t index);

static void note_fx_pipeline_rebuild_source_reservations_locked(void)
{
    memset(g_note_fx_source_reservation, 0,
           sizeof(g_note_fx_source_reservation));
    g_note_fx_source_reservation_count = 0U;
    uint8_t cursor = g_note_fx_command_tail;
    while (cursor != g_note_fx_command_head)
    {
        const note_fx_command_t *const command = &g_note_fx_commands[cursor];
        if ((command->kind == NOTE_FX_COMMAND_SOURCE_RAW)
                && (command->is_note_on != 0U))
        {
            const int16_t free_index =
                note_fx_pipeline_find_free_source_reservation_locked();
            if (free_index >= 0)
            {
                g_note_fx_source_reservation[(uint8_t)free_index] =
                    (note_fx_source_reservation_t){
                        .active = 1U,
                        .track = command->track,
                        .provenance = command->provenance,
                        .occurrence_id = command->source_occurrence_id
                    };
                ++g_note_fx_source_reservation_count;
            }
        }
        else if ((command->kind == NOTE_FX_COMMAND_SOURCE_RAW)
                 && (command->is_note_on == 0U))
        {
            const int16_t reservation_index =
                note_fx_pipeline_find_source_reservation_locked(
                    command->track, command->provenance,
                    command->source_occurrence_id);
            note_fx_pipeline_release_source_reservation_locked(
                reservation_index);
        }
        cursor = (uint8_t)((cursor + 1U) % NOTE_FX_COMMAND_CAPACITY);
    }
}

static void note_fx_pipeline_release_source_reservation_locked(int16_t index)
{
    if ((index >= 0) && (index < (int16_t)NOTE_FX_SOURCE_RESERVATION_CAPACITY)
            && (g_note_fx_source_reservation[(uint8_t)index].active != 0U))
    {
        g_note_fx_source_reservation[(uint8_t)index].active = 0U;
        if (g_note_fx_source_reservation_count > 0U)
            --g_note_fx_source_reservation_count;
    }
}

static void note_fx_pipeline_release_source_reservations_for_track(uint8_t track)
{
    const uint32_t primask = note_fx_pipeline_enter_critical();
    for (uint8_t i = 0U; i < NOTE_FX_SOURCE_RESERVATION_CAPACITY; ++i)
    {
        if ((g_note_fx_source_reservation[i].active != 0U)
                && (g_note_fx_source_reservation[i].track == track))
        {
            note_fx_pipeline_release_source_reservation_locked((int16_t)i);
        }
    }
    note_fx_pipeline_exit_critical(primask);
}

static void note_fx_pipeline_drop_pending_closures_for_track(uint8_t track)
{
    const uint32_t primask = note_fx_pipeline_enter_critical();
    uint8_t write_index = 0U;
    for (uint8_t read_index = 0U;
         read_index < g_note_fx_pending_closure_count; ++read_index)
    {
        const note_fx_command_t command =
            g_note_fx_pending_closure[read_index];
        if ((command.track == track)
                && (command.kind == NOTE_FX_COMMAND_SOURCE_RAW))
        {
            continue;
        }
        if (write_index != read_index)
            g_note_fx_pending_closure[write_index] = command;
        ++write_index;
    }
    g_note_fx_pending_closure_count = write_index;
    note_fx_pipeline_exit_critical(primask);
}

static uint8_t note_fx_pipeline_pending_closure_add(
    const note_fx_command_t *command)
{
    if (command == NULL)
        return 0U;

    const uint32_t primask = note_fx_pipeline_enter_critical();
    if (g_note_fx_pending_closure_count >= NOTE_FX_SOURCE_RESERVATION_CAPACITY)
    {
        note_fx_pipeline_exit_critical(primask);
        return 0U;
    }
    g_note_fx_pending_closure[g_note_fx_pending_closure_count++] = *command;
    note_fx_pipeline_exit_critical(primask);
    return 1U;
}

static uint8_t note_fx_pipeline_pending_closure_take(
    uint8_t index, note_fx_command_t *out_command)
{
    if ((out_command == NULL) || (index >= g_note_fx_pending_closure_count))
        return 0U;

    const uint32_t primask = note_fx_pipeline_enter_critical();
    if (index >= g_note_fx_pending_closure_count)
    {
        note_fx_pipeline_exit_critical(primask);
        return 0U;
    }
    *out_command = g_note_fx_pending_closure[index];
    for (uint8_t i = index; (uint8_t)(i + 1U) < g_note_fx_pending_closure_count; ++i)
        g_note_fx_pending_closure[i] = g_note_fx_pending_closure[i + 1U];
    --g_note_fx_pending_closure_count;
    note_fx_pipeline_exit_critical(primask);
    return 1U;
}

static note_fx_result_t note_fx_pipeline_stage_emit(const note_event_t *event,
                                                     void *context);

static note_fx_result_t note_fx_pipeline_budget_admit(note_event_t *event,
                                                       uint8_t *reserved_here)
{
    *reserved_here = 0U;
    if ((event->flags & NOTE_EVENT_FLAG_GENERATED) == 0U)
    {
        return NOTE_EVENT_RESULT_ACCEPTED;
    }
    if (g_note_fx_half_budget.active == 0U)
    {
        if (event->kind == NOTE_EVENT_KIND_ON)
            event->flags |= NOTE_EVENT_FLAG_CLOSURE_RESERVED;
        return NOTE_EVENT_RESULT_ACCEPTED;
    }
    if ((event->flags & NOTE_EVENT_FLAG_BUDGET_ACCOUNTED) != 0U)
        return NOTE_EVENT_RESULT_ACCEPTED;

    if (event->kind == NOTE_EVENT_KIND_OFF)
    {
        if ((event->flags & NOTE_EVENT_FLAG_CLOSURE_RESERVED) == 0U)
        {
            if (g_note_fx_half_budget.off_remaining == 0U)
                return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
            --g_note_fx_half_budget.off_remaining;
        }
        /* The generated On already reserved this closure budget.  The
         * matching Off consumes the reservation but must not charge it a
         * second time (nor bypass the reserve when it is exhausted). */
        event->flags |= NOTE_EVENT_FLAG_BUDGET_ACCOUNTED;
    }
    else
    {
        if ((event->flags & NOTE_EVENT_FLAG_CLOSURE_RESERVED) != 0U)
            return NOTE_EVENT_RESULT_ACCEPTED;
        if ((event->track >= NOTE_FX_TRACK_COUNT)
                || (g_note_fx_half_budget.on_remaining[event->track] == 0U)
                || (g_note_fx_half_budget.off_remaining == 0U))
            return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
        --g_note_fx_half_budget.on_remaining[event->track];
        --g_note_fx_half_budget.off_remaining;
        event->flags |= NOTE_EVENT_FLAG_CLOSURE_RESERVED
                      | NOTE_EVENT_FLAG_BUDGET_ACCOUNTED;
        *reserved_here = 1U;
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

    const uint32_t primask = note_fx_pipeline_enter_critical();
    note_fx_command_t queued_command = *command;
    queued_command.panic_epoch = g_note_fx_panic_epoch;
    command = &queued_command;
    if ((command->kind == NOTE_FX_COMMAND_TRANSITION_TRACK)
            || (command->kind == NOTE_FX_COMMAND_TRANSITION_ALL))
    {
        uint8_t cursor = g_note_fx_command_tail;
        while (cursor != g_note_fx_command_head)
        {
            const note_fx_command_t *const pending = &g_note_fx_commands[cursor];
            if ((pending->kind == command->kind)
                    && (pending->track == command->track)
                    && (pending->policy == command->policy))
            {
                note_fx_pipeline_exit_critical(primask);
                return 1U;
            }
            cursor = (uint8_t)((cursor + 1U) % NOTE_FX_COMMAND_CAPACITY);
        }
    }

    const uint8_t head = g_note_fx_command_head;
    const uint8_t depth = (uint8_t)((head + NOTE_FX_COMMAND_CAPACITY
                                     - g_note_fx_command_tail)
                                    % NOTE_FX_COMMAND_CAPACITY);
    const uint8_t free_slots = (uint8_t)((NOTE_FX_COMMAND_CAPACITY - 1U) - depth);
    const uint8_t is_source_on = (uint8_t)(
        (command->kind == NOTE_FX_COMMAND_SOURCE_RAW)
        && (command->is_note_on != 0U));
    const uint8_t is_source_off = (uint8_t)(
        (command->kind == NOTE_FX_COMMAND_SOURCE_RAW)
        && (command->is_note_on == 0U));
    int16_t reservation_index = -1;
    int16_t new_reservation_index = -1;

    if (is_source_on != 0U)
    {
        if (g_note_fx_source_reservation_count
                >= NOTE_FX_SOURCE_RESERVATION_CAPACITY)
        {
            ++g_note_fx_command_drop_count;
            note_fx_pipeline_exit_critical(primask);
            return 0U;
        }
        if (((uint16_t)g_note_fx_source_reservation_count
             + (uint16_t)g_note_fx_pending_closure_count)
                >= NOTE_FX_SOURCE_RESERVATION_CAPACITY)
        {
            ++g_note_fx_command_drop_count;
            note_fx_pipeline_exit_critical(primask);
            return 0U;
        }
        if (note_fx_pipeline_find_source_reservation_locked(
                command->track, command->provenance,
                command->source_occurrence_id) >= 0)
        {
            ++g_note_fx_command_drop_count;
            note_fx_pipeline_exit_critical(primask);
            return 0U;
        }
        /* The On itself and one future Off must both fit before accepting. */
        if (free_slots <= (uint8_t)(g_note_fx_source_reservation_count + 1U))
        {
            ++g_note_fx_command_drop_count;
            note_fx_pipeline_exit_critical(primask);
            return 0U;
        }
        new_reservation_index = note_fx_pipeline_find_free_source_reservation_locked();
        if (new_reservation_index < 0)
        {
            ++g_note_fx_command_drop_count;
            note_fx_pipeline_exit_critical(primask);
            return 0U;
        }
        g_note_fx_source_reservation[(uint8_t)new_reservation_index] =
            (note_fx_source_reservation_t){
                .active = 1U,
                .track = command->track,
                .provenance = command->provenance,
                .occurrence_id = command->source_occurrence_id
            };
        ++g_note_fx_source_reservation_count;
    }
    else if (is_source_off != 0U)
    {
        reservation_index = note_fx_pipeline_find_source_reservation_locked(
            command->track, command->provenance,
            command->source_occurrence_id);
        if ((reservation_index < 0) && (free_slots == 0U))
        {
            ++g_note_fx_command_drop_count;
            note_fx_pipeline_exit_critical(primask);
            return 0U;
        }
    }
    else if (free_slots <= g_note_fx_source_reservation_count)
    {
        /* Keep every slot promised to a source Note Off unavailable. */
        ++g_note_fx_command_drop_count;
        note_fx_pipeline_exit_critical(primask);
        return 0U;
    }

    const uint8_t next = (uint8_t)((head + 1U) % NOTE_FX_COMMAND_CAPACITY);
    if (next == g_note_fx_command_tail)
    {
        note_fx_pipeline_release_source_reservation_locked(new_reservation_index);
        ++g_note_fx_command_drop_count;
        note_fx_pipeline_exit_critical(primask);
        return 0U;
    }

    g_note_fx_commands[head] = *command;
    g_note_fx_command_head = next;
    if (reservation_index >= 0)
        note_fx_pipeline_release_source_reservation_locked(reservation_index);
    const uint8_t depth_after = (uint8_t)((next + NOTE_FX_COMMAND_CAPACITY
                                     - g_note_fx_command_tail)
                                    % NOTE_FX_COMMAND_CAPACITY);
    if (depth_after > g_note_fx_command_high_water)
    {
        g_note_fx_command_high_water = depth_after;
    }
    note_fx_pipeline_exit_critical(primask);
    return 1U;
}

static uint8_t note_fx_pipeline_enqueue_batch(const note_fx_command_t *commands,
                                               uint8_t count)
{
    if ((commands == NULL) || (count == 0U)
            || (count >= NOTE_FX_COMMAND_CAPACITY))
        return 0U;

    const uint32_t primask = note_fx_pipeline_enter_critical();
    const uint8_t depth = (uint8_t)((g_note_fx_command_head
        + NOTE_FX_COMMAND_CAPACITY - g_note_fx_command_tail)
        % NOTE_FX_COMMAND_CAPACITY);
    const uint8_t free_slots = (uint8_t)((NOTE_FX_COMMAND_CAPACITY - 1U) - depth);
    if ((count > free_slots)
            || (free_slots < g_note_fx_source_reservation_count)
            || (count > (uint8_t)(free_slots
                                  - g_note_fx_source_reservation_count)))
    {
        g_note_fx_command_drop_count += count;
        note_fx_pipeline_exit_critical(primask);
        return 0U;
    }

    uint8_t head = g_note_fx_command_head;
    for (uint8_t i = 0U; i < count; ++i)
    {
        g_note_fx_commands[head] = commands[i];
        g_note_fx_commands[head].panic_epoch = g_note_fx_panic_epoch;
        head = (uint8_t)((head + 1U) % NOTE_FX_COMMAND_CAPACITY);
    }
    __DMB();
    g_note_fx_command_head = head;
    const uint8_t next_depth = (uint8_t)(depth + count);
    if (next_depth > g_note_fx_command_high_water)
        g_note_fx_command_high_water = next_depth;
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
    note_fx_event_t admitted = *event;
    uint8_t reserved_here = 0U;
    const note_fx_result_t budget_result =
        note_fx_pipeline_budget_admit(&admitted, &reserved_here);
    if (budget_result != NOTE_EVENT_RESULT_ACCEPTED)
    {
        if (event->kind == NOTE_EVENT_KIND_OFF)
            ++g_note_fx_pipeline_diag[event->track].budget_off_drop_count;
        else
            ++g_note_fx_pipeline_diag[event->track].budget_on_drop_count;
        ++g_note_fx_pipeline_diag[event->track].continuation_drop_count;
        return budget_result;
    }
    ++g_note_fx_pipeline_diag[admitted.track].stage_emissions[admitted.stage];
    if (admitted.stage > g_note_fx_pipeline_diag[admitted.track].max_stage_reached)
        g_note_fx_pipeline_diag[admitted.track].max_stage_reached = admitted.stage;
    note_fx_result_t result;
    if (admitted.stage >= NOTE_EVENT_STAGE_TERMINAL)
    {
        result = note_fx_pipeline_terminal(&admitted, NULL);
    }
    else
    {
        result = note_fx_engine_stage_source(&admitted, admitted.stage,
                                             note_fx_pipeline_stage_emit, NULL);
    }
    if ((reserved_here != 0U) && (result != NOTE_EVENT_RESULT_ACCEPTED))
    {
        ++g_note_fx_half_budget.on_remaining[admitted.track];
        ++g_note_fx_half_budget.off_remaining;
        --g_note_fx_half_budget.emissions_used;
    }
    return result;
}

static void note_fx_pipeline_sync_track_owner(
    uint8_t track, const note_fx_track_state_t *state)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (state == NULL))
    {
        return;
    }
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        uint8_t value[NOTE_FX_PARAM_COUNT];
        for (uint8_t param = 0U; param < NOTE_FX_PARAM_COUNT; ++param)
            value[param] = g_note_fx_override_valid[track][slot][param]
                ? g_note_fx_override_value[track][slot][param]
                : state->value[slot][param];
        note_fx_engine_configure(track, slot, value[3], value[0], value[1], value[2]);
    }
}

void note_fx_pipeline_init(void)
{
    memset(g_note_fx_override_valid, 0, sizeof(g_note_fx_override_valid));
    memset(g_note_fx_source_ledger, 0, sizeof(g_note_fx_source_ledger));
    memset(g_note_fx_pipeline_diag, 0, sizeof(g_note_fx_pipeline_diag));
    memset(g_note_fx_commands, 0, sizeof(g_note_fx_commands));
    memset(g_note_fx_source_reservation, 0,
           sizeof(g_note_fx_source_reservation));
    memset(g_note_fx_pending_closure, 0, sizeof(g_note_fx_pending_closure));
    memset(g_note_fx_live_queue, 0, sizeof(g_note_fx_live_queue));
    g_note_fx_command_head = 0U;
    g_note_fx_command_tail = 0U;
    g_note_fx_command_high_water = 0U;
    g_note_fx_command_drop_count = 0U;
    g_note_fx_source_reservation_count = 0U;
    g_note_fx_pending_closure_count = 0U;
    g_note_fx_live_queue_count = 0U;
    g_note_fx_live_queue_high_water = 0U;
    g_note_fx_live_late_count = 0U;
    g_note_fx_live_stale_count = 0U;
    g_note_fx_live_queue_drop_count = 0U;
    g_note_fx_live_max_lateness_samples = 0U;
    g_note_fx_live_fallback_serial = 0U;
    memset(&g_note_fx_half_budget, 0, sizeof(g_note_fx_half_budget));
    g_note_fx_last_half_emissions = 0U;
    g_note_fx_max_half_emissions = 0U;
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        g_note_fx_source_generation[track] = 1U;
    note_fx_engine_init();
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        note_fx_track_state_t state;
        if (note_fx_state_capture_track(track, &state) != 0U)
            note_fx_pipeline_sync_track_owner(track, &state);
    }
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
    note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_APPLY_PARAM,
        .track = track,
        .slot = slot,
        .param = param,
        .value = value
    };
    command.track_state_valid = note_fx_state_capture_track(
        track, &command.track_state);
    if (command.track_state_valid == 0U)
        return 0U;
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
    note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_RELEASE_PARAM,
        .track = track,
        .slot = slot,
        .param = param
    };
    command.track_state_valid = note_fx_state_capture_track(
        track, &command.track_state);
    if (command.track_state_valid == 0U)
        return 0U;
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
    return (note_fx_pipeline_enqueue(&command) != 0U)
        ? NOTE_EVENT_RESULT_ACCEPTED
        : NOTE_EVENT_RESULT_REJECTED_CAPACITY;
}

static note_fx_result_t note_fx_pipeline_submit_source_audio(uint8_t track, uint8_t note,
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

    if (sample_time == NOTE_FX_SAMPLE_TIME_AUDIO_OWNER)
    {
        sample_time = seq_runtime_exec_get_audio_timeline_sample();
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
        if (note_fx_pipeline_find_source(track, source_occurrence_id,
                                         provenance) >= 0)
        {
            ++g_note_fx_pipeline_diag[track].stale;
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

note_fx_result_t note_fx_pipeline_submit_source_occurrence(
    uint8_t track, uint8_t note, uint8_t velocity, uint8_t is_note_on,
    uint64_t sample_time, note_event_provenance_t provenance,
    uint32_t source_occurrence_id)
{
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

note_fx_result_t note_fx_pipeline_submit_source_capture_tick(
    uint8_t track, uint8_t note, uint8_t velocity, uint8_t is_note_on,
    uint32_t capture_tick, uint32_t ingress_serial,
    note_event_provenance_t provenance,
    uint32_t source_occurrence_id)
{
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
        .sample_time = NOTE_FX_SAMPLE_TIME_AUDIO_OWNER,
        .source_occurrence_id = source_occurrence_id,
        .ingress_serial = ingress_serial,
        .capture_tick = capture_tick,
        .capture_tick_valid = 1U
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
    note_fx_pipeline_drop_pending_closures_for_track(track);
    note_fx_pipeline_release_source_reservations_for_track(track);
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

static void note_fx_pipeline_apply_runtime_param_owner(
    const note_fx_command_t *command)
{
    if ((command == NULL) || (command->track >= NOTE_FX_TRACK_COUNT)
            || (command->slot >= NOTE_FX_SLOT_COUNT)
            || (command->param >= NOTE_FX_PARAM_COUNT)
            || (command->track_state_valid == 0U))
    {
        return;
    }

    const uint8_t track = command->track;
    const uint8_t slot = command->slot;
    const uint8_t param = command->param;
    const uint8_t previous = g_note_fx_override_valid[track][slot][param]
        ? g_note_fx_override_value[track][slot][param]
        : command->track_state.value[slot][param];
    if ((param == 3U) && (previous != command->value))
    {
        note_fx_pipeline_cleanup_track_owner(track);
    }
    g_note_fx_override_valid[track][slot][param] = 1U;
    g_note_fx_override_value[track][slot][param] = command->value;
    note_fx_pipeline_sync_track_owner(track, &command->track_state);
}

static void note_fx_pipeline_release_runtime_param_owner(
    const note_fx_command_t *command)
{
    if ((command == NULL) || (command->track >= NOTE_FX_TRACK_COUNT)
            || (command->slot >= NOTE_FX_SLOT_COUNT)
            || (command->param >= NOTE_FX_PARAM_COUNT)
            || (command->track_state_valid == 0U))
    {
        return;
    }

    const uint8_t track = command->track;
    const uint8_t slot = command->slot;
    const uint8_t param = command->param;
    if ((param == 3U) && (g_note_fx_override_valid[track][slot][param] != 0U)
            && (g_note_fx_override_value[track][slot][param]
                != command->track_state.value[slot][param]))
    {
        note_fx_pipeline_cleanup_track_owner(track);
    }
    g_note_fx_override_valid[track][slot][param] = 0U;
    note_fx_pipeline_sync_track_owner(track, &command->track_state);
}

static void note_fx_pipeline_reset_runtime_overrides_owner(
    const note_fx_command_t *command)
{
    if ((command == NULL) || (command->track >= NOTE_FX_TRACK_COUNT)
            || (command->track_state_valid == 0U))
    {
        return;
    }
    const uint8_t track = command->track;
    note_fx_pipeline_cleanup_track_owner(track);
    memset(g_note_fx_override_valid[track], 0, sizeof(g_note_fx_override_valid[track]));
    note_fx_pipeline_sync_track_owner(track, &command->track_state);
}

static void note_fx_pipeline_release_source_reservation(
    const note_fx_command_t *command)
{
    if ((command == NULL) || (command->kind != NOTE_FX_COMMAND_SOURCE_RAW))
        return;

    const uint32_t primask = note_fx_pipeline_enter_critical();
    const int16_t index = note_fx_pipeline_find_source_reservation_locked(
        command->track, command->provenance, command->source_occurrence_id);
    note_fx_pipeline_release_source_reservation_locked(index);
    note_fx_pipeline_exit_critical(primask);
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
        ++g_note_fx_live_queue_drop_count;
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
    if (g_note_fx_live_queue_count > g_note_fx_live_queue_high_water)
        g_note_fx_live_queue_high_water = g_note_fx_live_queue_count;
    return 1U;
}

static note_fx_result_t note_fx_pipeline_submit_live_command(
    const note_fx_command_t *command, uint64_t now)
{
    if (command->capture_tick_valid == 0U)
    {
        return note_fx_pipeline_submit_source_audio(
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
        const uint64_t lateness = now - sample_time;
        ++g_note_fx_live_late_count;
        if (lateness > g_note_fx_live_max_lateness_samples)
            g_note_fx_live_max_lateness_samples = lateness;
        if (lateness > NOTE_FX_LIVE_STALE_THRESHOLD_SAMPLES)
            ++g_note_fx_live_stale_count;
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

static void note_fx_pipeline_apply_due_live_events(uint64_t now)
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
        const note_fx_result_t result = note_fx_pipeline_submit_source_audio(
            command.track, command.note, command.velocity, command.is_note_on,
            event.sample_time, provenance, command.source_occurrence_id);
        if (command.is_note_on != 0U)
        {
            if (result != NOTE_EVENT_RESULT_ACCEPTED)
                note_fx_pipeline_release_source_reservation(&command);
        }
        else if ((result != NOTE_EVENT_RESULT_ACCEPTED)
                && (result != NOTE_EVENT_RESULT_REJECTED_STALE))
        {
            (void)note_fx_pipeline_pending_closure_add(&command);
        }
    }
}

static note_fx_result_t note_fx_pipeline_apply_source_raw_command(
    const note_fx_command_t *command)
{
    const uint64_t now = seq_runtime_exec_get_audio_timeline_sample();
    const note_fx_result_t result =
        note_fx_pipeline_submit_live_command(command, now);

    if (command->is_note_on != 0U)
    {
        if (result != NOTE_EVENT_RESULT_ACCEPTED)
            note_fx_pipeline_release_source_reservation(command);
    }
    else if ((result != NOTE_EVENT_RESULT_ACCEPTED)
            && (result != NOTE_EVENT_RESULT_REJECTED_STALE))
    {
        /* The owner keeps the exact Off until the downstream owner accepts it. */
        (void)note_fx_pipeline_pending_closure_add(command);
    }
    return result;
}

static void note_fx_pipeline_apply_pending_closures(void)
{
    uint8_t attempts = g_note_fx_pending_closure_count;
    while ((attempts > 0U)
            && ((g_note_fx_half_budget.active == 0U)
                || (g_note_fx_half_budget.commands_used
                    < NOTE_FX_HALF_COMMAND_QUOTA)))
    {
        note_fx_command_t command;
        if (note_fx_pipeline_pending_closure_take(0U, &command) == 0U)
            break;
        --attempts;
        if (g_note_fx_half_budget.active != 0U)
            ++g_note_fx_half_budget.commands_used;

        const note_fx_result_t result = note_fx_pipeline_submit_source_audio(
            command.track,
            command.note,
            command.velocity,
            command.is_note_on,
            command.sample_time,
            (note_event_provenance_t)command.provenance,
            command.source_occurrence_id);
        if ((result != NOTE_EVENT_RESULT_ACCEPTED)
                && (result != NOTE_EVENT_RESULT_REJECTED_STALE))
        {
            /* Reinsert at the tail of the bounded retry list. */
            (void)note_fx_pipeline_pending_closure_add(&command);
        }
    }
}

static void note_fx_pipeline_apply_pending_commands(void)
{
    note_fx_pipeline_apply_pending_closures();
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
                (void)note_fx_pipeline_apply_source_raw_command(&command);
                break;
            case NOTE_FX_COMMAND_APPLY_PARAM:
                note_fx_pipeline_apply_runtime_param_owner(&command);
                break;
            case NOTE_FX_COMMAND_RELEASE_PARAM:
                note_fx_pipeline_release_runtime_param_owner(&command);
                break;
            case NOTE_FX_COMMAND_SYNC_TRACK:
                if (command.track_state_valid != 0U)
                    note_fx_pipeline_sync_track_owner(command.track,
                                                       &command.track_state);
                break;
            case NOTE_FX_COMMAND_TRANSITION_TRACK:
                note_fx_pipeline_transition_track_owner(command.track, command.policy);
                break;
            case NOTE_FX_COMMAND_TRANSITION_ALL:
                for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
                    note_fx_pipeline_transition_track_owner(track, command.policy);
                break;
            case NOTE_FX_COMMAND_RESET_TRACK:
                note_fx_pipeline_reset_runtime_overrides_owner(&command);
                break;
            default:
                break;
        }
    }
}

uint8_t note_fx_pipeline_request_panic(void)
{
    const uint32_t primask = note_fx_pipeline_enter_critical();
    ++g_note_fx_panic_epoch;
    if (g_note_fx_panic_epoch == 0U)
        g_note_fx_panic_epoch = 1U;
    g_note_fx_panic_pending = 1U;
    note_fx_pipeline_exit_critical(primask);
    return 1U;
}

static void note_fx_pipeline_purge_for_panic_owner(uint32_t panic_epoch)
{
    const uint32_t primask = note_fx_pipeline_enter_critical();
    uint8_t read_index = g_note_fx_command_tail;
    uint8_t write_index = g_note_fx_command_tail;
    while (read_index != g_note_fx_command_head)
    {
        const note_fx_command_t command = g_note_fx_commands[read_index];
        read_index = (uint8_t)((read_index + 1U) % NOTE_FX_COMMAND_CAPACITY);
        if (command.panic_epoch != panic_epoch)
            continue;
        g_note_fx_commands[write_index] = command;
        write_index = (uint8_t)((write_index + 1U) % NOTE_FX_COMMAND_CAPACITY);
    }
    g_note_fx_command_head = write_index;
    memset(g_note_fx_pending_closure, 0, sizeof(g_note_fx_pending_closure));
    g_note_fx_pending_closure_count = 0U;
    memset(g_note_fx_source_ledger, 0, sizeof(g_note_fx_source_ledger));
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        ++g_note_fx_source_generation[track];
        if (g_note_fx_source_generation[track] == 0U)
            g_note_fx_source_generation[track] = 1U;
    }
    memset(g_note_fx_live_queue, 0, sizeof(g_note_fx_live_queue));
    g_note_fx_live_queue_count = 0U;
    note_fx_pipeline_rebuild_source_reservations_locked();
    g_note_fx_panic_pending = 0U;
    note_fx_pipeline_exit_critical(primask);
}

static uint8_t note_fx_pipeline_apply_panic_owner(uint64_t first_renderable_sample)
{
    uint8_t requested = 0U;
    uint32_t panic_epoch = 0U;
    const uint32_t primask = note_fx_pipeline_enter_critical();
    if (g_note_fx_panic_pending != 0U)
    {
        g_note_fx_panic_active = 1U;
        requested = 1U;
        panic_epoch = g_note_fx_panic_epoch;
    }
    else if (g_note_fx_panic_active != 0U)
    {
        requested = 1U;
    }
    note_fx_pipeline_exit_critical(primask);

    if (requested == 0U)
        return 0U;
    if (panic_epoch != 0U)
        note_fx_pipeline_purge_for_panic_owner(panic_epoch);

    if (seq_play_scheduler_panic_audio(first_renderable_sample) == 0U)
        return 1U;

    /* Terminal admissions have already emitted their stops.  This cleanup
     * only resets NoteFx/ARP ownership and cannot create a second voice path. */
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        note_fx_engine_cleanup(track, first_renderable_sample, NULL, NULL);

    const uint32_t complete_primask = note_fx_pipeline_enter_critical();
    g_note_fx_panic_active = 0U;
    note_fx_pipeline_exit_critical(complete_primask);
    return 1U;
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
    if ((track >= NOTE_FX_TRACK_COUNT)
            || (policy > NOTE_FX_TRANSITION_SOURCE_CLOCK_CHANGE))
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
    if (policy > NOTE_FX_TRANSITION_SOURCE_CLOCK_CHANGE)
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
    note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_SYNC_TRACK,
        .track = track
    };
    command.track_state_valid = note_fx_state_capture_track(
        track, &command.track_state);
    if (command.track_state_valid == 0U)
        return 0U;
    return note_fx_pipeline_enqueue(&command);
}

void note_fx_pipeline_reset_runtime_overrides(uint8_t track)
{
    if (track >= NOTE_FX_TRACK_COUNT)
    {
        return;
    }
    note_fx_command_t command = {
        .kind = NOTE_FX_COMMAND_RESET_TRACK,
        .track = track
    };
    command.track_state_valid = note_fx_state_capture_track(
        track, &command.track_state);
    if (command.track_state_valid == 0U)
        return;
    (void)note_fx_pipeline_enqueue(&command);
}

void note_fx_pipeline_reset_all_runtime_overrides(void)
{
    note_fx_command_t commands[NOTE_FX_TRACK_COUNT];
    memset(commands, 0, sizeof(commands));
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        commands[track].kind = NOTE_FX_COMMAND_RESET_TRACK;
        commands[track].track = track;
        commands[track].track_state_valid = note_fx_state_capture_track(
            track, &commands[track].track_state);
        if (commands[track].track_state_valid == 0U)
            return;
    }
    (void)note_fx_pipeline_enqueue_batch(commands, NOTE_FX_TRACK_COUNT);
}

void note_fx_pipeline_process(uint64_t block_start, uint16_t frames,
                              uint32_t samples_per_step_q16)
{
    if (note_fx_pipeline_apply_panic_owner(block_start) != 0U)
        return;
    note_fx_pipeline_apply_pending_commands();
    note_fx_pipeline_apply_due_live_events(block_start);
    note_fx_engine_process(block_start, frames, samples_per_step_q16,
                           note_fx_pipeline_stage_emit, 0);
}

uint16_t note_fx_pipeline_frames_until_deadline(uint64_t block_start,
                                                uint16_t max_frames)
{
    const uint32_t primask = note_fx_pipeline_enter_critical();
    const uint8_t panic_pending = (uint8_t)(
        (g_note_fx_panic_pending != 0U) || (g_note_fx_panic_active != 0U));
    note_fx_pipeline_exit_critical(primask);
    if (panic_pending != 0U)
        return (max_frames != 0U) ? 1U : 0U;

    uint64_t deadline = note_fx_engine_next_deadline();
    if ((g_note_fx_live_queue_count != 0U)
            && (g_note_fx_live_queue[0].sample_time < deadline))
    {
        deadline = g_note_fx_live_queue[0].sample_time;
    }
    if ((deadline <= block_start) || (deadline >= block_start + max_frames))
    {
        return max_frames;
    }
    return (uint16_t)(deadline - block_start);
}

void note_fx_pipeline_get_live_queue_diag(note_fx_live_queue_diag_t *out_diag)
{
    if (out_diag == NULL)
        return;
    const uint32_t primask = note_fx_pipeline_enter_critical();
    out_diag->late_count = g_note_fx_live_late_count;
    out_diag->stale_count = g_note_fx_live_stale_count;
    out_diag->queue_drop_count = g_note_fx_live_queue_drop_count;
    out_diag->max_lateness_samples = g_note_fx_live_max_lateness_samples;
    out_diag->depth = g_note_fx_live_queue_count;
    out_diag->high_water = g_note_fx_live_queue_high_water;
    note_fx_pipeline_exit_critical(primask);
}
