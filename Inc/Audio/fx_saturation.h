#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float k;     // UI drive normalized 0..1
    float tone;  // coefficient low-pass post-shaper
    float asym;  // 1.0 is symmetric; 0.5..1.5 biases negative/positive drive

    float pre_gain;   // TRX BD driveGain = 1 + drive * 5
    float post_gain;

    float mix;
    float dry;

    float prev_l;
    float prev_r;

    float decimator_rate;
    uint8_t decimator_bits_to_crush;
    uint8_t decimator_inc_l;
    uint8_t decimator_inc_r;
    uint8_t decimator_threshold;
    float decimator_downsampled_l;
    float decimator_downsampled_r;
    uint8_t decimator_enabled;
    uint8_t decimator_rate2_enabled;
    float decimator_rate2_frequency;
    float decimator_rate2_phase_l;
    float decimator_rate2_sample_l;
    float decimator_rate2_next_sample_l;
    float decimator_rate2_previous_sample_l;
    float decimator_rate2_phase_r;
    float decimator_rate2_sample_r;
    float decimator_rate2_next_sample_r;
    float decimator_rate2_previous_sample_r;
    uint8_t bypass;

} fx_saturation_t;

void fx_saturation_init(fx_saturation_t *fx);
void fx_saturation_set_drive(fx_saturation_t *fx, float drive);
void fx_saturation_set_decimator_bits_ui(fx_saturation_t *fx, uint8_t bits_0_127);
void fx_saturation_set_decimator_rate_ui(fx_saturation_t *fx, uint8_t rate_0_127);
void fx_saturation_set_decimator_rate2_ui(fx_saturation_t *fx, uint8_t rate_0_127);
void fx_saturation_set_mix(fx_saturation_t *fx, float mix);
void fx_saturation_set_tone(fx_saturation_t *fx, float tone);
void fx_saturation_set_bias(fx_saturation_t *fx, float bias);
void fx_saturation_process_block(fx_saturation_t *fx,
                                 float *inout_l,
                                 float *inout_r,
                                 uint32_t frames);
void fx_saturation_process_mono_block(fx_saturation_t *fx,
                                      float *inout,
                                      uint32_t frames);

#ifdef __cplusplus
}
#endif
