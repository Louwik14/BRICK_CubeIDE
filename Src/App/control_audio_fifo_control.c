#include "IPC/control_audio_fifo_control.h"

#include "stm32h7xx.h"

#define FIFO g_control_audio_fifo_layout

void control_audio_fifo_control_init(void)
{
    FIFO.head = 0U;
    FIFO.overflow_count = 0U;
    FIFO.invariant_failure_count = 0U;
    FIFO.audio_sample_clock_sequence = 0U;
    FIFO.audio_sample_clock_low = 0U;
    FIFO.audio_sample_clock_high = 0U;
    FIFO.audio_sample_clock_valid = 0U;
    __DMB();
}

uint16_t control_audio_fifo_control_free(void)
{
    const uint32_t used = FIFO.head - FIFO.tail;
    return (used < CONTROL_AUDIO_FIFO_CAPACITY)
        ? (uint16_t)(CONTROL_AUDIO_FIFO_CAPACITY - used) : 0U;
}

uint8_t control_audio_fifo_control_audio_sample_now(
    uint64_t *out_sample_time)
{
    if (out_sample_time == NULL)
        return 0U;

    uint32_t sequence_start;
    uint32_t sequence_end;
    uint32_t low;
    uint32_t high;
    do
    {
        sequence_start = FIFO.audio_sample_clock_sequence;
        if ((sequence_start & 1U) != 0U)
            continue;
        low = FIFO.audio_sample_clock_low;
        high = FIFO.audio_sample_clock_high;
        __DMB();
        sequence_end = FIFO.audio_sample_clock_sequence;
    }
    while ((sequence_start != sequence_end)
        || ((sequence_end & 1U) != 0U));

    if (FIFO.audio_sample_clock_valid == 0U)
        return 0U;
    *out_sample_time = ((uint64_t)high << 32) | low;
    return 1U;
}

uint32_t control_audio_fifo_control_head_snapshot(void)
{
    const uint32_t head = FIFO.head;
    __DMB();
    return head;
}

uint8_t control_audio_fifo_control_head_consumed(uint32_t head)
{
    const uint32_t tail = FIFO.tail;
    __DMB();
    return ((int32_t)(tail - head) >= 0) ? 1U : 0U;
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
            || (writer->written >= writer->count) || (command == NULL)) return 0U;
    if (command->effective_sample_time < writer->floor)
    {
        /* Publication order is an invariant.  Never rewrite a producer's
         * timestamp to hide a regression; reject the whole batch instead. */
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
        { control_audio_fifo_batch_abort(&writer); return 0U; }
    return control_audio_fifo_batch_commit(&writer);
}
