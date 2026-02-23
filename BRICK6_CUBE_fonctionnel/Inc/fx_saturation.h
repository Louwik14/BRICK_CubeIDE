#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float k;
    float tone;
    float asym;
    float makeup;
    float mix;
    float dry;
    float prev_l;
    float prev_r;
    uint8_t bypass;
} fx_saturation_t;

void fx_saturation_init(fx_saturation_t *fx);
void fx_saturation_set_drive_ui(fx_saturation_t *fx, uint8_t drive_0_127);
void fx_saturation_set_mix_ui(fx_saturation_t *fx, uint8_t mix_0_127);
void fx_saturation_set_tone_ui(fx_saturation_t *fx, uint8_t tone_0_127);
void fx_saturation_set_bias_ui(fx_saturation_t *fx, uint8_t bias_0_127);
void fx_saturation_process_block(fx_saturation_t *fx,
                                 float *inout_l,
                                 float *inout_r,
                                 uint32_t frames);

#ifdef __cplusplus
}
#endif
