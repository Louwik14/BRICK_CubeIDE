/**
 * @file brick6_stack_braids_resources.cpp
 * @brief Narrow Stack accessors for immutable Braids tables.
 */

#include "Core/brick6_stack_braids_resources.h"

#include "braids/resources.h"

extern "C" int16_t brick6_stack_braids_wavetable_sample(uint8_t wave_index, uint32_t phase)
{
    const uint8_t *const table = braids::wt_waves + ((uint32_t)wave_index * 129U);
    const uint32_t index = phase >> 24;
    const int32_t a = table[index];
    const int32_t b = table[index + 1U];
    return (int16_t)((a << 8) + (((b - a) * (int32_t)(phase & 0x00FFFFFFUL)) >> 16) - 32768);
}
