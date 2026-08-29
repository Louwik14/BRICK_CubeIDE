#include "Audio/brick6_stack_waveform.h"

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

int16_t brick6_stack_waveform_pwm(uint32_t phase, uint16_t width_q15)
{
    uint32_t width = 8192U + (((uint32_t)width_q15 * 49152U) >> 15);
    if (width > 57344U)
    {
        width = 57344U;
    }
    return ((phase >> 16) < width) ? 32767 : -32768;
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
