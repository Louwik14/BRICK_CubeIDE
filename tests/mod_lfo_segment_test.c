#include "Mod/mod_lfo_segment.h"
#include "Mod/mod_lfo_v1.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>

static uint32_t advance_in_slices(uint8_t shape,
                                  uint32_t phase,
                                  uint32_t inc,
                                  uint32_t total,
                                  uint32_t slice)
{
    const uint8_t split_policy = mod_lfo_segment_policy_from_shape(shape, 0U);
    while (total > 0U)
    {
        const uint32_t requested = (total < slice) ? total : slice;
        mod_lfo_ramp_t ramp;
        const uint32_t used = mod_lfo_segment_plan(shape,
                                                   phase,
                                                   inc,
                                                   requested,
                                                   0.25f,
                                                   split_policy,
                                                   &ramp);
        assert(used != 0U);
        phase = ramp.phase_after;
        total -= used;
    }
    return phase;
}

static void assert_close(float a, float b)
{
    assert(fabsf(a - b) < 0.00001f);
}

static void test_phase_is_slice_invariant(void)
{
    const uint32_t phase = 0x12345678U;
    const uint32_t inc = 0x01234567U;
    const uint32_t expected = phase + (uint32_t)((uint64_t)inc * 64ULL);

    assert(advance_in_slices(MOD_LFO_SHAPE_SINE, phase, inc, 64U, 64U) == expected);
    assert(advance_in_slices(MOD_LFO_SHAPE_SINE, phase, inc, 64U, 32U) == expected);
    assert(advance_in_slices(MOD_LFO_SHAPE_SINE, phase, inc, 64U, 16U) == expected);
    assert(advance_in_slices(MOD_LFO_SHAPE_SINE, phase, inc, 64U, 1U) == expected);
}

static void test_saw_wrap_is_a_boundary(void)
{
    mod_lfo_ramp_t ramp;
    const uint32_t used = mod_lfo_segment_plan(MOD_LFO_SHAPE_SAW,
                                               0xF0000000U,
                                               0x10000000U,
                                               4U,
                                               0.0f,
                                               mod_lfo_segment_policy_from_shape(MOD_LFO_SHAPE_SAW, 0U),
                                               &ramp);
    assert(used == 1U);
    assert(ramp.transition != 0U);
    assert_close(ramp.step, 0.0f);

    assert(mod_lfo_segment_plan(MOD_LFO_SHAPE_SAW,
                                ramp.phase_after,
                                0x10000000U,
                                3U,
                                0.0f,
                                mod_lfo_segment_policy_from_shape(MOD_LFO_SHAPE_SAW, 0U),
                                &ramp) == 3U);
    assert(ramp.start < 0.0f);
}

static void test_triangle_and_square_boundaries(void)
{
    mod_lfo_ramp_t ramp;
    assert(mod_lfo_segment_plan(MOD_LFO_SHAPE_TRIANGLE,
                                0x70000000U,
                                0x10000000U,
                                4U,
                                0.0f,
                                mod_lfo_segment_policy_from_shape(MOD_LFO_SHAPE_TRIANGLE, 0U),
                                &ramp) == 1U);
    assert(ramp.transition != 0U);

    assert(mod_lfo_segment_plan(MOD_LFO_SHAPE_SQUARE,
                                0x70000000U,
                                0x10000000U,
                                4U,
                                0.0f,
                                mod_lfo_segment_policy_from_shape(MOD_LFO_SHAPE_SQUARE, 0U),
                                &ramp) == 1U);
    assert(ramp.transition != 0U);
    assert_close(ramp.step, 0.0f);
}

static void test_random_hold_and_single_sample(void)
{
    mod_lfo_ramp_t ramp;
    assert(mod_lfo_segment_plan(MOD_LFO_SHAPE_RANDOM_SH,
                                0x10000000U,
                                0x01000000U,
                                8U,
                                0.37f,
                                mod_lfo_segment_policy_from_shape(MOD_LFO_SHAPE_RANDOM_SH, 0U),
                                &ramp) == 8U);
    assert_close(ramp.start, 0.37f);
    assert_close(ramp.step, 0.0f);

    assert(mod_lfo_segment_plan(MOD_LFO_SHAPE_SINE,
                                0x20000000U,
                                0x01000000U,
                                1U,
                                0.0f,
                                mod_lfo_segment_policy_from_shape(MOD_LFO_SHAPE_SINE, 0U),
                                &ramp) == 1U);
    assert_close(ramp.step, 0.0f);
}

static void test_one_shot_wrap(void)
{
    mod_lfo_ramp_t ramp;
    assert(mod_lfo_segment_plan(MOD_LFO_SHAPE_SINE,
                                0xF0000000U,
                                0x10000000U,
                                4U,
                                0.0f,
                                mod_lfo_segment_policy_from_shape(MOD_LFO_SHAPE_SINE, 1U),
                                &ramp) == 1U);
    assert(ramp.transition != 0U);
    assert(ramp.phase_after == 0U);
}

int main(void)
{
    test_phase_is_slice_invariant();
    test_saw_wrap_is_a_boundary();
    test_triangle_and_square_boundaries();
    test_random_hold_and_single_sample();
    test_one_shot_wrap();
    return 0;
}
