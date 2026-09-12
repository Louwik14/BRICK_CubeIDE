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
 *
 * H747 port contract: M7 alone calls brick_media_clock_init() and owns the
 * TIM5 update IRQ.  Define BRICK_MEDIA_CLOCK_SHARED_STATE_ADDRESS to the same
 * non-cacheable/coherent shared-SRAM address in both images; M4 only calls the
 * read/conversion API.  The shared seqlock state replaces neither TIM5 nor its
 * ticks with an anchor or mailbox.
 */
void brick_media_clock_init(void);
void brick_media_clock_on_tim5_update_irq(void);
uint32_t brick_media_clock_now_tick(void);
bool brick_media_clock_tick_to_sample(uint32_t capture_tick,
                                      uint64_t *out_sample_time);
bool brick_media_clock_tick_to_guarded_sample(uint32_t capture_tick,
                                              uint64_t *out_sample_time);
bool brick_media_clock_now_sample(uint64_t *out_sample_time);
uint32_t brick_media_clock_tick_hz(void);
