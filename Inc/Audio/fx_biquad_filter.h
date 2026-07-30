#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    FX_BIQUAD_FILTER_MODE_LP = 0,
    FX_BIQUAD_FILTER_MODE_HP,
    FX_BIQUAD_FILTER_MODE_BP
} fx_biquad_filter_mode_t;

typedef struct
{
    float a1;
    float a2;
    float a3;
    float k;
    float input_gain;
    float loop_saturation;
    float lp_gain;
    float hp_gain;
    float bp_gain;
} fx_biquad_filter_coeffs_t;

typedef struct {
    float sample_rate;
    float cutoff_hz;
    float q;
    fx_biquad_filter_coeffs_t current;
    float ic1eq_l;
    float ic2eq_l;
    float ic1eq_r;
    float ic2eq_r;
    uint16_t mode_xfade_remaining;
    uint16_t bypass_xfade_remaining;
    float bypass_mix;
    uint8_t mode;
    uint8_t previous_mode;
    uint8_t bypass;
    uint8_t reset_after_bypass;
    uint8_t mode_via_dry;
} fx_biquad_filter_t;

typedef struct {
    float sample_rate;
    float cutoff_hz;
    float q;
    fx_biquad_filter_coeffs_t current;
    float ic1eq;
    float ic2eq;
    uint16_t mode_xfade_remaining;
    uint16_t bypass_xfade_remaining;
    float bypass_mix;
    uint8_t mode;
    uint8_t previous_mode;
    uint8_t bypass;
    uint8_t reset_after_bypass;
    uint8_t mode_via_dry;
} fx_biquad_filter_mono_t;

void fx_biquad_filter_init(fx_biquad_filter_t *filter, float sample_rate);
void fx_biquad_filter_reset(fx_biquad_filter_t *filter);
void fx_biquad_filter_update_coeffs(fx_biquad_filter_t *filter);
void fx_biquad_filter_set_sample_rate(fx_biquad_filter_t *filter, float sample_rate);
void fx_biquad_filter_set_mode(fx_biquad_filter_t *filter, fx_biquad_filter_mode_t mode);
void fx_biquad_filter_set_params(fx_biquad_filter_t *filter, float cutoff_hz, float q);
void fx_biquad_filter_set_cutoff(fx_biquad_filter_t *filter, float cutoff_hz);
void fx_biquad_filter_set_q(fx_biquad_filter_t *filter, float q);
void fx_biquad_filter_set_bypass(fx_biquad_filter_t *filter, uint8_t bypass);
void fx_biquad_filter_process_block(fx_biquad_filter_t *filter,
                                    float *inout_l,
                                    float *inout_r,
                                    uint32_t block_size);

void fx_biquad_filter_mono_init(fx_biquad_filter_mono_t *filter, float sample_rate);
void fx_biquad_filter_mono_reset(fx_biquad_filter_mono_t *filter);
void fx_biquad_filter_mono_update_coeffs(fx_biquad_filter_mono_t *filter);
void fx_biquad_filter_mono_set_sample_rate(fx_biquad_filter_mono_t *filter, float sample_rate);
void fx_biquad_filter_mono_set_mode(fx_biquad_filter_mono_t *filter, fx_biquad_filter_mode_t mode);
void fx_biquad_filter_mono_set_params(fx_biquad_filter_mono_t *filter, float cutoff_hz, float q);
void fx_biquad_filter_mono_set_cutoff(fx_biquad_filter_mono_t *filter, float cutoff_hz);
void fx_biquad_filter_mono_set_q(fx_biquad_filter_mono_t *filter, float q);
void fx_biquad_filter_mono_set_bypass(fx_biquad_filter_mono_t *filter, uint8_t bypass);
void fx_biquad_filter_mono_process_block(fx_biquad_filter_mono_t *filter,
                                         float *inout,
                                         uint32_t block_size);

#ifdef __cplusplus
}
#endif
