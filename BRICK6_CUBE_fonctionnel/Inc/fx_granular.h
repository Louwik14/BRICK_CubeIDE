#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void fx_granular_init(float sample_rate);
void fx_granular_process_block(float* in_l, float* in_r,
                               float* out_l, float* out_r,
                               uint32_t frames);

void fx_granular_set_density(float density_0_1);
void fx_granular_set_pitch(float semitones_m48_p48);
void fx_granular_set_freeze(bool freeze);
void fx_granular_set_mix(float mix_0_1);

#ifdef __cplusplus
}
#endif
