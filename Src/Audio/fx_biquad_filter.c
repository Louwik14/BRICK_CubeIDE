/**
 * @file fx_biquad_filter.c
 * @brief SVF TPT/ZDF multimode float mono/stereo.
 */

#include "fx_biquad_filter.h"

#include <math.h>
#include <string.h>

#define FX_FILTER_MIN_FREQ_HZ          20.0f
#define FX_FILTER_MAX_FREQ_HZ       16000.0f
#define FX_FILTER_DEFAULT_SR        48000.0f
#define FX_FILTER_MIN_Q                 0.70710678f
#define FX_FILTER_MAX_Q                 6.5f
#define FX_FILTER_G_LUT_SIZE         1024U
#define FX_FILTER_BYPASS_XFADE_SAMPLES 256U

static float g_fx_filter_g_lut[FX_FILTER_G_LUT_SIZE + 1U];
static uint8_t g_fx_filter_g_lut_ready;

static inline float fx_filter_clampf(float x, float lo, float hi)
{
    if(x < lo) return lo;
    if(x > hi) return hi;
    return x;
}

static void fx_filter_init_g_lut(float sample_rate)
{
    if(g_fx_filter_g_lut_ready != 0U) return;
    const float sr = (sample_rate > 1000.0f) ? sample_rate : FX_FILTER_DEFAULT_SR;
    for(uint32_t i = 0U; i <= FX_FILTER_G_LUT_SIZE; ++i)
    {
        const float t = (float)i * (1.0f / (float)FX_FILTER_G_LUT_SIZE);
        const float f = FX_FILTER_MIN_FREQ_HZ
                      + ((FX_FILTER_MAX_FREQ_HZ - FX_FILTER_MIN_FREQ_HZ) * t);
        g_fx_filter_g_lut[i] = tanf(3.14159265358979323846f * f / sr);
    }
    g_fx_filter_g_lut_ready = 1U;
}

static inline float fx_filter_lookup_g(float cutoff_hz)
{
    const float cutoff = fx_filter_clampf(cutoff_hz,
                                          FX_FILTER_MIN_FREQ_HZ,
                                          FX_FILTER_MAX_FREQ_HZ);
    const float pos = (cutoff - FX_FILTER_MIN_FREQ_HZ)
                    * ((float)FX_FILTER_G_LUT_SIZE
                       / (FX_FILTER_MAX_FREQ_HZ - FX_FILTER_MIN_FREQ_HZ));
    const uint32_t index = (uint32_t)pos;
    if(index >= FX_FILTER_G_LUT_SIZE)
        return g_fx_filter_g_lut[FX_FILTER_G_LUT_SIZE];
    const float a = g_fx_filter_g_lut[index];
    return a + ((g_fx_filter_g_lut[index + 1U] - a) * (pos - (float)index));
}

static inline void fx_filter_make_coeffs(float cutoff_hz,
                                         float q,
                                         fx_biquad_filter_coeffs_t *out)
{
    const float q_safe = fx_filter_clampf(q, FX_FILTER_MIN_Q, FX_FILTER_MAX_Q);
    const float g = fx_filter_lookup_g(cutoff_hz);
    const float k = 1.0f / q_safe;
    const float a1 = 1.0f / (1.0f + (g * (g + k)));
    const float r = (q_safe - FX_FILTER_MIN_Q) / (FX_FILTER_MAX_Q - FX_FILTER_MIN_Q);

    out->a1 = a1;
    out->a2 = g * a1;
    out->a3 = g * g * a1;
    out->k = k;
    /* Per-mode calibration; BP receives explicit Q-independent normalization. */
    out->lp_gain = 1.0f + (0.035f * r);
    out->hp_gain = 0.98f + (0.055f * r);
    out->bp_gain = 0.92f + (0.08f * r);
}

static inline float fx_filter_tpt_sample_lp(float input,
                                            float *ic1eq,
                                            float *ic2eq,
                                            const fx_biquad_filter_coeffs_t *c)
{
    const float v3 = input - *ic2eq;
    const float v1 = (c->a1 * *ic1eq) + (c->a2 * v3);
    const float v2 = *ic2eq + (c->a2 * *ic1eq) + (c->a3 * v3);
    *ic1eq = (2.0f * v1) - *ic1eq;
    *ic2eq = (2.0f * v2) - *ic2eq;
    return v2 * c->lp_gain;
}

