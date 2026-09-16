#ifndef AUDIO_COMMAND_EXECUTOR_H
#define AUDIO_COMMAND_EXECUTOR_H

#include <stdint.h>

void audio_command_executor_init(void);
uint16_t __attribute__((noinline)) audio_command_executor_apply_due(
    uint64_t sample_time, uint32_t head_limit,
    uint64_t discard_transient_before);
uint16_t audio_command_executor_apply_seq_rt_due(uint64_t sample_time);
void audio_command_executor_seq_rt_begin_block(uint16_t track_mask);

#endif
