#include "IPC/control_audio_fifo_control.h"

#include <stddef.h>
#include "stm32h7xx.h"
#include "Track/entity_topology.h"

#define FIFO g_control_audio_fifo_layout

void control_audio_fifo_control_init(void)
{
    FIFO.head = 0U;
    FIFO.overflow_count = 0U;
    FIFO.invariant_failure_count = 0U;
    __DMB();
}

uint16_t control_audio_fifo_control_free(void)
{
    const uint32_t used = FIFO.head - FIFO.tail;
    return (used < CONTROL_AUDIO_FIFO_CAPACITY)
        ? (uint16_t)(CONTROL_AUDIO_FIFO_CAPACITY - used) : 0U;
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
    if ((writer == NULL) || (count == 0U) || (count > CONTROL_AUDIO_FIFO_CAPACITY)) return 0U;
    const uint32_t head = FIFO.head;
    const uint32_t tail = FIFO.tail;
    __DMB();
    if ((head - tail + count) > CONTROL_AUDIO_FIFO_CAPACITY)
    { ++FIFO.overflow_count; return 0U; }
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
    if ((writer == NULL) || (writer->active == 0U)
            || (writer->written >= writer->count) || (command_valid(command) == 0U)) return 0U;
    control_audio_command_t published = *command;
    if (published.effective_sample_time < writer->floor)
    { ++FIFO.invariant_failure_count; published.effective_sample_time = writer->floor; }
    writer->floor = published.effective_sample_time;
    g_control_audio_fifo_commands[(writer->head + writer->written)
        & (CONTROL_AUDIO_FIFO_CAPACITY - 1U)] = published;
    ++writer->written;
    return 1U;
}

uint8_t control_audio_fifo_batch_commit(control_audio_fifo_batch_writer_t *writer)
{
    if ((writer == NULL) || (writer->active == 0U) || (writer->written != writer->count)) return 0U;
    __DMB(); FIFO.head = writer->head + writer->count; writer->active = 0U; return 1U;
}

void control_audio_fifo_batch_abort(control_audio_fifo_batch_writer_t *writer)
{ if (writer != NULL) writer->active = 0U; }

uint8_t control_audio_fifo_publish_batch(const control_audio_command_t *commands,
                                         uint16_t count)
{
    control_audio_fifo_batch_writer_t writer;
    if ((commands == NULL) || (control_audio_fifo_batch_begin(&writer, count) == 0U)) return 0U;
    for (uint16_t i=0U; i<count; ++i)
        if (control_audio_fifo_batch_append(&writer, &commands[i]) == 0U)
        { control_audio_fifo_batch_abort(&writer); ++FIFO.invariant_failure_count; return 0U; }
    return control_audio_fifo_batch_commit(&writer);
}

uint8_t control_audio_fifo_publish(const control_audio_command_t *command)
{ return control_audio_fifo_publish_batch(command, 1U); }
