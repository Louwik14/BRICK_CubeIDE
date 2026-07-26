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
    return (int16_t)(a + (((b - a) * fraction + 0x00800000L) >> 24));
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

static int16_t brick6_stack_waveform_mul_q15(int16_t sample, uint16_t gain_q15)
{
    return brick6_stack_waveform_sat16(((int32_t)sample * (int32_t)gain_q15) >> 15);
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

int16_t brick6_stack_waveform_fold(int16_t sample, uint16_t amount_q15)
{
    if (amount_q15 == 0U)
    {
        return sample;
    }

    int32_t x = (int32_t)sample * (int32_t)(32767U + (amount_q15 << 2));
    x >>= 15;
    while ((x > 32767) || (x < -32768))
    {
        if (x > 32767)
        {
            x = 65534 - x;
        }
        else
        {
            x = -65536 - x;
        }
    }
    return (int16_t)x;
}

int16_t brick6_stack_waveform_soft(uint32_t phase, uint16_t morph_q15, uint16_t fold_q15)
{
    int16_t sample = 0;
    if (morph_q15 == 0U)
    {
        sample = brick6_stack_waveform_sine(phase);
    }
    else if (morph_q15 >= 32767U)
    {
        sample = brick6_stack_waveform_triangle(phase);
    }
    else
    {
        sample = brick6_stack_waveform_mix_q15(brick6_stack_waveform_sine(phase),
                                               brick6_stack_waveform_triangle(phase),
                                               morph_q15);
    }
    return brick6_stack_waveform_fold(sample, fold_q15);
}

int16_t brick6_stack_waveform_shape(uint32_t phase, uint16_t shape_q15, uint16_t morph_q15)
{
    const uint16_t square_gain_q15 = 18944U; /* Braids SawSquare: square * 148 / 256. */
    if (morph_q15 == 0U)
    {
        return brick6_stack_waveform_saw(phase);
    }
    if (morph_q15 >= 32767U)
    {
        return brick6_stack_waveform_mul_q15(brick6_stack_waveform_pwm(phase, shape_q15),
                                             square_gain_q15);
    }
    return brick6_stack_waveform_mix_q15(brick6_stack_waveform_saw(phase),
                                         brick6_stack_waveform_mul_q15(brick6_stack_waveform_pwm(phase, shape_q15),
                                                                      square_gain_q15),
                                         morph_q15);
}