static inline float fx_filter_tpt_sample_bp(float input,
                                            float *ic1eq,
                                            float *ic2eq,
                                            const fx_biquad_filter_coeffs_t *c)
{
    const float v3 = input - *ic2eq;
    const float v1 = (c->a1 * *ic1eq) + (c->a2 * v3);
    const float v2 = *ic2eq + (c->a2 * *ic1eq) + (c->a3 * v3);
    *ic1eq = (2.0f * v1) - *ic1eq;
    *ic2eq = (2.0f * v2) - *ic2eq;
    return v1 * c->bp_gain;
}

static inline float fx_filter_tpt_sample_hp(float input,
                                            float *ic1eq,
                                            float *ic2eq,
                                            const fx_biquad_filter_coeffs_t *c)
{
    const float v3 = input - *ic2eq;
    const float v1 = (c->a1 * *ic1eq) + (c->a2 * v3);
    const float v2 = *ic2eq + (c->a2 * *ic1eq) + (c->a3 * v3);
    const float high = input - (c->k * v1) - v2;
    *ic1eq = (2.0f * v1) - *ic1eq;
    *ic2eq = (2.0f * v2) - *ic2eq;
    return high * c->hp_gain;
}

static inline float fx_filter_tpt_sample_lp_bp(float input, float *ic1eq, float *ic2eq,
                                                const fx_biquad_filter_coeffs_t *c,
                                                float a, float b)
{
    const float v3 = input - *ic2eq;
    const float v1 = (c->a1 * *ic1eq) + (c->a2 * v3);
    const float v2 = *ic2eq + (c->a2 * *ic1eq) + (c->a3 * v3);
    *ic1eq = (2.0f * v1) - *ic1eq;
    *ic2eq = (2.0f * v2) - *ic2eq;
    return (a * v2) + (b * v1);
}

static inline float fx_filter_tpt_sample_bp_hp(float input, float *ic1eq, float *ic2eq,
                                                const fx_biquad_filter_coeffs_t *c,
                                                float a, float b)
{
    const float v3 = input - *ic2eq;
    const float v1 = (c->a1 * *ic1eq) + (c->a2 * v3);
    const float v2 = *ic2eq + (c->a2 * *ic1eq) + (c->a3 * v3);
    *ic1eq = (2.0f * v1) - *ic1eq;
    *ic2eq = (2.0f * v2) - *ic2eq;
    return (a * (input - v2)) + (b * v1);
}

static void fx_filter_prepare_morph(float morph, const fx_biquad_filter_coeffs_t *c,
                                    uint8_t *plan, float *a, float *b)
{
    morph = fx_filter_clampf(morph, 0.0f, 127.0f);
    if(morph == 0.0f) { *plan = FX_FILTER_MORPH_LP; *a = c->lp_gain; *b = 0.0f; return; }
    if(morph == 64.0f) { *plan = FX_FILTER_MORPH_BP; *a = 0.0f; *b = c->bp_gain; return; }
    if(morph == 127.0f) { *plan = FX_FILTER_MORPH_HP; *a = c->hp_gain; *b = -c->hp_gain * c->k; return; }
    if(morph < 64.0f)
    {
        const float t = morph * (1.0f / 64.0f);
        *plan = FX_FILTER_MORPH_LP_BP; *a = (1.0f - t) * c->lp_gain; *b = t * c->bp_gain;
    }
    else
    {
        const float t = (morph - 64.0f) * (1.0f / 63.0f);
        *plan = FX_FILTER_MORPH_BP_HP; *a = t * c->hp_gain;
        *b = ((1.0f - t) * c->bp_gain) - (*a * c->k);
    }
}

