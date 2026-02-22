#include "fx_dj_eq3_cmsis.h"

#include <math.h>
#include <string.h>

#define FX_DJ_EQ3_NUM_STAGES 3U
#define FX_DJ_EQ3_MIN_DB    (-60.0f)
#define FX_DJ_EQ3_MAX_DB    (12.0f)
#define FX_DJ_EQ3_SHELF_S   (1.0f)
#define FX_DJ_EQ3_MIN_FREQ  (10.0f)

static inline float fx_clamp(float x, float lo, float hi)
{
    if(x < lo)
    {
        return lo;
    }
    if(x > hi)
    {
        return hi;
    }
    return x;
}

static inline float fx_safe(float x)
{
    return isfinite(x) ? x : 0.0f;
}

static inline float fx_clamp_db(float db)
{
    return fx_clamp(db, FX_DJ_EQ3_MIN_DB, FX_DJ_EQ3_MAX_DB);
}

static inline float fx_clamp_freq(float f, float sample_rate)
{
    const float nyquist_margin = sample_rate * 0.49f;
    return fx_clamp(f, FX_DJ_EQ3_MIN_FREQ, nyquist_margin);
}


static inline float fx_sanitize_sample(float x)
{
    if(!isfinite(x))
    {
        return 0.0f;
    }

    if(fabsf(x) < 1.0e-20f)
    {
        return 0.0f;
    }

    return x;
}

static void rbj_low_shelf(float fs, float f0, float gain_db, float s, float *c)
{
    const float a = powf(10.0f, gain_db * 0.025f);
    const float w0 = 2.0f * PI * f0 / fs;
    const float cos_w0 = cosf(w0);
    const float sin_w0 = sinf(w0);
    const float root_a = sqrtf(a);
    const float t = (a + (1.0f / a)) * ((1.0f / s) - 1.0f) + 2.0f;
    const float alpha = 0.5f * sin_w0 * sqrtf(fmaxf(t, 0.0f));

    const float ap1 = a + 1.0f;
    const float am1 = a - 1.0f;
    const float two_root_alpha = 2.0f * root_a * alpha;

    const float b0 = a * (ap1 - am1 * cos_w0 + two_root_alpha);
    const float b1 = 2.0f * a * (am1 - ap1 * cos_w0);
    const float b2 = a * (ap1 - am1 * cos_w0 - two_root_alpha);
    const float a0 = ap1 + am1 * cos_w0 + two_root_alpha;
    const float a1 = -2.0f * (am1 + ap1 * cos_w0);
    const float a2 = ap1 + am1 * cos_w0 - two_root_alpha;

    const float inv_a0 = (fabsf(a0) > 1.0e-12f) ? (1.0f / a0) : 0.0f;
    c[0] = fx_safe(b0 * inv_a0);
    c[1] = fx_safe(b1 * inv_a0);
    c[2] = fx_safe(b2 * inv_a0);
    c[3] = fx_safe((-a1) * inv_a0);
    c[4] = fx_safe((-a2) * inv_a0);
}

static void rbj_peaking(float fs, float f0, float q, float gain_db, float *c)
{
    const float a = powf(10.0f, gain_db * 0.025f);
    const float w0 = 2.0f * PI * f0 / fs;
    const float alpha = sinf(w0) / (2.0f * q);
    const float cos_w0 = cosf(w0);

    const float b0 = 1.0f + alpha * a;
    const float b1 = -2.0f * cos_w0;
    const float b2 = 1.0f - alpha * a;
    const float a0 = 1.0f + alpha / a;
    const float a1 = -2.0f * cos_w0;
    const float a2 = 1.0f - alpha / a;

    const float inv_a0 = (fabsf(a0) > 1.0e-12f) ? (1.0f / a0) : 0.0f;
    c[0] = fx_safe(b0 * inv_a0);
    c[1] = fx_safe(b1 * inv_a0);
    c[2] = fx_safe(b2 * inv_a0);
    c[3] = fx_safe((-a1) * inv_a0);
    c[4] = fx_safe((-a2) * inv_a0);
}

static void rbj_high_shelf(float fs, float f0, float gain_db, float s, float *c)
{
    const float a = powf(10.0f, gain_db * 0.025f);
    const float w0 = 2.0f * PI * f0 / fs;
    const float cos_w0 = cosf(w0);
    const float sin_w0 = sinf(w0);
    const float root_a = sqrtf(a);
    const float t = (a + (1.0f / a)) * ((1.0f / s) - 1.0f) + 2.0f;
    const float alpha = 0.5f * sin_w0 * sqrtf(fmaxf(t, 0.0f));

    const float ap1 = a + 1.0f;
    const float am1 = a - 1.0f;
    const float two_root_alpha = 2.0f * root_a * alpha;

    const float b0 = a * (ap1 + am1 * cos_w0 + two_root_alpha);
    const float b1 = -2.0f * a * (am1 + ap1 * cos_w0);
    const float b2 = a * (ap1 + am1 * cos_w0 - two_root_alpha);
    const float a0 = ap1 - am1 * cos_w0 + two_root_alpha;
    const float a1 = 2.0f * (am1 - ap1 * cos_w0);
    const float a2 = ap1 - am1 * cos_w0 - two_root_alpha;

    const float inv_a0 = (fabsf(a0) > 1.0e-12f) ? (1.0f / a0) : 0.0f;
    c[0] = fx_safe(b0 * inv_a0);
    c[1] = fx_safe(b1 * inv_a0);
    c[2] = fx_safe(b2 * inv_a0);
    c[3] = fx_safe((-a1) * inv_a0);
    c[4] = fx_safe((-a2) * inv_a0);
}


