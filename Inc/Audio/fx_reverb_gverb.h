#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void fx_reverb_gverb_global_init(float sample_rate);
void fx_reverb_gverb_global_reset(void);
void fx_reverb_gverb_global_set_wet(float wet);
void fx_reverb_gverb_global_set_size(float size);
void fx_reverb_gverb_global_set_decay(float decay);
void fx_reverb_gverb_global_set_lpf(float lpf);
void fx_reverb_gverb_global_process_send_mono_to_stereo_wet(const float *in,
                                                             float *out_l,
                                                             float *out_r,
                                                             uint32_t frames);

#ifdef __cplusplus
}
#endif
