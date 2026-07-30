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
#define FX_FILTER_MODE_XFADE_SAMPLES   64U
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
    const float r2 = r * r;

    out->a1 = a1;
    out->a2 = g * a1;
    out->a3 = g * g * a1;
    out->k = k;
    /* -0.83 dB at maximum resonance; zero loss at minimum resonance. */
    out->input_gain = 1.0f - (0.091f * r2);
    /*
     * The band-integrator state is the resonance-loop signal. This rational
     * soft saturation is exactly linear at the origin and only becomes
     * material in the upper half of the resonance travel.
     */
    out->loop_saturation = 0.52f * r2 * r;
    /* Per-mode calibration; BP receives explicit Q-independent normalization. */
    out->lp_gain = 1.0f + (0.035f * r);
    out->hp_gain = 0.98f + (0.055f * r);
    out->bp_gain = 0.92f + (0.08f * r);
}

static inline float fx_filter_soft_loop(float state, float amount)
{
    return state / (1.0f + (amount * fabsf(state)));
}

static inline float fx_filter_mode_output(fx_biquad_filter_mode_t mode,
                                          float low,
                                          float band,
                                          float high,
                                          const fx_biquad_filter_coeffs_t *c)
{
    if(mode == FX_BIQUAD_FILTER_MODE_HP) return high * c->hp_gain;
    if(mode == FX_BIQUAD_FILTER_MODE_BP) return band * c->bp_gain;
    return low * c->lp_gain;
}

static inline float fx_filter_transition_output(uint8_t mode,
                                                uint8_t previous_mode,
                                                uint8_t via_dry,
                                                uint16_t remaining,
                                                float dry,
                                                float low,
                                                float band,
                                                float high,
                                                const fx_biquad_filter_coeffs_t *c)
{
    const float current = fx_filter_mode_output((fx_biquad_filter_mode_t)mode,
                                                low, band, high, c);
    if(remaining == 0U) return current;

    const float elapsed = (float)(FX_FILTER_MODE_XFADE_SAMPLES - remaining)
                        * (1.0f / (float)FX_FILTER_MODE_XFADE_SAMPLES);
    const float previous = fx_filter_mode_output((fx_biquad_filter_mode_t)previous_mode,
                                                 low, band, high, c);
    if(via_dry != 0U)
    {
        if(elapsed < 0.5f)
            return previous + ((dry - previous) * (elapsed * 2.0f));
        return dry + ((current - dry) * ((elapsed - 0.5f) * 2.0f));
    }
    return previous + ((current - previous) * elapsed);
}

static inline float fx_filter_tpt_sample(float input,
                                         float *ic1eq,
                                         float *ic2eq,
                                         const fx_biquad_filter_coeffs_t *c,
                                         uint8_t mode,
                                         uint8_t previous_mode,
                                         uint8_t via_dry,
                                         uint16_t mode_remaining)
{
    const float driven = input * c->input_gain;
    const float v3 = driven - *ic2eq;
    const float loop_state = fx_filter_soft_loop(*ic1eq, c->loop_saturation);
    const float v1 = (c->a1 * loop_state) + (c->a2 * v3);
    const float v2 = *ic2eq + (c->a2 * loop_state) + (c->a3 * v3);
    const float high = driven - (c->k * v1) - v2;
    *ic1eq = (2.0f * v1) - *ic1eq;
    *ic2eq = (2.0f * v2) - *ic2eq;
    return fx_filter_transition_output(mode, previous_mode, via_dry,
                                       mode_remaining, input, v2, v1, high, c);
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
    filter->mode = (uint8_t)FX_BIQUAD_FILTER_MODE_LP;
    filter->previous_mode = filter->mode;
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
    if(filter != NULL) fx_filter_make_coeffs(filter->cutoff_hz, filter->q, &filter->current);
}

void fx_biquad_filter_set_sample_rate(fx_biquad_filter_t *filter, float sample_rate)
{
    if(filter == NULL) return;
    filter->sample_rate = (sample_rate > 1000.0f) ? sample_rate : FX_FILTER_DEFAULT_SR;
    fx_biquad_filter_update_coeffs(filter);
}

