#ifndef AUDIO_COMMAND_EXECUTOR_H
#define AUDIO_COMMAND_EXECUTOR_H

#include <stdint.h>

void audio_command_executor_init(void);
uint16_t __attribute__((noinline)) audio_command_executor_apply_due(
    uint64_t sample_time, uint32_t head_limit);

#endif
