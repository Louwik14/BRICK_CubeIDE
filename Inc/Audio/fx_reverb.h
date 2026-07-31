#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void fx_reverb_global_init(float sample_rate);
void fx_reverb_global_set_model(uint8_t model);
void fx_reverb_global_set_wet(float wet);
void fx_reverb_global_set_size(float size);
void fx_reverb_global_set_decay(float decay);
void fx_reverb_global_set_damp(float damp);
void fx_reverb_global_set_predelay(float predelay_s);
void fx_reverb_global_set_hpf(float hpf);
void fx_reverb_global_set_lpf(float lpf);
void fx_reverb_global_set_smear(float smear);
void fx_reverb_global_set_digital_decay(float decay);
void fx_reverb_global_set_digital_damp(float damp);
void fx_reverb_global_set_digital_hpf(float hpf);
void fx_reverb_global_set_digital_lpf(float lpf);
uint8_t fx_reverb_global_is_active(void);
void fx_reverb_global_process_block(float *in_l,
                                    float *in_r,
                                    float *out_l,
                                    float *out_r,
                                    uint32_t frames);

#ifdef __cplusplus
}
#endif
