#include "Audio/control_audio_fifo.h"

#include <stddef.h>
#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overflow_count;
    volatile uint32_t invariant_failure_count;
} control_audio_fifo_state_t;

D3_IPC static control_audio_fifo_state_t g_control_audio_fifo;
AUDIO_STORAGE_SHARED_SDRAM static control_audio_command_t
    g_control_audio_fifo_commands[CONTROL_AUDIO_FIFO_CAPACITY];

void control_audio_fifo_init(void)
{
    g_control_audio_fifo.head = 0U;
    g_control_audio_fifo.tail = 0U;
    g_control_audio_fifo.overflow_count = 0U;
    g_control_audio_fifo.invariant_failure_count = 0U;
    __DMB();
}

uint16_t control_audio_fifo_control_free(void)
{
    const uint32_t used = g_control_audio_fifo.head - g_control_audio_fifo.tail;
    return (used < CONTROL_AUDIO_FIFO_CAPACITY)
        ? (uint16_t)(CONTROL_AUDIO_FIFO_CAPACITY - used) : 0U;
}

static uint8_t control_audio_command_valid(const control_audio_command_t *command)
{
    const uint8_t opcode = (command != NULL)
        ? CONTROL_AUDIO_COMMAND_OPCODE(command) : UINT8_MAX;
    if ((command == NULL) || (opcode > CONTROL_AUDIO_COMMAND_PANIC))
        return 0U;
    if ((opcode != CONTROL_AUDIO_COMMAND_TRANSPORT)
            && (opcode != CONTROL_AUDIO_COMMAND_RECORD)
            && (opcode != CONTROL_AUDIO_COMMAND_PANIC)
            && (command->entity >= BRICK_ENTITY_CAPACITY))
        return 0U;
    return 1U;
}

uint8_t control_audio_fifo_batch_begin(control_audio_fifo_batch_writer_t *writer,
                                       uint16_t count)
{
    if ((writer == NULL) || (count == 0U)
            || (count > CONTROL_AUDIO_FIFO_CAPACITY)) return 0U;
    const uint32_t head = g_control_audio_fifo.head;
    const uint32_t tail = g_control_audio_fifo.tail;
    __DMB();
    if ((head - tail + count) > CONTROL_AUDIO_FIFO_CAPACITY)
    {
        ++g_control_audio_fifo.overflow_count;
        return 0U;
    }
    uint64_t floor = 0U;
    if (head != tail)
        floor = g_control_audio_fifo_commands[(head - 1U)
            & (CONTROL_AUDIO_FIFO_CAPACITY - 1U)].effective_sample_time;
    *writer = (control_audio_fifo_batch_writer_t){
        .head = head, .count = count, .written = 0U,
        .floor = floor, .active = 1U
    };
    return 1U;
}

uint8_t control_audio_fifo_batch_append(control_audio_fifo_batch_writer_t *writer,
                                        const control_audio_command_t *command)
{
    if ((writer == NULL) || (writer->active == 0U)
            || (writer->written >= writer->count)
            || (control_audio_command_valid(command) == 0U)) return 0U;
    control_audio_command_t published = *command;
    if (published.effective_sample_time < writer->floor)
    {
        ++g_control_audio_fifo.invariant_failure_count;
        published.effective_sample_time = writer->floor;
    }
    writer->floor = published.effective_sample_time;
    g_control_audio_fifo_commands[(writer->head + writer->written)
        & (CONTROL_AUDIO_FIFO_CAPACITY - 1U)] = published;
    writer->written++;
    return 1U;
}

uint8_t control_audio_fifo_batch_commit(control_audio_fifo_batch_writer_t *writer)
{
    if ((writer == NULL) || (writer->active == 0U)
            || (writer->written != writer->count)) return 0U;
    __DMB();
    g_control_audio_fifo.head = writer->head + writer->count;
    writer->active = 0U;
    return 1U;
}

void control_audio_fifo_batch_abort(control_audio_fifo_batch_writer_t *writer)
{
    if (writer != NULL) writer->active = 0U;
}

