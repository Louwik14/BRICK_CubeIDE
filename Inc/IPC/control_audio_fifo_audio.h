#pragma once

#include <stdint.h>
#include "IPC/control_audio_fifo_layout.h"

void control_audio_fifo_audio_init(void);
uint8_t control_audio_fifo_audio_peek(control_audio_command_t *out_command);
uint8_t control_audio_fifo_audio_pop(void);
uint32_t control_audio_fifo_audio_head_snapshot(void);
uint8_t control_audio_fifo_audio_tail_before(uint32_t head_limit);
uint16_t control_audio_fifo_audio_frames_until_due(uint64_t sample_now,
                                                   uint16_t max_frames,
                                                   uint32_t head_limit);
