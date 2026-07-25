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
    const int32_t tri = brick6_stack_waveform_triangle(phase);
    const int32_t x = (tri < 0) ? -tri : tri;
    return brick6_stack_waveform_sat16((tri * (49152 - x)) >> 14);
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