void fx_biquad_filter_set_mode(fx_biquad_filter_t *filter, fx_biquad_filter_mode_t mode)
{
    if(filter == NULL) return;
    if(mode > FX_BIQUAD_FILTER_MODE_BP) mode = FX_BIQUAD_FILTER_MODE_LP;
    if(filter->mode != (uint8_t)mode)
    {
        filter->previous_mode = filter->mode;
        filter->mode_via_dry =
            (uint8_t)(((filter->mode == (uint8_t)FX_BIQUAD_FILTER_MODE_LP)
                    && (mode == FX_BIQUAD_FILTER_MODE_HP))
                   || ((filter->mode == (uint8_t)FX_BIQUAD_FILTER_MODE_HP)
                    && (mode == FX_BIQUAD_FILTER_MODE_LP)));
        filter->mode = (uint8_t)mode;
        filter->mode_xfade_remaining = FX_FILTER_MODE_XFADE_SAMPLES;
    }
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
    for(uint32_t i = 0U; i < frames; ++i)
    {
        const float dry_l = left[i];
        const float dry_r = right[i];
        const float wet_l = fx_filter_tpt_sample(dry_l, &filter->ic1eq_l, &filter->ic2eq_l,
                                                 &c, filter->mode, filter->previous_mode,
                                                 filter->mode_via_dry,
                                                 filter->mode_xfade_remaining);
        const float wet_r = fx_filter_tpt_sample(dry_r, &filter->ic1eq_r, &filter->ic2eq_r,
                                                 &c, filter->mode, filter->previous_mode,
                                                 filter->mode_via_dry,
                                                 filter->mode_xfade_remaining);
        left[i] = fx_filter_apply_bypass(dry_l, wet_l, &filter->bypass_mix,
                                         &filter->bypass_xfade_remaining, filter->bypass);
        right[i] = wet_r + ((dry_r - wet_r) * filter->bypass_mix);
        if(filter->mode_xfade_remaining != 0U) --filter->mode_xfade_remaining;
    }
    if(filter->mode_xfade_remaining == 0U) filter->previous_mode = filter->mode;
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
    filter->mode = (uint8_t)FX_BIQUAD_FILTER_MODE_LP;
    filter->previous_mode = filter->mode;
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
    if(filter != NULL) fx_filter_make_coeffs(filter->cutoff_hz, filter->q, &filter->current);
}

void fx_biquad_filter_mono_set_sample_rate(fx_biquad_filter_mono_t *filter, float sample_rate)
{
    if(filter == NULL) return;
    filter->sample_rate = (sample_rate > 1000.0f) ? sample_rate : FX_FILTER_DEFAULT_SR;
    fx_biquad_filter_mono_update_coeffs(filter);
}

void fx_biquad_filter_mono_set_mode(fx_biquad_filter_mono_t *filter, fx_biquad_filter_mode_t mode)
{
    if(filter == NULL) return;
    if(mode > FX_BIQUAD_FILTER_MODE_BP) mode = FX_BIQUAD_FILTER_MODE_LP;
    if(filter->mode != (uint8_t)mode)
    {
        filter->previous_mode = filter->mode;
        filter->mode_via_dry =
            (uint8_t)(((filter->mode == (uint8_t)FX_BIQUAD_FILTER_MODE_LP)
                    && (mode == FX_BIQUAD_FILTER_MODE_HP))
                   || ((filter->mode == (uint8_t)FX_BIQUAD_FILTER_MODE_HP)
                    && (mode == FX_BIQUAD_FILTER_MODE_LP)));
        filter->mode = (uint8_t)mode;
        filter->mode_xfade_remaining = FX_FILTER_MODE_XFADE_SAMPLES;
    }
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
    for(uint32_t i = 0U; i < frames; ++i)
    {
        const float dry = samples[i];
        const float wet = fx_filter_tpt_sample(dry, &filter->ic1eq, &filter->ic2eq,
                                               &c, filter->mode, filter->previous_mode,
                                               filter->mode_via_dry,
                                               filter->mode_xfade_remaining);
        samples[i] = fx_filter_apply_bypass(dry, wet, &filter->bypass_mix,
                                            &filter->bypass_xfade_remaining,
                                            filter->bypass);
        if(filter->mode_xfade_remaining != 0U) --filter->mode_xfade_remaining;
    }
    if(filter->mode_xfade_remaining == 0U) filter->previous_mode = filter->mode;
    if((filter->bypass != 0U) && (filter->bypass_xfade_remaining == 0U)
            && (filter->reset_after_bypass != 0U))
    {
        fx_filter_mono_reset_states(filter);
        filter->reset_after_bypass = 0U;
    }
}
