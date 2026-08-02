#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void fx_delay_stereo_global_init(float sample_rate);
void fx_delay_stereo_global_clear(void);
void fx_delay_stereo_global_set_time(float time_s);
void fx_delay_stereo_global_set_feedback(float feedback);
void fx_delay_stereo_global_set_filter_hz(float low_cut_hz, float high_cut_hz);
void fx_delay_stereo_global_set_pingpong(uint8_t enabled);
void fx_delay_stereo_global_set_width(float width);
void fx_delay_stereo_global_set_reverb_send(float reverb_send);
void fx_delay_stereo_global_set_volume(float volume);
uint8_t fx_delay_stereo_global_is_active(void);
void fx_delay_stereo_global_process_block(const float *in_l,
                                          const float *in_r,
                                          float *out_l,
                                          float *out_r,
                                          float *rev_l,
                                          float *rev_r,
                                          uint32_t frames);

#ifdef __cplusplus
}
#endif
