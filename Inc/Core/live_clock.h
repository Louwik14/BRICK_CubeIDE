#ifndef BRICK6_LIVE_CLOCK_H
#define BRICK6_LIVE_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The capture clock is TIM5. The audio clock remains the absolute sample
 * timeline owned by the SAI audio IRQ. Producers must never read that
 * timeline directly to timestamp an input event.
 */
typedef struct
{
    uint32_t tim5_tick;
    uint64_t audio_sample;
} live_clock_anchor_t;

void live_clock_init(void);

/*
 * Publish an anchor at the beginning of an SAI half callback, before the
 * corresponding half is rendered. audio_sample is therefore the first
 * sample of the TX half that the audio owner is about to write; it is not
 * the sample currently leaving the codec.
 */
void live_clock_audio_publish_anchor(uint64_t audio_sample);

/* Read a coherent snapshot of the latest audio-owned anchor. */
bool live_clock_read_anchor(live_clock_anchor_t *out_anchor);

/* Convert a TIM5 capture tick to the absolute audio sample timeline. */
bool live_clock_tim5_to_sample_time(uint32_t capture_tick,
                                    uint64_t *out_sample_time);

/* Capture the acquisition clock without exposing timer ownership to producers. */
uint32_t live_clock_capture_tick(void);

uint32_t live_clock_get_tim5_hz(void);

#endif