static inline float fx_filter_apply_bypass(float dry,
                                           float wet,
                                           float *mix,
                                           uint16_t *remaining,
                                           uint8_t bypass)
{
    if(*remaining != 0U)
    {
        const float target = (bypass != 0U) ? 1.0f : 0.0f;
        const float step = (target - *mix) / (float)*remaining;
        *mix += step;
        --(*remaining);
        if(*remaining == 0U) *mix = target;
    }
    return wet + ((dry - wet) * *mix);
}

static inline void fx_filter_stereo_process_lp(fx_biquad_filter_t *filter,
                                                float *left,
                                                float *right,
                                                uint32_t frames,
                                                const fx_biquad_filter_coeffs_t *c,
                                                uint8_t apply_bypass)
{
    if(apply_bypass == 0U)
    {
        for(uint32_t i = 0U; i < frames; ++i)
        {
            left[i] = fx_filter_tpt_sample_lp(left[i], &filter->ic1eq_l,
                                               &filter->ic2eq_l, c);
            right[i] = fx_filter_tpt_sample_lp(right[i], &filter->ic1eq_r,
                                                &filter->ic2eq_r, c);
        }
        return;
    }

    for(uint32_t i = 0U; i < frames; ++i)
    {
        const float dry_l = left[i];
        const float dry_r = right[i];
        const float wet_l = fx_filter_tpt_sample_lp(dry_l, &filter->ic1eq_l,
                                                    &filter->ic2eq_l, c);
        const float wet_r = fx_filter_tpt_sample_lp(dry_r, &filter->ic1eq_r,
                                                    &filter->ic2eq_r, c);
        left[i] = fx_filter_apply_bypass(dry_l, wet_l, &filter->bypass_mix,
                                         &filter->bypass_xfade_remaining,
                                         filter->bypass);
        right[i] = wet_r + ((dry_r - wet_r) * filter->bypass_mix);
    }
}

static inline void fx_filter_stereo_process_bp(fx_biquad_filter_t *filter,
                                                float *left,
                                                float *right,
                                                uint32_t frames,
                                                const fx_biquad_filter_coeffs_t *c,
                                                uint8_t apply_bypass)
{
    if(apply_bypass == 0U)
    {
        for(uint32_t i = 0U; i < frames; ++i)
        {
            left[i] = fx_filter_tpt_sample_bp(left[i], &filter->ic1eq_l,
                                               &filter->ic2eq_l, c);
            right[i] = fx_filter_tpt_sample_bp(right[i], &filter->ic1eq_r,
                                                &filter->ic2eq_r, c);
        }
        return;
    }

    for(uint32_t i = 0U; i < frames; ++i)
    {
        const float dry_l = left[i];
        const float dry_r = right[i];
        const float wet_l = fx_filter_tpt_sample_bp(dry_l, &filter->ic1eq_l,
                                                    &filter->ic2eq_l, c);
        const float wet_r = fx_filter_tpt_sample_bp(dry_r, &filter->ic1eq_r,
                                                    &filter->ic2eq_r, c);
        left[i] = fx_filter_apply_bypass(dry_l, wet_l, &filter->bypass_mix,
                                         &filter->bypass_xfade_remaining,
                                         filter->bypass);
        right[i] = wet_r + ((dry_r - wet_r) * filter->bypass_mix);
    }
}

static inline void fx_filter_stereo_process_hp(fx_biquad_filter_t *filter,
                                                float *left,
                                                float *right,
                                                uint32_t frames,
                                                const fx_biquad_filter_coeffs_t *c,
                                                uint8_t apply_bypass)
{
    if(apply_bypass == 0U)
    {
        for(uint32_t i = 0U; i < frames; ++i)
        {
            left[i] = fx_filter_tpt_sample_hp(left[i], &filter->ic1eq_l,
                                               &filter->ic2eq_l, c);
            right[i] = fx_filter_tpt_sample_hp(right[i], &filter->ic1eq_r,
                                                &filter->ic2eq_r, c);
        }
        return;
    }

    for(uint32_t i = 0U; i < frames; ++i)
    {
        const float dry_l = left[i];
        const float dry_r = right[i];
        const float wet_l = fx_filter_tpt_sample_hp(dry_l, &filter->ic1eq_l,
                                                    &filter->ic2eq_l, c);
        const float wet_r = fx_filter_tpt_sample_hp(dry_r, &filter->ic1eq_r,
                                                    &filter->ic2eq_r, c);
        left[i] = fx_filter_apply_bypass(dry_l, wet_l, &filter->bypass_mix,
                                         &filter->bypass_xfade_remaining,
                                         filter->bypass);
        right[i] = wet_r + ((dry_r - wet_r) * filter->bypass_mix);
    }
}

