#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void fx_clouds_init(float sample_rate);
void fx_clouds_process_block(float *in_l, float *in_r,
                             float *out_l, float *out_r,
                             uint32_t frames);

void fx_clouds_set_position(float position_0_1);
void fx_clouds_set_size(float size_0_1);
void fx_clouds_set_pitch(float pitch);
void fx_clouds_set_density(float density_0_1);

#ifdef __cplusplus
}
#endif
