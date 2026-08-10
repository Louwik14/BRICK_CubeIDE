#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A ramp describes the samples [0, frames).  start is the value of sample
 * zero and sample n is start + step * n.  The phase_after field is the exact
 * state to use for the following ramp; it is deliberately not reconstructed
 * from the floating-point ramp.
 */
typedef struct
{
    float start;
    float step;
    uint32_t frames;
    uint32_t phase_after;
    uint8_t transition;
} mod_lfo_ramp_t;

float mod_lfo_segment_wave(uint8_t shape, uint32_t phase, float sh_value);

#define MOD_LFO_SEGMENT_POLICY_WRAP  (1U << 0)
#define MOD_LFO_SEGMENT_POLICY_SHAPE (1U << 1)
#define MOD_LFO_SEGMENT_POLICY_HALF  (1U << 2)

uint8_t mod_lfo_segment_policy_from_shape(uint8_t shape, uint8_t force_wrap);

/*
 * Plan one bounded piece of a trajectory.  The returned length is in
 * ramp->frames and is never greater than requested_frames.  A short result
 * marks a transition that must be handled before the caller continues.
 */
uint32_t mod_lfo_segment_plan(uint8_t shape,
                              uint32_t phase,
                              uint32_t phase_inc,
                              uint32_t requested_frames,
                              float sh_value,
                              uint8_t split_policy,
                              mod_lfo_ramp_t *ramp);

#ifdef __cplusplus
}
#endif
