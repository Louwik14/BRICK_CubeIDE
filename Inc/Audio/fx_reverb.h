#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void fx_reverb_global_init(float sample_rate);
void fx_reverb_global_set_wet(float wet);
void fx_reverb_global_set_size(float size);
void fx_reverb_global_set_decay(float decay);
void fx_reverb_global_set_predelay(float predelay_s);
void fx_reverb_global_set_lpf(float lpf);
uint8_t fx_reverb_global_is_active(void);
void fx_reverb_global_process_block(float *in_l,
                                    float *in_r,
                                    float *out_l,
                                    float *out_r,
                                    uint32_t frames);

#ifdef __cplusplus
}
#endif
