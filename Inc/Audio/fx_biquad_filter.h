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

typedef struct {
    float sample_rate;
    float cutoff_hz;
    float q;

    int16_t frequency_q15;
    int16_t resonance_q15;
    int32_t f_q15;
    int32_t damp_q15;

    int32_t lp_l;
    int32_t bp_l;
    int32_t lp_r;
    int32_t bp_r;

    uint8_t mode;
    uint8_t bypass;
    volatile uint8_t coeffs_pending_update;
} fx_biquad_filter_t;

void fx_biquad_filter_init(fx_biquad_filter_t *filter, float sample_rate);
void fx_biquad_filter_reset(fx_biquad_filter_t *filter);
void fx_biquad_filter_update_coeffs(fx_biquad_filter_t *filter);
void fx_biquad_filter_set_sample_rate(fx_biquad_filter_t *filter, float sample_rate);
void fx_biquad_filter_set_mode(fx_biquad_filter_t *filter, fx_biquad_filter_mode_t mode);
void fx_biquad_filter_set_cutoff(fx_biquad_filter_t *filter, float cutoff_hz);
void fx_biquad_filter_set_q(fx_biquad_filter_t *filter, float q);
void fx_biquad_filter_set_bypass(fx_biquad_filter_t *filter, uint8_t bypass);
void fx_biquad_filter_process_block(fx_biquad_filter_t *filter,
                                    float *inout_l,
                                    float *inout_r,
                                    uint32_t block_size);

#ifdef __cplusplus
}
#endif
