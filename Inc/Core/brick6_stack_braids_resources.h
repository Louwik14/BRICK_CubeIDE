/**
 * @file brick6_stack_braids_resources.h
 * @brief Narrow C bridge to Braids resources used by Stack.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_STACK_BRAIDS_WAVETABLE_COUNT 256U
#define BRICK6_STACK_BRAIDS_WAVETABLE_BANK_COUNT 16U
#define BRICK6_STACK_BRAIDS_WAVETABLE_BANK_SIZE 16U

int16_t brick6_stack_braids_wavetable_sample(uint8_t wave_index, uint32_t phase);

#ifdef __cplusplus
}
#endif
