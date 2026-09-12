#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "Board/board_audio_format.h"

#define BRICK_MEDIA_CLOCK_GUARD_SAMPLES BOARD_AUDIO_FRAMES_PER_HALF

/*
 * Canonical BRICK media clock.
 *
 * Board init owns TIM5 configuration and start.  Every domain is a reader of
 * the same free-running counter; no AUDIO callback counter participates in
 * media timestamps.  The platform implementation owns the sole logical
 * 32-to-64-bit extension.
 */
void brick_media_clock_init(void);
uint32_t brick_media_clock_now_tick(void);
bool brick_media_clock_tick_to_sample(uint32_t capture_tick,
                                      uint64_t *out_sample_time);
bool brick_media_clock_tick_to_guarded_sample(uint32_t capture_tick,
                                              uint64_t *out_sample_time);
bool brick_media_clock_now_sample(uint64_t *out_sample_time);
uint32_t brick_media_clock_tick_hz(void);