static inline void fx_filter_mono_process_lp(fx_biquad_filter_mono_t *filter,
                                              float *samples,
                                              uint32_t frames,
                                              const fx_biquad_filter_coeffs_t *c,
                                              uint8_t apply_bypass)
{
    if(apply_bypass == 0U)
    {
        for(uint32_t i = 0U; i < frames; ++i)
        {
            samples[i] = fx_filter_tpt_sample_lp(samples[i], &filter->ic1eq,
                                                  &filter->ic2eq, c);
        }
        return;
    }

    for(uint32_t i = 0U; i < frames; ++i)
    {
        const float dry = samples[i];
        const float wet = fx_filter_tpt_sample_lp(dry, &filter->ic1eq,
                                                  &filter->ic2eq, c);
        samples[i] = fx_filter_apply_bypass(dry, wet, &filter->bypass_mix,
                                            &filter->bypass_xfade_remaining,
                                            filter->bypass);
    }
}

static inline void fx_filter_mono_process_bp(fx_biquad_filter_mono_t *filter,
                                              float *samples,
                                              uint32_t frames,
                                              const fx_biquad_filter_coeffs_t *c,
                                              uint8_t apply_bypass)
{
    if(apply_bypass == 0U)
    {
        for(uint32_t i = 0U; i < frames; ++i)
        {
            samples[i] = fx_filter_tpt_sample_bp(samples[i], &filter->ic1eq,
                                                  &filter->ic2eq, c);
        }
        return;
    }

    for(uint32_t i = 0U; i < frames; ++i)
    {
        const float dry = samples[i];
        const float wet = fx_filter_tpt_sample_bp(dry, &filter->ic1eq,
                                                  &filter->ic2eq, c);
        samples[i] = fx_filter_apply_bypass(dry, wet, &filter->bypass_mix,
                                            &filter->bypass_xfade_remaining,
                                            filter->bypass);
    }
}

static inline void fx_filter_mono_process_hp(fx_biquad_filter_mono_t *filter,
                                              float *samples,
                                              uint32_t frames,
                                              const fx_biquad_filter_coeffs_t *c,
                                              uint8_t apply_bypass)
{
    if(apply_bypass == 0U)
    {
        for(uint32_t i = 0U; i < frames; ++i)
        {
            samples[i] = fx_filter_tpt_sample_hp(samples[i], &filter->ic1eq,
                                                  &filter->ic2eq, c);
        }
        return;
    }

    for(uint32_t i = 0U; i < frames; ++i)
    {
        const float dry = samples[i];
        const float wet = fx_filter_tpt_sample_hp(dry, &filter->ic1eq,
                                                  &filter->ic2eq, c);
        samples[i] = fx_filter_apply_bypass(dry, wet, &filter->bypass_mix,
                                            &filter->bypass_xfade_remaining,
                                            filter->bypass);
    }
}

static void fx_filter_stereo_reset_states(fx_biquad_filter_t *filter)
{
    filter->ic1eq_l = 0.0f;
    filter->ic2eq_l = 0.0f;
    filter->ic1eq_r = 0.0f;
    filter->ic2eq_r = 0.0f;
}

static void fx_filter_mono_reset_states(fx_biquad_filter_mono_t *filter)
{
    filter->ic1eq = 0.0f;
    filter->ic2eq = 0.0f;
}

