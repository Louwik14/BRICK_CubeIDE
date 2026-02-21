#include "fx_dj_eq3.h"

#define FX_DJ_EQ3_SHELF_SLOPE 1.0f
#define FX_DJ_EQ3_MIN_DB     -60.0f
#define FX_DJ_EQ3_MAX_DB      12.0f

static float fx_dj_eq3_clamp_db(float gain_db)
{
    if(gain_db < FX_DJ_EQ3_MIN_DB)
        gain_db = FX_DJ_EQ3_MIN_DB;
    if(gain_db > FX_DJ_EQ3_MAX_DB)
        gain_db = FX_DJ_EQ3_MAX_DB;
    return gain_db;
}

void fx_dj_eq3_reset(fx_dj_eq3_t *eq)
{
    if(!eq)
        return;

    fx_biquad_reset(&eq->low_l);
    fx_biquad_reset(&eq->low_r);
    fx_biquad_reset(&eq->mid_l);
    fx_biquad_reset(&eq->mid_r);
    fx_biquad_reset(&eq->high_l);
    fx_biquad_reset(&eq->high_r);
}

void fx_dj_eq3_set_low_db(fx_dj_eq3_t *eq, float gain_db)
{
    if(!eq)
        return;

    eq->low_db = fx_dj_eq3_clamp_db(gain_db);
    fx_biquad_set_low_shelf(&eq->low_l,
                            eq->sample_rate_hz,
                            eq->low_freq_hz,
                            eq->low_db,
                            FX_DJ_EQ3_SHELF_SLOPE);
    fx_biquad_set_low_shelf(&eq->low_r,
                            eq->sample_rate_hz,
                            eq->low_freq_hz,
                            eq->low_db,
                            FX_DJ_EQ3_SHELF_SLOPE);
}

void fx_dj_eq3_set_mid_db(fx_dj_eq3_t *eq, float gain_db)
{
    if(!eq)
        return;

    eq->mid_db = fx_dj_eq3_clamp_db(gain_db);
    fx_biquad_set_peaking(&eq->mid_l, eq->sample_rate_hz, eq->mid_freq_hz, eq->mid_db, eq->mid_q);
    fx_biquad_set_peaking(&eq->mid_r, eq->sample_rate_hz, eq->mid_freq_hz, eq->mid_db, eq->mid_q);
}

void fx_dj_eq3_set_high_db(fx_dj_eq3_t *eq, float gain_db)
{
    if(!eq)
        return;

    eq->high_db = fx_dj_eq3_clamp_db(gain_db);
    fx_biquad_set_high_shelf(&eq->high_l,
                             eq->sample_rate_hz,
                             eq->high_freq_hz,
                             eq->high_db,
                             FX_DJ_EQ3_SHELF_SLOPE);
    fx_biquad_set_high_shelf(&eq->high_r,
                             eq->sample_rate_hz,
                             eq->high_freq_hz,
                             eq->high_db,
                             FX_DJ_EQ3_SHELF_SLOPE);
}

void fx_dj_eq3_init(fx_dj_eq3_t *eq,
                    float sample_rate_hz,
                    float low_freq_hz,
                    float mid_freq_hz,
                    float mid_q,
                    float high_freq_hz)
{
    if(!eq)
        return;

    if(sample_rate_hz <= 0.0f)
        sample_rate_hz = 48000.0f;

    eq->sample_rate_hz = sample_rate_hz;
    eq->low_freq_hz = low_freq_hz;
    eq->mid_freq_hz = mid_freq_hz;
    eq->mid_q = (mid_q <= 0.0f) ? 1.0f : mid_q;
    eq->high_freq_hz = high_freq_hz;

    fx_biquad_set_identity(&eq->low_l);
    fx_biquad_set_identity(&eq->low_r);
    fx_biquad_set_identity(&eq->mid_l);
    fx_biquad_set_identity(&eq->mid_r);
    fx_biquad_set_identity(&eq->high_l);
    fx_biquad_set_identity(&eq->high_r);

    fx_dj_eq3_reset(eq);

    fx_dj_eq3_set_low_db(eq, 0.0f);
    fx_dj_eq3_set_mid_db(eq, 0.0f);
    fx_dj_eq3_set_high_db(eq, 0.0f);
}
