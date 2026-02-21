#pragma once

#include "fx_biquad.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    fx_biquad_t low_l;
    fx_biquad_t low_r;
    fx_biquad_t mid_l;
    fx_biquad_t mid_r;
    fx_biquad_t high_l;
    fx_biquad_t high_r;
    float       sample_rate_hz;
    float       low_freq_hz;
    float       mid_freq_hz;
    float       mid_q;
    float       high_freq_hz;
    float       low_db;
    float       mid_db;
    float       high_db;
} fx_dj_eq3_t;

void fx_dj_eq3_init(fx_dj_eq3_t *eq,
                    float sample_rate_hz,
                    float low_freq_hz,
                    float mid_freq_hz,
                    float mid_q,
                    float high_freq_hz);

void fx_dj_eq3_reset(fx_dj_eq3_t *eq);
void fx_dj_eq3_set_low_db(fx_dj_eq3_t *eq, float gain_db);
void fx_dj_eq3_set_mid_db(fx_dj_eq3_t *eq, float gain_db);
void fx_dj_eq3_set_high_db(fx_dj_eq3_t *eq, float gain_db);

static inline void fx_dj_eq3_process_stereo_sample(fx_dj_eq3_t *eq, float *x_l, float *x_r)
{
    float y_l = *x_l;
    float y_r = *x_r;

    y_l = fx_biquad_process(&eq->low_l, y_l);
    y_l = fx_biquad_process(&eq->mid_l, y_l);
    y_l = fx_biquad_process(&eq->high_l, y_l);

    y_r = fx_biquad_process(&eq->low_r, y_r);
    y_r = fx_biquad_process(&eq->mid_r, y_r);
    y_r = fx_biquad_process(&eq->high_r, y_r);

    *x_l = y_l;
    *x_r = y_r;
}

#ifdef __cplusplus
}
#endif
