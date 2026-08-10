#include "Mod/mod_lfo_segment.h"

#include "Mod/mod_lfo_v1.h"

#include <math.h>

#define MOD_LFO_SINE_LUT_SIZE 256U

static const float g_mod_lfo_sine_lut[MOD_LFO_SINE_LUT_SIZE + 1U] = {
#include "mod_lfo_sine_lut_257.inc"
};

float mod_lfo_segment_wave(uint8_t shape, uint32_t phase, float sh_value)
{
    switch ((mod_lfo_shape_t)shape)
    {
        case MOD_LFO_SHAPE_SINE:
        case MOD_LFO_SHAPE_SINE_POS:
        {
            const uint32_t lut_pos = phase >> 24;
            const uint32_t frac = (phase >> 8) & 0xFFFFU;
            const float y0 = g_mod_lfo_sine_lut[lut_pos];
            const float y1 = g_mod_lfo_sine_lut[lut_pos + 1U];
            const float y = y0 + (y1 - y0) * ((float)frac * (1.0f / 65535.0f));
            return (shape == (uint8_t)MOD_LFO_SHAPE_SINE_POS) ? ((y + 1.0f) * 0.5f) : y;
        }

        case MOD_LFO_SHAPE_TRIANGLE:
        case MOD_LFO_SHAPE_TRIANGLE_POS:
        {
            const float p = (float)phase * (1.0f / 4294967296.0f);
            const float y = 1.0f - 4.0f * fabsf(p - 0.5f);
            return (shape == (uint8_t)MOD_LFO_SHAPE_TRIANGLE_POS) ? ((y + 1.0f) * 0.5f) : y;
        }

        case MOD_LFO_SHAPE_SAW:
            return ((float)phase * (2.0f / 4294967296.0f)) - 1.0f;

        case MOD_LFO_SHAPE_REVERSE_SAW:
            return 1.0f - ((float)phase * (2.0f / 4294967296.0f));

        case MOD_LFO_SHAPE_SQUARE:
        case MOD_LFO_SHAPE_SQUARE_POS:
            if (shape == (uint8_t)MOD_LFO_SHAPE_SQUARE_POS)
            {
                return (phase < 0x80000000U) ? 1.0f : 0.0f;
            }
            return (phase < 0x80000000U) ? 1.0f : -1.0f;

        case MOD_LFO_SHAPE_RANDOM_SH:
            return sh_value;

        default:
            return 0.0f;
    }
}

uint8_t mod_lfo_segment_policy_from_shape(uint8_t shape, uint8_t force_wrap)
{
    uint8_t policy = 0U;
    if ((force_wrap != 0U)
            || (shape == (uint8_t)MOD_LFO_SHAPE_SAW)
            || (shape == (uint8_t)MOD_LFO_SHAPE_REVERSE_SAW)
            || (shape == (uint8_t)MOD_LFO_SHAPE_RANDOM_SH))
    {
        policy |= MOD_LFO_SEGMENT_POLICY_WRAP;
    }

    if ((shape == (uint8_t)MOD_LFO_SHAPE_TRIANGLE)
            || (shape == (uint8_t)MOD_LFO_SHAPE_TRIANGLE_POS)
            || (shape == (uint8_t)MOD_LFO_SHAPE_SQUARE)
            || (shape == (uint8_t)MOD_LFO_SHAPE_SQUARE_POS))
    {
        policy |= (uint8_t)(MOD_LFO_SEGMENT_POLICY_SHAPE
                            | MOD_LFO_SEGMENT_POLICY_HALF);
    }

    return policy;
}

static uint64_t mod_lfo_segment_next_boundary(uint8_t split_policy, uint32_t phase)
{
    const uint64_t full = 0x100000000ULL;
    if ((split_policy & MOD_LFO_SEGMENT_POLICY_HALF) != 0U)
    {
        const uint64_t half = 0x80000000ULL;
        return (phase < 0x80000000U) ? (half - phase) : (full - phase);
    }
    return full - phase;
}

uint32_t mod_lfo_segment_plan(uint8_t shape,
                              uint32_t phase,
                              uint32_t phase_inc,
                              uint32_t requested_frames,
                              float sh_value,
                              uint8_t split_policy,
                              mod_lfo_ramp_t *ramp)
{
    if ((ramp == NULL) || (requested_frames == 0U))
    {
        return 0U;
    }

    uint32_t count = requested_frames;
    if (phase_inc != 0U)
    {
        if ((split_policy & (MOD_LFO_SEGMENT_POLICY_WRAP
                             | MOD_LFO_SEGMENT_POLICY_SHAPE)) != 0U)
        {
            const uint64_t distance = mod_lfo_segment_next_boundary(split_policy, phase);
            const uint64_t requested_distance = (uint64_t)phase_inc * (uint64_t)count;
            if (distance <= requested_distance)
            {
                const uint32_t samples = ((uint32_t)(distance - 1ULL) / phase_inc) + 1U;
                if (samples < count)
                {
                    count = samples;
                }
            }
        }
    }

    ramp->frames = count;
    ramp->phase_after = phase + (uint32_t)((uint64_t)phase_inc * (uint64_t)count);
    ramp->transition = (count < requested_frames) ? 1U : 0U;
    ramp->start = mod_lfo_segment_wave(shape, phase, sh_value);

    if (count <= 1U)
    {
        ramp->step = 0.0f;
        return count;
    }

    const uint32_t last_phase = phase + (uint32_t)((uint64_t)phase_inc * (uint64_t)(count - 1U));
    const float end = mod_lfo_segment_wave(shape, last_phase, sh_value);
    ramp->step = (end - ramp->start) / (float)(count - 1U);
    return count;
}
