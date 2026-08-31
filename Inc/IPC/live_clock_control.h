#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "Board/board_audio_format.h"

#define LIVE_GUARD_SAMPLES BOARD_AUDIO_FRAMES_PER_HALF
void live_clock_control_init(void);
bool live_clock_read_audio_sample(uint64_t *out_audio_sample);
uint64_t live_clock_control_sample(void);
bool live_clock_tim5_to_sample_time(uint32_t capture_tick, uint64_t *out_sample_time);
bool live_clock_tim5_to_guarded_sample_time(uint32_t capture_tick, uint64_t *out_sample_time);
uint32_t live_clock_capture_tick(void);
uint32_t live_clock_get_tim5_hz(void);