void fx_dj_eq3_reset(fx_dj_eq3_t *eq)
{
    if(eq == NULL)
    {
        return;
    }

    memset(eq->state_l, 0, sizeof(eq->state_l));
    memset(eq->state_r, 0, sizeof(eq->state_r));
}

void fx_dj_eq3_update_coeffs(fx_dj_eq3_t *eq)
{
    if(eq == NULL)
    {
        return;
    }

    float coeffs_tmp[3U * 5U];

    eq->low_db = fx_clamp_db(eq->low_db);
    eq->mid_db = fx_clamp_db(eq->mid_db);
    eq->high_db = fx_clamp_db(eq->high_db);
    eq->sample_rate = (eq->sample_rate > 1000.0f) ? eq->sample_rate : 48000.0f;

    const float fs = eq->sample_rate;
    const float low_f = fx_clamp_freq(eq->low_freq, fs);
    const float mid_f = fx_clamp_freq(eq->mid_freq, fs);
    const float high_f = fx_clamp_freq(eq->high_freq, fs);
    const float q = (eq->mid_q > 0.05f) ? eq->mid_q : 1.0f;

    eq->low_freq = low_f;
    eq->mid_freq = mid_f;
    eq->high_freq = high_f;
    eq->mid_q = q;

    rbj_low_shelf(fs, low_f, eq->low_db, FX_DJ_EQ3_SHELF_S, &coeffs_tmp[0]);
    rbj_peaking(fs, mid_f, q, eq->mid_db, &coeffs_tmp[5]);
    rbj_high_shelf(fs, high_f, eq->high_db, FX_DJ_EQ3_SHELF_S, &coeffs_tmp[10]);

    eq->coeffs_pending_update = 0U;
    __DMB();
    memcpy(eq->coeffs_pending, coeffs_tmp, sizeof(coeffs_tmp));
    __DMB();
    eq->coeffs_pending_update = 1U;

    eq->bypass = ((fabsf(eq->low_db) < 1.0e-6f) &&
                  (fabsf(eq->mid_db) < 1.0e-6f) &&
                  (fabsf(eq->high_db) < 1.0e-6f))
                     ? 1U
                     : 0U;
}

void fx_dj_eq3_set_low_db(fx_dj_eq3_t *eq, float gain_db)
{
    if(eq == NULL)
    {
        return;
    }

    const float clamped = fx_clamp_db(gain_db);
    if(clamped != eq->low_db)
    {
        eq->low_db = clamped;
        fx_dj_eq3_update_coeffs(eq);
    }
}

void fx_dj_eq3_set_mid_db(fx_dj_eq3_t *eq, float gain_db)
{
    if(eq == NULL)
    {
        return;
    }

    const float clamped = fx_clamp_db(gain_db);
    if(clamped != eq->mid_db)
    {
        eq->mid_db = clamped;
        fx_dj_eq3_update_coeffs(eq);
    }
}

void fx_dj_eq3_set_high_db(fx_dj_eq3_t *eq, float gain_db)
{
    if(eq == NULL)
    {
        return;
    }

    const float clamped = fx_clamp_db(gain_db);
    if(clamped != eq->high_db)
    {
        eq->high_db = clamped;
        fx_dj_eq3_update_coeffs(eq);
    }
}

void fx_dj_eq3_set_bypass(fx_dj_eq3_t *eq, uint8_t bypass)
{
    if(eq == NULL)
    {
        return;
    }

    eq->bypass = (bypass != 0U) ? 1U : 0U;
}

void fx_dj_eq3_init(fx_dj_eq3_t *eq,
                    float sample_rate,
                    float low_freq,
                    float mid_freq,
                    float mid_q,
                    float high_freq)
{
    if(eq == NULL)
    {
        return;
    }

    memset(eq, 0, sizeof(*eq));

    eq->sample_rate = sample_rate;
    eq->low_freq = low_freq;
    eq->mid_freq = mid_freq;
    eq->high_freq = high_freq;
    eq->mid_q = mid_q;

    arm_biquad_cascade_df1_init_f32(&eq->inst_l, FX_DJ_EQ3_NUM_STAGES, eq->coeffs, eq->state_l);
    arm_biquad_cascade_df1_init_f32(&eq->inst_r, FX_DJ_EQ3_NUM_STAGES, eq->coeffs, eq->state_r);

    fx_dj_eq3_update_coeffs(eq);
    if(eq->coeffs_pending_update != 0U)
    {
        memcpy(eq->coeffs, eq->coeffs_pending, sizeof(eq->coeffs));
        eq->coeffs_pending_update = 0U;
    }
    fx_dj_eq3_reset(eq);
}

void fx_dj_eq3_process_block(fx_dj_eq3_t *eq,
                             float *inout_l,
                             float *inout_r,
                             uint32_t block_size)
{
    if((eq == NULL) || (inout_l == NULL) || (inout_r == NULL) || (block_size == 0U))
    {
        return;
    }

    if(eq->coeffs_pending_update != 0U)
    {
        memcpy(eq->coeffs, eq->coeffs_pending, sizeof(eq->coeffs));
        __DMB();
        eq->coeffs_pending_update = 0U;
    }

    arm_biquad_cascade_df1_f32(&eq->inst_l, inout_l, inout_l, block_size);
    arm_biquad_cascade_df1_f32(&eq->inst_r, inout_r, inout_r, block_size);

    for(uint32_t n = 0U; n < block_size; n++)
    {
        inout_l[n] = fx_sanitize_sample(inout_l[n]);
        inout_r[n] = fx_sanitize_sample(inout_r[n]);
    }
}
