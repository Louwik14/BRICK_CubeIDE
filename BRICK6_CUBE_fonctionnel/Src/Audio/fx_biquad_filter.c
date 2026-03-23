/**
 * @file fx_biquad_filter.c
 * @brief Filtre biquad CMSIS multimode stéréo pour runtime par track.
 */

#include "fx_biquad_filter.h"

#include <math.h>
#include <string.h>

#define FX_BIQUAD_FILTER_NUM_STAGES 1U
#define FX_BIQUAD_FILTER_MIN_FREQ   20.0f
#define FX_BIQUAD_FILTER_MIN_Q      0.70710678f
#define FX_BIQUAD_FILTER_MAX_Q      12.0f
#define FX_BIQUAD_FILTER_DEFAULT_Q  0.70710678f

static inline float fx_biquad_filter_clamp(float x, float lo, float hi)
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

static inline float fx_biquad_filter_safe(float x)
{
    return isfinite(x) ? x : 0.0f;
}

static inline float fx_biquad_filter_clamp_freq(float cutoff_hz, float sample_rate)
{
    const float nyquist_margin = sample_rate * 0.49f;
    return fx_biquad_filter_clamp(cutoff_hz, FX_BIQUAD_FILTER_MIN_FREQ, nyquist_margin);
}

static inline float fx_biquad_filter_clamp_q(float q)
{
    return fx_biquad_filter_clamp(q, FX_BIQUAD_FILTER_MIN_Q, FX_BIQUAD_FILTER_MAX_Q);
}

static void fx_biquad_filter_compute_coeffs(const fx_biquad_filter_t *filter, float *coeffs)
{
    const float fs = (filter->sample_rate > 0.0f) ? filter->sample_rate : 48000.0f;
    const float f0 = fx_biquad_filter_clamp_freq(filter->cutoff_hz, fs);
    const float q = fx_biquad_filter_clamp_q(filter->q);
    const float w0 = 2.0f * PI * f0 / fs;
    const float cos_w0 = cosf(w0);
    const float sin_w0 = sinf(w0);
    const float alpha = sin_w0 / (2.0f * q);

    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a0 = 1.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;

    switch((fx_biquad_filter_mode_t)filter->mode)
    {
        case FX_BIQUAD_FILTER_MODE_LP:
            b0 = 0.5f * (1.0f - cos_w0);
            b1 = 1.0f - cos_w0;
            b2 = 0.5f * (1.0f - cos_w0);
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha;
            break;

        case FX_BIQUAD_FILTER_MODE_HP:
            b0 = 0.5f * (1.0f + cos_w0);
            b1 = -(1.0f + cos_w0);
            b2 = 0.5f * (1.0f + cos_w0);
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha;
            break;

        case FX_BIQUAD_FILTER_MODE_BP:
        default:
            b0 = alpha;
            b1 = 0.0f;
            b2 = -alpha;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha;
            break;
    }

    const float inv_a0 = (fabsf(a0) > 1.0e-12f) ? (1.0f / a0) : 0.0f;
    coeffs[0] = fx_biquad_filter_safe(b0 * inv_a0);
    coeffs[1] = fx_biquad_filter_safe(b1 * inv_a0);
    coeffs[2] = fx_biquad_filter_safe(b2 * inv_a0);
    coeffs[3] = fx_biquad_filter_safe((-a1) * inv_a0);
    coeffs[4] = fx_biquad_filter_safe((-a2) * inv_a0);
}

void fx_biquad_filter_init(fx_biquad_filter_t *filter, float sample_rate)
{
    if(filter == NULL)
    {
        return;
    }

    memset(filter, 0, sizeof(*filter));
    filter->sample_rate = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    filter->cutoff_hz = 16000.0f;
    filter->q = FX_BIQUAD_FILTER_DEFAULT_Q;
    filter->mode = (uint8_t)FX_BIQUAD_FILTER_MODE_LP;
    filter->bypass = 1U;

    fx_biquad_filter_update_coeffs(filter);
    memcpy(filter->coeffs, filter->coeffs_pending, sizeof(filter->coeffs));
    filter->coeffs_pending_update = 0U;
    arm_biquad_cascade_df1_init_f32(&filter->inst_l, FX_BIQUAD_FILTER_NUM_STAGES, filter->coeffs, filter->state_l);
    arm_biquad_cascade_df1_init_f32(&filter->inst_r, FX_BIQUAD_FILTER_NUM_STAGES, filter->coeffs, filter->state_r);
}

void fx_biquad_filter_reset(fx_biquad_filter_t *filter)
{
    if(filter == NULL)
    {
        return;
    }

    memset(filter->state_l, 0, sizeof(filter->state_l));
    memset(filter->state_r, 0, sizeof(filter->state_r));
    arm_biquad_cascade_df1_init_f32(&filter->inst_l, FX_BIQUAD_FILTER_NUM_STAGES, filter->coeffs, filter->state_l);
    arm_biquad_cascade_df1_init_f32(&filter->inst_r, FX_BIQUAD_FILTER_NUM_STAGES, filter->coeffs, filter->state_r);
}

void fx_biquad_filter_update_coeffs(fx_biquad_filter_t *filter)
{
    if(filter == NULL)
    {
        return;
    }

    fx_biquad_filter_compute_coeffs(filter, filter->coeffs_pending);
    filter->coeffs_pending_update = 1U;
}

void fx_biquad_filter_set_sample_rate(fx_biquad_filter_t *filter, float sample_rate)
{
    if(filter == NULL)
    {
        return;
    }

    filter->sample_rate = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    fx_biquad_filter_update_coeffs(filter);
}

void fx_biquad_filter_set_mode(fx_biquad_filter_t *filter, fx_biquad_filter_mode_t mode)
{
    if(filter == NULL)
    {
        return;
    }

    filter->mode = (uint8_t)mode;
    fx_biquad_filter_update_coeffs(filter);
}

void fx_biquad_filter_set_cutoff(fx_biquad_filter_t *filter, float cutoff_hz)
{
    if(filter == NULL)
    {
        return;
    }

    filter->cutoff_hz = fx_biquad_filter_clamp_freq(cutoff_hz, filter->sample_rate);
    fx_biquad_filter_update_coeffs(filter);
}

void fx_biquad_filter_set_q(fx_biquad_filter_t *filter, float q)
{
    if(filter == NULL)
    {
        return;
    }

    filter->q = fx_biquad_filter_clamp_q(q);
    fx_biquad_filter_update_coeffs(filter);
}

void fx_biquad_filter_set_bypass(fx_biquad_filter_t *filter, uint8_t bypass)
{
    if(filter == NULL)
    {
        return;
    }

    filter->bypass = (bypass != 0U) ? 1U : 0U;
}

void fx_biquad_filter_process_block(fx_biquad_filter_t *filter,
                                    float *inout_l,
                                    float *inout_r,
                                    uint32_t block_size)
{
    if((filter == NULL) || (inout_l == NULL) || (inout_r == NULL) || (block_size == 0U) || (filter->bypass != 0U))
    {
        return;
    }

    if(filter->coeffs_pending_update != 0U)
    {
        memcpy(filter->coeffs, filter->coeffs_pending, sizeof(filter->coeffs));
        filter->coeffs_pending_update = 0U;
    }

    arm_biquad_cascade_df1_f32(&filter->inst_l, inout_l, inout_l, block_size);
    arm_biquad_cascade_df1_f32(&filter->inst_r, inout_r, inout_r, block_size);
}
