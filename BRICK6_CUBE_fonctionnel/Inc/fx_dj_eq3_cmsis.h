#pragma once

#include <stdint.h>
#include "arm_math.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    arm_biquad_casd_df1_inst_f32 inst_l;
    arm_biquad_casd_df1_inst_f32 inst_r;

    __attribute__((aligned(32))) float coeffs[3U * 5U];
    float coeffs_pending[3U * 5U];
    __attribute__((aligned(32))) float state_l[3U * 4U];
    __attribute__((aligned(32))) float state_r[3U * 4U];

    float sample_rate;
    float low_freq;
    float mid_freq;
    float high_freq;
    float mid_q;

    float low_db;
    float mid_db;
    float high_db;

    uint8_t bypass;
    volatile uint8_t coeffs_pending_update;
} fx_dj_eq3_t;

void fx_dj_eq3_init(fx_dj_eq3_t *eq,
                    float sample_rate,
                    float low_freq,
                    float mid_freq,
                    float mid_q,
                    float high_freq);

void fx_dj_eq3_reset(fx_dj_eq3_t *eq);
void fx_dj_eq3_update_coeffs(fx_dj_eq3_t *eq);
void fx_dj_eq3_set_low_db(fx_dj_eq3_t *eq, float gain_db);
void fx_dj_eq3_set_mid_db(fx_dj_eq3_t *eq, float gain_db);
void fx_dj_eq3_set_high_db(fx_dj_eq3_t *eq, float gain_db);
void fx_dj_eq3_set_bypass(fx_dj_eq3_t *eq, uint8_t bypass);

void fx_dj_eq3_process_block(fx_dj_eq3_t *eq,
                             float *inout_l,
                             float *inout_r,
                             uint32_t block_size);

#ifdef __cplusplus
}
#endif