uint8_t control_audio_fifo_publish_batch(const control_audio_command_t *commands,
                                         uint16_t count)
{
    if ((commands == NULL) || (count == 0U) || (count > CONTROL_AUDIO_FIFO_CAPACITY))
        return 0U;
    uint32_t head = g_control_audio_fifo.head;
    const uint32_t tail = g_control_audio_fifo.tail;
    __DMB();
    if ((head - tail + count) > CONTROL_AUDIO_FIFO_CAPACITY)
    {
        ++g_control_audio_fifo.overflow_count;
        return 0U;
    }
    uint64_t floor = 0U;
    if (head != tail)
        floor = g_control_audio_fifo_commands[(head - 1U)
            & (CONTROL_AUDIO_FIFO_CAPACITY - 1U)].effective_sample_time;
    for (uint16_t i = 0U; i < count; ++i)
        if (control_audio_command_valid(&commands[i]) == 0U)
        {
            ++g_control_audio_fifo.invariant_failure_count;
            return 0U;
        }
    for (uint16_t i = 0U; i < count; ++i)
    {
        control_audio_command_t published = commands[i];
        if (published.effective_sample_time < floor)
        {
            ++g_control_audio_fifo.invariant_failure_count;
            published.effective_sample_time = floor;
        }
        floor = published.effective_sample_time;
        g_control_audio_fifo_commands[(head + i)
            & (CONTROL_AUDIO_FIFO_CAPACITY - 1U)] = published;
    }
    __DMB();
    g_control_audio_fifo.head = head + count;
    return 1U;
}

uint8_t control_audio_fifo_publish(const control_audio_command_t *command)
{
    return control_audio_fifo_publish_batch(command, 1U);
}

uint8_t control_audio_fifo_publish_fenced(const control_audio_command_t *command,
                                          uint32_t *out_consumer_fence)
{
    if ((out_consumer_fence == NULL)
            || (control_audio_fifo_publish(command) == 0U))
        return 0U;
    __DMB();
    *out_consumer_fence = g_control_audio_fifo.head;
    return 1U;
}

uint8_t control_audio_fifo_control_fence_consumed(uint32_t consumer_fence)
{
    __DMB();
    return ((int32_t)(g_control_audio_fifo.tail - consumer_fence) >= 0)
        ? 1U : 0U;
}

uint8_t control_audio_fifo_audio_peek(control_audio_command_t *out_command)
{
    if ((out_command == NULL) || (g_control_audio_fifo.tail == g_control_audio_fifo.head))
        return 0U;
    __DMB();
    *out_command = g_control_audio_fifo_commands[g_control_audio_fifo.tail
        & (CONTROL_AUDIO_FIFO_CAPACITY - 1U)];
    return 1U;
}

uint8_t control_audio_fifo_audio_pop(void)
{
    const uint32_t tail = g_control_audio_fifo.tail;
    if (tail == g_control_audio_fifo.head) return 0U;
    g_control_audio_fifo.tail = tail + 1U;
    __DMB();
    return 1U;
}

uint32_t control_audio_fifo_audio_head_snapshot(void)
{
    const uint32_t head = g_control_audio_fifo.head;
    __DMB();
    return head;
}

uint8_t control_audio_fifo_audio_tail_before(uint32_t head_limit)
{
    return ((int32_t)(g_control_audio_fifo.tail - head_limit) < 0)
        ? 1U : 0U;
}

uint16_t control_audio_fifo_audio_frames_until_due(uint64_t sample_now,
                                                   uint16_t max_frames,
                                                   uint32_t head_limit)
{
    control_audio_command_t command;
    if ((max_frames == 0U)
            || (control_audio_fifo_audio_tail_before(head_limit) == 0U)
            || (control_audio_fifo_audio_peek(&command) == 0U))
        return max_frames;
    if (command.effective_sample_time <= sample_now) return 0U;
    const uint64_t distance = command.effective_sample_time - sample_now;
    return (distance < max_frames) ? (uint16_t)distance : max_frames;
}
