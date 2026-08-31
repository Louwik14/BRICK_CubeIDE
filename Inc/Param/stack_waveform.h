#pragma once

#include <stdint.h>

int16_t brick6_stack_waveform_saw(uint32_t phase);
int16_t brick6_stack_waveform_pwm(uint32_t phase, uint16_t width_q15);
int16_t brick6_stack_waveform_shape(uint32_t phase, uint16_t shape_q15,
                                    uint16_t morph_q15);
