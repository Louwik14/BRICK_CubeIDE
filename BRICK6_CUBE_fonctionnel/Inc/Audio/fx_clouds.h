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
void fx_clouds_set_texture(float texture_0_1);
void fx_clouds_set_dry_wet(float dry_wet_0_1);
void fx_clouds_set_feedback(float feedback_0_1);
void fx_clouds_set_stereo_spread(float stereo_spread_0_1);
void fx_clouds_set_freeze(uint8_t freeze);

#ifdef __cplusplus
}
#endif
