#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fx_granular_state fx_granular_state_t;

size_t fx_granular_state_size(void);
size_t fx_granular_buffer_size(void);

void fx_granular_init(fx_granular_state_t* state,
                      float sample_rate,
                      float* buffer_l,
                      float* buffer_r,
                      uint32_t buffer_frames);
void fx_granular_process_block(fx_granular_state_t* state,
                               float* in_l,
                               float* in_r,
                               float* out_l,
                               float* out_r,
                               uint32_t frames);

void fx_granular_set_density(fx_granular_state_t* state, float density_0_1);
void fx_granular_set_pitch(fx_granular_state_t* state, float semitones_m48_p48);
void fx_granular_set_freeze(fx_granular_state_t* state, bool freeze);
void fx_granular_set_mix(fx_granular_state_t* state, float mix_0_1);
void fx_granular_set_spread(fx_granular_state_t* state, float spread_0_1);
void fx_granular_set_stereo_offset(fx_granular_state_t* state, float amount_0_1);

#ifdef __cplusplus
}
#endif
