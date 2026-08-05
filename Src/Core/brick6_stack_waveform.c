#include "Core/brick6_stack_waveform.h"

static int16_t brick6_stack_waveform_sat16(int32_t value)
{
    if (value > 32767)
    {
        return 32767;
    }
    if (value < -32768)
    {
        return -32768;
    }
    return (int16_t)value;
}

static const int16_t k_stack_sine_quarter_q15[65] = {
       0,    804,   1608,   2410,
    3212,   4011,   4808,   5602,
    6393,   7179,   7962,   8739,
    9512,  10278,  11039,  11793,
   12539,  13279,  14010,  14732,
   15446,  16151,  16846,  17530,
   18204,  18868,  19519,  20159,
   20787,  21403,  22005,  22594,
   23170,  23731,  24279,  24811,
   25329,  25832,  26319,  26790,
   27245,  27683,  28105,  28510,
   28898,  29268,  29621,  29956,
   30273,  30571,  30852,  31113,
   31356,  31580,  31785,  31971,
   32137,  32285,  32412,  32521,
   32609,  32678,  32728,  32757,
   32767
};

static int16_t brick6_stack_waveform_sine_quarter(uint32_t phase)
{
    const uint32_t index = phase >> 24;
    const int32_t a = k_stack_sine_quarter_q15[index];
    const int32_t b = k_stack_sine_quarter_q15[index + 1U];
    const int32_t fraction = (int32_t)(phase & 0x00FFFFFFUL);
    return (int16_t)(a + (int32_t)((((int64_t)(b - a) * (int64_t)fraction) + 0x00800000LL) >> 24));
}

static int16_t brick6_stack_waveform_mix_q15(int16_t a, int16_t b, uint16_t balance_q15)
{
    if (balance_q15 == 0U)
    {
        return a;
    }
    if (balance_q15 >= 32767U)
    {
        return b;
    }
    const int32_t inv = 32767 - (int32_t)balance_q15;
    const int32_t mixed = (((int32_t)a * inv) + ((int32_t)b * (int32_t)balance_q15)) >> 15;
    return brick6_stack_waveform_sat16(mixed);
}

int16_t brick6_stack_waveform_saw(uint32_t phase)
{
    return (int16_t)((int32_t)(phase >> 16) - 32768);
}

int16_t brick6_stack_waveform_triangle(uint32_t phase)
{
    const uint32_t ramp = phase >> 16;
    const int32_t tri = (ramp < 32768U) ? (int32_t)ramp : (int32_t)(65535U - ramp);
    return (int16_t)((tri << 1) - 32768);
}

int16_t brick6_stack_waveform_sine(uint32_t phase)
{
    const uint32_t shifted = phase - 0x40000000UL;
    const uint32_t quadrant = shifted >> 30;
    const uint32_t quarter_phase = shifted & 0x3FFFFFFFUL;
    const uint32_t mirrored = 0x3FFFFFFFUL - quarter_phase;

    switch (quadrant)
    {
    case 0U:
        return brick6_stack_waveform_sine_quarter(quarter_phase);
    case 1U:
        return brick6_stack_waveform_sine_quarter(mirrored);
    case 2U:
        return (int16_t)-brick6_stack_waveform_sine_quarter(quarter_phase);
    default:
        return (int16_t)-brick6_stack_waveform_sine_quarter(mirrored);
    }
}

int16_t brick6_stack_waveform_pwm(uint32_t phase, uint16_t width_q15)
{
    uint32_t width = 8192U + (((uint32_t)width_q15 * 49152U) >> 15);
    if (width > 57344U)
    {
        width = 57344U;
    }
    return ((phase >> 16) < width) ? 32767 : -32768;
}

int16_t brick6_stack_waveform_wavefold(int16_t sample, uint16_t fold_q15, uint16_t sym_q15, uint16_t shape_q15)
{
    if (fold_q15 == 0U)
    {
        return sample;
    }

    const int32_t drive_q15 = 32767L + (int32_t)(((uint32_t)fold_q15 * 131068U) >> 15);
    const int32_t sym = (((int32_t)sym_q15 - 16384L) * (int32_t)fold_q15) >> 16;
    int32_t x = (((int32_t)sample * drive_q15) >> 15) + sym;

    for (uint8_t i = 0U; i < 4U; ++i)
    {
        if (x > 32767L)
        {
            x = 65534L - x;
        }
        else if (x < -32768L)
        {
            x = -65536L - x;
        }
        else
        {
            break;
        }
    }

    const int16_t folded = brick6_stack_waveform_sat16(x);
    const uint32_t rounded_phase = (uint32_t)(0x40000000LL + ((int64_t)folded << 17));
    const int16_t rounded = brick6_stack_waveform_sine(rounded_phase);
    const int16_t shaped = brick6_stack_waveform_mix_q15(folded, rounded, shape_q15);
    return brick6_stack_waveform_mix_q15(sample, shaped, fold_q15);
}

int16_t brick6_stack_waveform_shape(uint32_t phase, uint16_t shape_q15, uint16_t morph_q15)
{
    if (morph_q15 == 0U)
    {
        return brick6_stack_waveform_saw(phase);
    }
    if (morph_q15 >= 32767U)
    {
        return brick6_stack_waveform_pwm(phase, shape_q15);
    }
    return brick6_stack_waveform_mix_q15(brick6_stack_waveform_saw(phase),
                                         brick6_stack_waveform_pwm(phase, shape_q15),
                                         morph_q15);
}

