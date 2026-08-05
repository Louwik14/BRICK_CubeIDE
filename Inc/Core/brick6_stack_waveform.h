#ifndef BRICK6_STACK_WAVEFORM_H
#define BRICK6_STACK_WAVEFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t brick6_stack_waveform_saw(uint32_t phase);
int16_t brick6_stack_waveform_triangle(uint32_t phase);
int16_t brick6_stack_waveform_sine(uint32_t phase);
int16_t brick6_stack_waveform_pwm(uint32_t phase, uint16_t width_q15);
int16_t brick6_stack_waveform_wavefold(int16_t sample, uint16_t fold_q15, uint16_t sym_q15, uint16_t shape_q15);
int16_t brick6_stack_waveform_shape(uint32_t phase, uint16_t shape_q15, uint16_t morph_q15);
int16_t brick6_stack_waveform_sine_morph(uint32_t phase,
                                         uint16_t morph_q15,
                                         uint16_t target_q15,
                                         uint16_t asym_q15);
int16_t brick6_stack_waveform_tri_morph(uint32_t phase,
                                        uint16_t morph_q15,
                                        uint16_t target_q15,
                                        uint16_t skew_q15);

#ifdef __cplusplus
}
#endif

#endif /* BRICK6_STACK_WAVEFORM_H */
