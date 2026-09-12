#include "IPC/control_audio_fifo_audio.h"

#include <stddef.h>
#include "stm32h7xx.h"

#define FIFO g_control_audio_fifo_layout

void control_audio_fifo_audio_init(void)
{
    FIFO.tail = 0U;
    __DMB();
}

uint8_t control_audio_fifo_audio_peek(control_audio_command_t *out_command)
{
    if ((out_command == NULL) || (FIFO.tail == FIFO.head)) return 0U;
    __DMB();
    *out_command = g_control_audio_fifo_commands[FIFO.tail
        & (CONTROL_AUDIO_FIFO_CAPACITY - 1U)];
    return 1U;
}

uint8_t control_audio_fifo_audio_pop(void)
{
    const uint32_t tail = FIFO.tail;
    if (tail == FIFO.head) return 0U;
    FIFO.tail = tail + 1U; __DMB(); return 1U;
}

uint32_t control_audio_fifo_audio_head_snapshot(void)
{ const uint32_t head = FIFO.head; __DMB(); return head; }

uint8_t control_audio_fifo_audio_tail_before(uint32_t head_limit)
{ return ((int32_t)(FIFO.tail - head_limit) < 0) ? 1U : 0U; }

uint16_t control_audio_fifo_audio_frames_until_due(uint64_t sample_now,
                                                   uint16_t max_frames,
                                                   uint32_t head_limit)
{
    control_audio_command_t command;
    if ((max_frames == 0U) || (control_audio_fifo_audio_tail_before(head_limit) == 0U)
            || (control_audio_fifo_audio_peek(&command) == 0U)) return max_frames;
    if (command.effective_sample_time <= sample_now) return 0U;
    const uint64_t distance = command.effective_sample_time - sample_now;
    return (distance < max_frames) ? (uint16_t)distance : max_frames;
}