static uint32_t brick6_stack_waveform_skew_phase(uint32_t phase, uint16_t skew_q15)
{
    const uint32_t phase_u16 = phase >> 16;
    const uint32_t bell_u16 = (phase_u16 * (65535U - phase_u16)) >> 16;
    const int32_t skew = (int32_t)skew_q15 - 16384L;
    int32_t warped = (int32_t)phase_u16 + (int32_t)(((int64_t)bell_u16 * (int64_t)skew * 3LL) >> 16);
    if (warped < 0)
    {
        warped = 0;
    }
    else if (warped > 65535L)
    {
        warped = 65535L;
    }
    return ((uint32_t)warped << 16) | (phase & 0xFFFFU);
}

static int16_t brick6_stack_waveform_full_rect(int16_t sample)
{
    if (sample == -32768)
    {
        return 32767;
    }
    return (sample < 0) ? (int16_t)-sample : sample;
}

static int16_t brick6_stack_waveform_half_rect(int16_t sample)
{
    return (sample < 0) ? 0 : sample;
}

static int16_t brick6_stack_waveform_sine_morph_target(uint8_t target,
                                                        uint32_t phase,
                                                        uint16_t asym_q15)
{
    const uint32_t warped_phase = brick6_stack_waveform_skew_phase(phase, asym_q15);
    const int16_t sine = brick6_stack_waveform_sine(warped_phase);
    switch (target)
    {
        case 0U:
            return brick6_stack_waveform_full_rect(sine);
        case 1U:
            return brick6_stack_waveform_half_rect(sine);
        case 2U:
            return brick6_stack_waveform_triangle(warped_phase);
        default:
            return brick6_stack_waveform_wavefold(brick6_stack_waveform_sine(phase),
                                                  32767U,
                                                  asym_q15,
                                                  16384U);
    }
}

int16_t brick6_stack_waveform_sine_morph(uint32_t phase,
                                         uint16_t morph_q15,
                                         uint16_t target_q15,
                                         uint16_t asym_q15)
{
    const int16_t base = brick6_stack_waveform_sine(phase);
    if (morph_q15 == 0U)
    {
        return base;
    }

    if (target_q15 >= 32767U)
    {
        const int16_t target = brick6_stack_waveform_sine_morph_target(3U, phase, asym_q15);
        return brick6_stack_waveform_mix_q15(base, target, morph_q15);
    }

    const uint32_t target_position = (uint32_t)target_q15 * 3U;
    const uint8_t target0 = (uint8_t)(target_position >> 15);
    const uint8_t target1 = (target0 < 3U) ? (uint8_t)(target0 + 1U) : 3U;
    const uint16_t target_fraction = (uint16_t)(target_position & 0x7FFFU);
    const int16_t wave0 = brick6_stack_waveform_sine_morph_target(target0, phase, asym_q15);
    if (target_fraction == 0U)
    {
        return brick6_stack_waveform_mix_q15(base, wave0, morph_q15);
    }
    const int16_t wave1 = (target1 == target0)
        ? wave0
        : brick6_stack_waveform_sine_morph_target(target1, phase, asym_q15);
    const int16_t target = brick6_stack_waveform_mix_q15(wave0, wave1, target_fraction);
    return brick6_stack_waveform_mix_q15(base, target, morph_q15);
}

static int16_t brick6_stack_waveform_tri_morph_target(uint8_t target,
                                                       uint32_t phase,
                                                       uint16_t skew_q15)
{
    const uint32_t warped_phase = brick6_stack_waveform_skew_phase(phase, skew_q15);
    switch (target)
    {
        case 0U:
            return brick6_stack_waveform_pwm(phase, skew_q15);
        case 1U:
            return brick6_stack_waveform_saw(warped_phase);
        default:
            return brick6_stack_waveform_pwm(warped_phase, 16384U);
    }
}

int16_t brick6_stack_waveform_tri_morph(uint32_t phase,
                                        uint16_t morph_q15,
                                        uint16_t target_q15,
                                        uint16_t skew_q15)
{
    const int16_t base = brick6_stack_waveform_triangle(phase);
    if (morph_q15 == 0U)
    {
        return base;
    }

    if (target_q15 >= 32767U)
    {
        const int16_t target = brick6_stack_waveform_tri_morph_target(2U, phase, skew_q15);
        return brick6_stack_waveform_mix_q15(base, target, morph_q15);
    }

    const uint32_t target_position = (uint32_t)target_q15 * 2U;
    const uint8_t target0 = (uint8_t)(target_position >> 15);
    const uint8_t target1 = (target0 < 2U) ? (uint8_t)(target0 + 1U) : 2U;
    const uint16_t target_fraction = (uint16_t)(target_position & 0x7FFFU);
    const int16_t wave0 = brick6_stack_waveform_tri_morph_target(target0, phase, skew_q15);
    if (target_fraction == 0U)
    {
        return brick6_stack_waveform_mix_q15(base, wave0, morph_q15);
    }
    const int16_t wave1 = (target1 == target0)
        ? wave0
        : brick6_stack_waveform_tri_morph_target(target1, phase, skew_q15);
    const int16_t target = brick6_stack_waveform_mix_q15(wave0, wave1, target_fraction);
    return brick6_stack_waveform_mix_q15(base, target, morph_q15);
}
