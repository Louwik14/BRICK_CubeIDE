#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fx_reverb_drumboy_t fx_reverb_drumboy_t;

fx_reverb_drumboy_t *fx_reverb_drumboy_get_instance(void);

void fx_reverb_drumboy_init(fx_reverb_drumboy_t *rev, float sample_rate);
void fx_reverb_drumboy_reset(fx_reverb_drumboy_t *rev);

void fx_reverb_drumboy_set_size(fx_reverb_drumboy_t *rev, float size_0_1);
void fx_reverb_drumboy_set_decay(fx_reverb_drumboy_t *rev, float decay_0_1);
void fx_reverb_drumboy_set_predelay(fx_reverb_drumboy_t *rev, float predelay_s);
void fx_reverb_drumboy_set_surround(fx_reverb_drumboy_t *rev, float surround_s);
void fx_reverb_drumboy_set_wet(fx_reverb_drumboy_t *rev, float wet_0_1);
void fx_reverb_drumboy_set_bypass(fx_reverb_drumboy_t *rev, uint8_t bypass);

void fx_reverb_drumboy_process_block(fx_reverb_drumboy_t *rev,
                                     const float *in_l,
                                     const float *in_r,
                                     float *out_l,
                                     float *out_r,
                                     uint32_t frames);

#ifdef __cplusplus
}
#endif
