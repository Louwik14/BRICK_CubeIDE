#include "IPC/control_audio_fifo_control.h"

#include <stddef.h>
#include "stm32h7xx.h"
#include "Track/entity_topology.h"

#define FIFO g_control_audio_fifo_layout
#define FIFO_DEBUG_UNAVAILABLE UINT32_MAX

volatile uint32_t g_control_audio_fifo_debug_reject_reason;
volatile uint32_t g_control_audio_fifo_debug_index;
volatile uint32_t g_control_audio_fifo_debug_head;
volatile uint32_t g_control_audio_fifo_debug_tail;
volatile uint64_t g_control_audio_fifo_debug_floor;
volatile uint64_t g_control_audio_fifo_debug_command_time;
volatile uint32_t g_control_audio_fifo_debug_opcode;
volatile uint32_t g_control_audio_fifo_debug_entity;
volatile uint32_t g_control_audio_fifo_debug_param_id;
volatile uint64_t g_fifo_debug_rejected_time;
volatile uint64_t g_fifo_debug_floor;
volatile uint16_t g_fifo_debug_param_id;
volatile uint8_t g_fifo_debug_entity;
volatile uint8_t g_fifo_debug_opcode;

static void control_audio_fifo_debug_capture(
    control_audio_fifo_debug_reject_reason_t reason, uint32_t index,
    uint32_t head, uint32_t tail, uint64_t floor,
    const control_audio_command_t *command)
{
    if (g_control_audio_fifo_debug_reject_reason !=
        CONTROL_AUDIO_FIFO_DEBUG_REJECT_NONE) return;

    g_control_audio_fifo_debug_index = index;
    g_control_audio_fifo_debug_head = head;
    g_control_audio_fifo_debug_tail = tail;
    g_control_audio_fifo_debug_floor = floor;
    g_control_audio_fifo_debug_command_time = command
        ? command->effective_sample_time : 0U;
    g_control_audio_fifo_debug_opcode = command
        ? CONTROL_AUDIO_COMMAND_OPCODE(command) : FIFO_DEBUG_UNAVAILABLE;
    g_control_audio_fifo_debug_entity = command
        ? command->entity : FIFO_DEBUG_UNAVAILABLE;
    g_control_audio_fifo_debug_param_id = command
        ? command->id : FIFO_DEBUG_UNAVAILABLE;
    /* Publish the latch last, after all diagnostic fields are populated. */
    g_control_audio_fifo_debug_reject_reason = (uint32_t)reason;
}

void control_audio_fifo_control_init(void)
{
    FIFO.head = 0U;
    FIFO.tail = 0U;
    FIFO.overflow_count = 0U;
    FIFO.invariant_failure_count = 0U;
    g_control_audio_fifo_debug_reject_reason =
        CONTROL_AUDIO_FIFO_DEBUG_REJECT_NONE;
    __DMB();
}

uint16_t control_audio_fifo_control_free(void)
{
    const uint32_t used = FIFO.head - FIFO.tail;
    return (used < CONTROL_AUDIO_FIFO_CAPACITY)
        ? (uint16_t)(CONTROL_AUDIO_FIFO_CAPACITY - used) : 0U;
}

uint64_t control_audio_fifo_control_floor(void)
{
    const uint32_t head = FIFO.head;
    const uint32_t tail = FIFO.tail;
    __DMB();
    return (head != tail)
        ? g_control_audio_fifo_commands[(head - 1U)
            & (CONTROL_AUDIO_FIFO_CAPACITY - 1U)].effective_sample_time
        : 0U;
}

static uint8_t command_valid(const control_audio_command_t *command)
{
    const uint8_t opcode = command ? CONTROL_AUDIO_COMMAND_OPCODE(command) : UINT8_MAX;
    if ((command == NULL) || (opcode > CONTROL_AUDIO_COMMAND_PANIC)) return 0U;
    return ((opcode == CONTROL_AUDIO_COMMAND_TRANSPORT)
            || (opcode == CONTROL_AUDIO_COMMAND_RECORD)
            || (opcode == CONTROL_AUDIO_COMMAND_PANIC)
            || (command->entity < BRICK_ENTITY_CAPACITY)) ? 1U : 0U;
}

uint8_t control_audio_fifo_batch_begin(control_audio_fifo_batch_writer_t *writer,
                                       uint16_t count)
{
    if ((writer == NULL) || (count == 0U) || (count > CONTROL_AUDIO_FIFO_CAPACITY))
    {
        control_audio_fifo_debug_capture(
            CONTROL_AUDIO_FIFO_DEBUG_REJECT_BATCH_BEGIN_ARGUMENT,
            FIFO_DEBUG_UNAVAILABLE, FIFO.head, FIFO.tail, 0U, NULL);
        return 0U;
    }
    const uint32_t head = FIFO.head;
    const uint32_t tail = FIFO.tail;
    __DMB();
    if ((head - tail + count) > CONTROL_AUDIO_FIFO_CAPACITY)
    {
        control_audio_fifo_debug_capture(
            CONTROL_AUDIO_FIFO_DEBUG_REJECT_BATCH_BEGIN_CAPACITY,
            FIFO_DEBUG_UNAVAILABLE, head, tail, 0U, NULL);
        ++FIFO.overflow_count;
        return 0U;
    }
    uint64_t floor = 0U;
    if (head != tail) floor = g_control_audio_fifo_commands[(head - 1U)
        & (CONTROL_AUDIO_FIFO_CAPACITY - 1U)].effective_sample_time;
    *writer = (control_audio_fifo_batch_writer_t){ .head=head, .count=count,
        .written=0U, .floor=floor, .active=1U };
    return 1U;
}