void fx_biquad_filter_init(fx_biquad_filter_t *filter, float sample_rate)
{
    if(filter == NULL) return;
    fx_filter_init_g_lut(sample_rate);
    memset(filter, 0, sizeof(*filter));
    filter->sample_rate = (sample_rate > 1000.0f) ? sample_rate : FX_FILTER_DEFAULT_SR;
    filter->cutoff_hz = FX_FILTER_MAX_FREQ_HZ;
    filter->q = FX_FILTER_MIN_Q;
    filter->morph_plan = (uint8_t)FX_FILTER_MORPH_LP;
    filter->bypass = 1U;
    filter->bypass_mix = 1.0f;
    fx_filter_make_coeffs(filter->cutoff_hz, filter->q, &filter->current);
}

void fx_biquad_filter_reset(fx_biquad_filter_t *filter)
{
    if(filter == NULL) return;
    fx_filter_stereo_reset_states(filter);
}

void fx_biquad_filter_update_coeffs(fx_biquad_filter_t *filter)
{
    if(filter != NULL) { fx_filter_make_coeffs(filter->cutoff_hz, filter->q, &filter->current); fx_biquad_filter_set_morph(filter, filter->morph); }
}

void fx_biquad_filter_set_sample_rate(fx_biquad_filter_t *filter, float sample_rate)
{
    if(filter == NULL) return;
    filter->sample_rate = (sample_rate > 1000.0f) ? sample_rate : FX_FILTER_DEFAULT_SR;
    fx_biquad_filter_update_coeffs(filter);
}

void fx_biquad_filter_set_morph(fx_biquad_filter_t *filter, float morph)
{
    if(filter == NULL) return;
    filter->morph = fx_filter_clampf(morph, 0.0f, 127.0f);
    fx_filter_prepare_morph(morph, &filter->current, &filter->morph_plan,
                            &filter->morph_a, &filter->morph_b);
}

void fx_biquad_filter_set_params(fx_biquad_filter_t *filter, float cutoff_hz, float q)
{
    if(filter == NULL) return;
    const float cutoff = fx_filter_clampf(cutoff_hz, FX_FILTER_MIN_FREQ_HZ, FX_FILTER_MAX_FREQ_HZ);
    const float q_safe = fx_filter_clampf(q, FX_FILTER_MIN_Q, FX_FILTER_MAX_Q);
    if((fabsf(cutoff - filter->cutoff_hz) < 0.001f)
            && (fabsf(q_safe - filter->q) < 0.0001f)) return;
    filter->cutoff_hz = cutoff;
    filter->q = q_safe;
    fx_biquad_filter_update_coeffs(filter);
}

void fx_biquad_filter_set_cutoff(fx_biquad_filter_t *filter, float cutoff_hz)
{
    if(filter != NULL) fx_biquad_filter_set_params(filter, cutoff_hz, filter->q);
}

void fx_biquad_filter_set_q(fx_biquad_filter_t *filter, float q)
{
    if(filter != NULL) fx_biquad_filter_set_params(filter, filter->cutoff_hz, q);
}

void fx_biquad_filter_set_bypass(fx_biquad_filter_t *filter, uint8_t bypass)
{
    if(filter == NULL) return;
    const uint8_t target = (bypass != 0U) ? 1U : 0U;
    if(filter->bypass == target) return;
    filter->bypass = target;
    filter->bypass_xfade_remaining = FX_FILTER_BYPASS_XFADE_SAMPLES;
    if(target != 0U)
    {
        filter->reset_after_bypass = 1U;
    }
    else
    {
        fx_filter_stereo_reset_states(filter);
        filter->reset_after_bypass = 0U;
    }
}

