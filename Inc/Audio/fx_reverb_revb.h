#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fx_reverb_revb_t fx_reverb_revb_t;

void fx_reverb_revb_global_init(float sample_rate);
void fx_reverb_revb_global_set_wet(float wet);
void fx_reverb_revb_global_set_room_size(float room_size);
void fx_reverb_revb_global_set_damping(float damping);
void fx_reverb_revb_global_set_width(float width);
void fx_reverb_revb_global_set_hpf(float hpf);
void fx_reverb_revb_global_set_lpf(float lpf);
void fx_reverb_revb_global_process_send_mono_to_stereo_wet(const float *in,
                                                            float *out_l,
                                                            float *out_r,
                                                            uint32_t frames);

#ifdef __cplusplus
}
#endif