uint8_t control_audio_fifo_batch_append(control_audio_fifo_batch_writer_t *writer,
                                        const control_audio_command_t *command)
{
    if (writer == NULL)
    {
        control_audio_fifo_debug_capture(
            CONTROL_AUDIO_FIFO_DEBUG_REJECT_APPEND_STATE,
            FIFO_DEBUG_UNAVAILABLE, FIFO.head, FIFO.tail, 0U, command);
        return 0U;
    }
    if ((writer->active == 0U) || (writer->written >= writer->count))
    {
        control_audio_fifo_debug_capture(
            CONTROL_AUDIO_FIFO_DEBUG_REJECT_APPEND_STATE, writer->written,
            FIFO.head, FIFO.tail, writer->floor, command);
        return 0U;
    }
    if (command_valid(command) == 0U)
    {
        control_audio_fifo_debug_capture(
            CONTROL_AUDIO_FIFO_DEBUG_REJECT_APPEND_COMMAND, writer->written,
            FIFO.head, FIFO.tail, writer->floor, command);
        return 0U;
    }
    if (command->effective_sample_time < writer->floor)
    {
        /* Publication order is an invariant.  Never rewrite a producer's
         * timestamp to hide a regression; reject the whole batch instead. */
        if (g_control_audio_fifo_debug_reject_reason ==
            CONTROL_AUDIO_FIFO_DEBUG_REJECT_NONE)
        {
            g_fifo_debug_rejected_time = command->effective_sample_time;
            g_fifo_debug_floor = writer->floor;
            g_fifo_debug_param_id = command->id;
            g_fifo_debug_entity = command->entity;
            g_fifo_debug_opcode = CONTROL_AUDIO_COMMAND_OPCODE(command);
        }
        control_audio_fifo_debug_capture(
            CONTROL_AUDIO_FIFO_DEBUG_REJECT_APPEND_RETROGRADE_TIME,
            writer->written, FIFO.head, FIFO.tail, writer->floor, command);
        ++FIFO.invariant_failure_count;
        return 0U;
    }
    writer->floor = command->effective_sample_time;
    g_control_audio_fifo_commands[(writer->head + writer->written)
        & (CONTROL_AUDIO_FIFO_CAPACITY - 1U)] = *command;
    ++writer->written;
    return 1U;
}

uint8_t control_audio_fifo_batch_commit(control_audio_fifo_batch_writer_t *writer)
{
    if (writer == NULL)
    {
        control_audio_fifo_debug_capture(
            CONTROL_AUDIO_FIFO_DEBUG_REJECT_COMMIT_STATE,
            FIFO_DEBUG_UNAVAILABLE, FIFO.head, FIFO.tail, 0U, NULL);
        return 0U;
    }
    if ((writer->active == 0U) || (writer->written != writer->count))
    {
        control_audio_fifo_debug_capture(
            CONTROL_AUDIO_FIFO_DEBUG_REJECT_COMMIT_STATE, writer->written,
            FIFO.head, FIFO.tail, writer->floor, NULL);
        return 0U;
    }
    __DMB(); FIFO.head = writer->head + writer->count; writer->active = 0U; return 1U;
}

void control_audio_fifo_batch_abort(control_audio_fifo_batch_writer_t *writer)
{ if (writer != NULL) writer->active = 0U; }

uint8_t control_audio_fifo_publish_batch(const control_audio_command_t *commands,
                                         uint16_t count)
{
    control_audio_fifo_batch_writer_t writer;
    if (commands == NULL)
    {
        control_audio_fifo_debug_capture(
            CONTROL_AUDIO_FIFO_DEBUG_REJECT_PUBLISH_ARGUMENT,
            FIFO_DEBUG_UNAVAILABLE, FIFO.head, FIFO.tail, 0U, NULL);
        return 0U;
    }
    if (control_audio_fifo_batch_begin(&writer, count) == 0U) return 0U;
    for (uint16_t i=0U; i<count; ++i)
        if (control_audio_fifo_batch_append(&writer, &commands[i]) == 0U)
        { control_audio_fifo_batch_abort(&writer); ++FIFO.invariant_failure_count; return 0U; }
    return control_audio_fifo_batch_commit(&writer);
}

uint8_t control_audio_fifo_publish(const control_audio_command_t *command)
{ return control_audio_fifo_publish_batch(command, 1U); }