void fx_biquad_filter_process_block(fx_biquad_filter_t *filter,
                                    float *left,
                                    float *right,
                                    uint32_t frames)
{
    if((filter == NULL) || (left == NULL) || (right == NULL) || (frames == 0U)) return;
    if((filter->bypass != 0U) && (filter->bypass_xfade_remaining == 0U)) return;

    const fx_biquad_filter_coeffs_t c = filter->current;
    const uint8_t apply_bypass = (uint8_t)((filter->bypass != 0U) || (filter->bypass_xfade_remaining != 0U));
    if(filter->morph_plan == FX_FILTER_MORPH_LP)
        fx_filter_stereo_process_lp(filter, left, right, frames, &c, apply_bypass);
    else if(filter->morph_plan == FX_FILTER_MORPH_BP)
        fx_filter_stereo_process_bp(filter, left, right, frames, &c, apply_bypass);
    else if(filter->morph_plan == FX_FILTER_MORPH_HP)
        fx_filter_stereo_process_hp(filter, left, right, frames, &c, apply_bypass);
    else if(filter->morph_plan == FX_FILTER_MORPH_LP_BP)
        for(uint32_t i = 0U; i < frames; ++i) { const float dl=left[i],dr=right[i]; const float wl=fx_filter_tpt_sample_lp_bp(dl,&filter->ic1eq_l,&filter->ic2eq_l,&c,filter->morph_a,filter->morph_b),wr=fx_filter_tpt_sample_lp_bp(dr,&filter->ic1eq_r,&filter->ic2eq_r,&c,filter->morph_a,filter->morph_b); if(apply_bypass!=0U){left[i]=fx_filter_apply_bypass(dl,wl,&filter->bypass_mix,&filter->bypass_xfade_remaining,filter->bypass);right[i]=wr+((dr-wr)*filter->bypass_mix);}else{left[i]=wl;right[i]=wr;} }
    else
        for(uint32_t i = 0U; i < frames; ++i) { const float dl=left[i],dr=right[i]; const float wl=fx_filter_tpt_sample_bp_hp(dl,&filter->ic1eq_l,&filter->ic2eq_l,&c,filter->morph_a,filter->morph_b),wr=fx_filter_tpt_sample_bp_hp(dr,&filter->ic1eq_r,&filter->ic2eq_r,&c,filter->morph_a,filter->morph_b); if(apply_bypass!=0U){left[i]=fx_filter_apply_bypass(dl,wl,&filter->bypass_mix,&filter->bypass_xfade_remaining,filter->bypass);right[i]=wr+((dr-wr)*filter->bypass_mix);}else{left[i]=wl;right[i]=wr;} }
    if((filter->bypass != 0U) && (filter->bypass_xfade_remaining == 0U)
            && (filter->reset_after_bypass != 0U))
    {
        fx_filter_stereo_reset_states(filter);
        filter->reset_after_bypass = 0U;
    }
}

void fx_biquad_filter_mono_init(fx_biquad_filter_mono_t *filter, float sample_rate)
{
    if(filter == NULL) return;
    fx_filter_init_g_lut(sample_rate);
    memset(filter, 0, sizeof(*filter));
    filter->sample_rate = (sample_rate > 1000.0f) ? sample_rate : FX_FILTER_DEFAULT_SR;
    filter->cutoff_hz = FX_FILTER_MAX_FREQ_HZ;
    filter->q = FX_FILTER_MIN_Q;
    filter->morph_plan = (uint8_t)FX_FILTER_MORPH_LP;
    filter->bypass = 1U;
    filter->bypass_mix = 1.0f;
    fx_filter_make_coeffs(filter->cutoff_hz, filter->q, &filter->current);
}

void fx_biquad_filter_mono_reset(fx_biquad_filter_mono_t *filter)
{
    if(filter != NULL) fx_filter_mono_reset_states(filter);
}

void fx_biquad_filter_mono_update_coeffs(fx_biquad_filter_mono_t *filter)
{
    if(filter != NULL) { fx_filter_make_coeffs(filter->cutoff_hz, filter->q, &filter->current); fx_biquad_filter_mono_set_morph(filter, filter->morph); }
}

void fx_biquad_filter_mono_set_sample_rate(fx_biquad_filter_mono_t *filter, float sample_rate)
{
    if(filter == NULL) return;
    filter->sample_rate = (sample_rate > 1000.0f) ? sample_rate : FX_FILTER_DEFAULT_SR;
    fx_biquad_filter_mono_update_coeffs(filter);
}

void fx_biquad_filter_mono_set_morph(fx_biquad_filter_mono_t *filter, float morph)
{
    if(filter == NULL) return;
    filter->morph = fx_filter_clampf(morph, 0.0f, 127.0f);
    fx_filter_prepare_morph(morph, &filter->current, &filter->morph_plan,
                            &filter->morph_a, &filter->morph_b);
}

