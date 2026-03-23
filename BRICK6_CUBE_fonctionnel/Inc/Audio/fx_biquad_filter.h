#pragma once

#include <stdint.h>
#include "arm_math.h"

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
    arm_biquad_casd_df1_inst_f32 inst_l;
    arm_biquad_casd_df1_inst_f32 inst_r;

    __attribute__((aligned(32))) float coeffs[5U];
    float coeffs_pending[5U];
    __attribute__((aligned(32))) float state_l[4U];
    __attribute__((aligned(32))) float state_r[4U];

    float sample_rate;
    float cutoff_hz;
    float q;

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
