#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t fx_granular_state_size(void);
void fx_granular_init_state(void *mem, float sample_rate);
void fx_granular_process_state(void *mem,
                               float *inout_l,
                               float *inout_r,
                               uint32_t frames);

/* Compat API (single active granular state) */
void fx_granular_init(float sample_rate);
void fx_granular_process_block(float* in_l, float* in_r,
                               float* out_l, float* out_r,
                               uint32_t frames);

void fx_granular_set_density(float density_0_1);
void fx_granular_set_pitch(float semitones_m48_p48);
void fx_granular_set_freeze(bool freeze);
void fx_granular_set_mix(float mix_0_1);
void fx_granular_set_spread(float spread_0_1);
void fx_granular_set_stereo_offset(float amount_0_1);

#ifdef __cplusplus
}
#endif