void fx_biquad_filter_mono_set_params(fx_biquad_filter_mono_t *filter, float cutoff_hz, float q)
{
    if(filter == NULL) return;
    const float cutoff = fx_filter_clampf(cutoff_hz, FX_FILTER_MIN_FREQ_HZ, FX_FILTER_MAX_FREQ_HZ);
    const float q_safe = fx_filter_clampf(q, FX_FILTER_MIN_Q, FX_FILTER_MAX_Q);
    if((fabsf(cutoff - filter->cutoff_hz) < 0.001f)
            && (fabsf(q_safe - filter->q) < 0.0001f)) return;
    filter->cutoff_hz = cutoff;
    filter->q = q_safe;
    fx_biquad_filter_mono_update_coeffs(filter);
}

void fx_biquad_filter_mono_set_cutoff(fx_biquad_filter_mono_t *filter, float cutoff_hz)
{
    if(filter != NULL) fx_biquad_filter_mono_set_params(filter, cutoff_hz, filter->q);
}

void fx_biquad_filter_mono_set_q(fx_biquad_filter_mono_t *filter, float q)
{
    if(filter != NULL) fx_biquad_filter_mono_set_params(filter, filter->cutoff_hz, q);
}

void fx_biquad_filter_mono_set_bypass(fx_biquad_filter_mono_t *filter, uint8_t bypass)
{
    if(filter == NULL) return;
    const uint8_t target = (bypass != 0U) ? 1U : 0U;
    if(filter->bypass == target) return;
    filter->bypass = target;
    filter->bypass_xfade_remaining = FX_FILTER_BYPASS_XFADE_SAMPLES;
    if(target != 0U)
    {
        filter->reset_after_bypass = 1U;
    }
    else
    {
        fx_filter_mono_reset_states(filter);
        filter->reset_after_bypass = 0U;
    }
}

void fx_biquad_filter_mono_process_block(fx_biquad_filter_mono_t *filter,
                                         float *samples,
                                         uint32_t frames)
{
    if((filter == NULL) || (samples == NULL) || (frames == 0U)) return;
    if((filter->bypass != 0U) && (filter->bypass_xfade_remaining == 0U)) return;

    const fx_biquad_filter_coeffs_t c = filter->current;
    const uint8_t apply_bypass = (uint8_t)((filter->bypass != 0U) || (filter->bypass_xfade_remaining != 0U));
    if(filter->morph_plan == FX_FILTER_MORPH_LP)
        fx_filter_mono_process_lp(filter, samples, frames, &c, apply_bypass);
    else if(filter->morph_plan == FX_FILTER_MORPH_BP)
        fx_filter_mono_process_bp(filter, samples, frames, &c, apply_bypass);
    else if(filter->morph_plan == FX_FILTER_MORPH_HP)
        fx_filter_mono_process_hp(filter, samples, frames, &c, apply_bypass);
    else if(filter->morph_plan == FX_FILTER_MORPH_LP_BP)
        for(uint32_t i = 0U; i < frames; ++i) { const float dry=samples[i]; const float wet=fx_filter_tpt_sample_lp_bp(dry,&filter->ic1eq,&filter->ic2eq,&c,filter->morph_a,filter->morph_b); samples[i]=(apply_bypass!=0U)?fx_filter_apply_bypass(dry,wet,&filter->bypass_mix,&filter->bypass_xfade_remaining,filter->bypass):wet; }
    else
        for(uint32_t i = 0U; i < frames; ++i) { const float dry=samples[i]; const float wet=fx_filter_tpt_sample_bp_hp(dry,&filter->ic1eq,&filter->ic2eq,&c,filter->morph_a,filter->morph_b); samples[i]=(apply_bypass!=0U)?fx_filter_apply_bypass(dry,wet,&filter->bypass_mix,&filter->bypass_xfade_remaining,filter->bypass):wet; }
    if((filter->bypass != 0U) && (filter->bypass_xfade_remaining == 0U)
            && (filter->reset_after_bypass != 0U))
    {
        fx_filter_mono_reset_states(filter);
        filter->reset_after_bypass = 0U;
    }
}
