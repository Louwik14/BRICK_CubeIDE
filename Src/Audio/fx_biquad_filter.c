/**
 * @file fx_biquad_filter.c
 * @brief Peaks SVF multimode stéréo (remplacement runtime de l'ancien biquad).
 */

#include "fx_biquad_filter.h"
#include <math.h>
#include <string.h>

#include "fx_peaks_svf_lut.h"

#define FX_BIQUAD_FILTER_MIN_FREQ   20.0f
#define FX_BIQUAD_FILTER_MAX_FREQ   20000.0f
#define FX_BIQUAD_FILTER_MIN_Q      0.70710678f
#define FX_BIQUAD_FILTER_MAX_Q      12.0f
#define FX_BIQUAD_FILTER_DEFAULT_Q  0.70710678f
#define FX_BIQUAD_FILTER_DEFAULT_SR 48000.0f

static inline float fx_biquad_filter_clamp(float x, float lo, float hi)
{
    if(x < lo) return lo;
    if(x > hi) return hi;
    return x;
}

static inline int32_t fx_biquad_filter_clip16(int32_t x)
{
    if(x < -32767) return -32767;
    if(x > 32767) return 32767;
    return x;
}

static inline int32_t fx_biquad_filter_clip16_mono(int32_t x)
{
    if(x < -32767) return -32767;
    if(x > 32767) return 32767;
    return x;
}

static inline uint16_t fx_biquad_filter_interpolate824(const uint16_t *table, uint32_t phase)
{
    const uint16_t a = table[(phase >> 24) & 0xffU];
    const uint16_t b = table[((phase >> 24) & 0xffU) + 1U];
    return a + (((b - a) * ((phase >> 8) & 0xffffU)) >> 16);
}

enum
{
    FX_BIQUAD_FILTER_LOG2_LUT_SIZE = 256U
};

#define FX_BIQUAD_FILTER_INV_LOG2 1.4426950408889634f
#define FX_BIQUAD_FILTER_LOG2_440 8.7813597135246596f

static float g_fx_biquad_filter_log2_lut[FX_BIQUAD_FILTER_LOG2_LUT_SIZE + 1U];
static uint8_t g_fx_biquad_filter_log2_lut_ready = 0U;

static void fx_biquad_filter_init_log2_lut(void)
{
    if(g_fx_biquad_filter_log2_lut_ready != 0U)
    {
        return;
    }

    for(uint32_t i = 0U; i <= FX_BIQUAD_FILTER_LOG2_LUT_SIZE; ++i)
    {
        const float x = 1.0f + ((float)i * (1.0f / (float)FX_BIQUAD_FILTER_LOG2_LUT_SIZE));
        g_fx_biquad_filter_log2_lut[i] = logf(x) * FX_BIQUAD_FILTER_INV_LOG2;
    }

    g_fx_biquad_filter_log2_lut_ready = 1U;
}

static float fx_biquad_filter_log2_lut(float x)
{
    union
    {
        float f;
        uint32_t u;
    } bits;

    bits.f = fx_biquad_filter_clamp(x, 1.0e-20f, 1.0e20f);
    const int32_t exponent = (int32_t)((bits.u >> 23) & 0xffU) - 127;
    const uint32_t mantissa_bits = bits.u & 0x7fffffU;
    uint32_t index = mantissa_bits >> 15;
    if(index >= FX_BIQUAD_FILTER_LOG2_LUT_SIZE)
    {
        index = FX_BIQUAD_FILTER_LOG2_LUT_SIZE - 1U;
    }

    const float frac = (float)(mantissa_bits & 0x7fffU) * (1.0f / 32768.0f);
    const float a = g_fx_biquad_filter_log2_lut[index];
    const float b = g_fx_biquad_filter_log2_lut[index + 1U];
    return (float)exponent + a + ((b - a) * frac);
}

static inline int16_t fx_biquad_filter_cutoff_to_peaks_frequency(float cutoff_hz, float sample_rate)
{
    const float sr = (sample_rate > 1000.0f) ? sample_rate : FX_BIQUAD_FILTER_DEFAULT_SR;
    const float nyquist_margin = sr * 0.49f;
    const float cutoff = fx_biquad_filter_clamp(cutoff_hz, FX_BIQUAD_FILTER_MIN_FREQ, nyquist_margin);

    const float normalized_to_48k = cutoff * (FX_BIQUAD_FILTER_DEFAULT_SR / sr);
    const float safe_hz = fx_biquad_filter_clamp(normalized_to_48k, 8.1757989156f, FX_BIQUAD_FILTER_MAX_FREQ);
    const float midi_note = 69.0f + (12.0f * (fx_biquad_filter_log2_lut(safe_hz) - FX_BIQUAD_FILTER_LOG2_440));
    const float q15 = fx_biquad_filter_clamp(midi_note * 128.0f, 0.0f, 32767.0f);
    return (int16_t)(q15 + 0.5f);
}

static inline int16_t fx_biquad_filter_q_to_peaks_resonance(float q)
{
    const float q_clamped = fx_biquad_filter_clamp(q, FX_BIQUAD_FILTER_MIN_Q, FX_BIQUAD_FILTER_MAX_Q);
    const float resonance = (q_clamped - FX_BIQUAD_FILTER_MIN_Q) / (FX_BIQUAD_FILTER_MAX_Q - FX_BIQUAD_FILTER_MIN_Q);
    const float q15 = fx_biquad_filter_clamp(resonance * 32767.0f, 0.0f, 32767.0f);
    return (int16_t)(q15 + 0.5f);
}

void fx_biquad_filter_init(fx_biquad_filter_t *filter, float sample_rate)
{
    fx_biquad_filter_init_log2_lut();
    if(filter == NULL) return;

    memset(filter, 0, sizeof(*filter));
    filter->sample_rate = (sample_rate > 0.0f) ? sample_rate : FX_BIQUAD_FILTER_DEFAULT_SR;
    filter->cutoff_hz = 16000.0f;
    filter->q = FX_BIQUAD_FILTER_DEFAULT_Q;
    filter->mode = (uint8_t)FX_BIQUAD_FILTER_MODE_LP;
    filter->bypass = 1U;

    fx_biquad_filter_update_coeffs(filter);
    filter->coeffs_pending_update = 0U;
}

void fx_biquad_filter_reset(fx_biquad_filter_t *filter)
{
    if(filter == NULL) return;
    filter->lp_l = 0;
    filter->bp_l = 0;
    filter->lp_r = 0;
    filter->bp_r = 0;
}

void fx_biquad_filter_update_coeffs(fx_biquad_filter_t *filter)
{
    if(filter == NULL) return;

    filter->frequency_q15 = fx_biquad_filter_cutoff_to_peaks_frequency(filter->cutoff_hz, filter->sample_rate);
    filter->resonance_q15 = fx_biquad_filter_q_to_peaks_resonance(filter->q);
    filter->f_q15 = (int32_t)fx_biquad_filter_interpolate824(fx_peaks_lut_svf_cutoff,
                                                              ((uint32_t)(uint16_t)filter->frequency_q15) << 17);
    filter->damp_q15 = (int32_t)fx_biquad_filter_interpolate824(fx_peaks_lut_svf_damp,
                                                                 ((uint32_t)(uint16_t)filter->resonance_q15) << 17);
    filter->coeffs_pending_update = 1U;
}

void fx_biquad_filter_set_sample_rate(fx_biquad_filter_t *filter, float sample_rate)
{
    if(filter == NULL) return;
    filter->sample_rate = (sample_rate > 0.0f) ? sample_rate : FX_BIQUAD_FILTER_DEFAULT_SR;
    fx_biquad_filter_update_coeffs(filter);
}

void fx_biquad_filter_set_mode(fx_biquad_filter_t *filter, fx_biquad_filter_mode_t mode)
{
    if(filter == NULL) return;
    filter->mode = (uint8_t)mode;
}

void fx_biquad_filter_set_cutoff(fx_biquad_filter_t *filter, float cutoff_hz)
{
    if(filter == NULL) return;
    filter->cutoff_hz = cutoff_hz;

    const int16_t frequency_q15 = fx_biquad_filter_cutoff_to_peaks_frequency(filter->cutoff_hz, filter->sample_rate);
    if(frequency_q15 == filter->frequency_q15)
    {
        return;
    }

    filter->frequency_q15 = frequency_q15;
    filter->f_q15 = (int32_t)fx_biquad_filter_interpolate824(fx_peaks_lut_svf_cutoff,
                                                             ((uint32_t)(uint16_t)filter->frequency_q15) << 17);
    filter->coeffs_pending_update = 1U;
}

void fx_biquad_filter_set_q(fx_biquad_filter_t *filter, float q)
{
    if(filter == NULL) return;
    filter->q = q;

    const int16_t resonance_q15 = fx_biquad_filter_q_to_peaks_resonance(filter->q);
    if(resonance_q15 == filter->resonance_q15)
    {
        return;
    }

    filter->resonance_q15 = resonance_q15;
    filter->damp_q15 = (int32_t)fx_biquad_filter_interpolate824(fx_peaks_lut_svf_damp,
                                                                ((uint32_t)(uint16_t)filter->resonance_q15) << 17);
    filter->coeffs_pending_update = 1U;
}

void fx_biquad_filter_set_bypass(fx_biquad_filter_t *filter, uint8_t bypass)
{
    if(filter == NULL) return;
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

    filter->coeffs_pending_update = 0U;

    const int32_t f = filter->f_q15;
    const int32_t damp = filter->damp_q15;

    for(uint32_t i = 0U; i < block_size; ++i)
    {
        int32_t in_l = (int32_t)(inout_l[i] * 32767.0f);
        int32_t notch_l = in_l - ((filter->bp_l * damp) >> 15);
        filter->lp_l += (f * filter->bp_l) >> 15;
        filter->lp_l = fx_biquad_filter_clip16(filter->lp_l);
        int32_t hp_l = notch_l - filter->lp_l;
        filter->bp_l += (f * hp_l) >> 15;
        filter->bp_l = fx_biquad_filter_clip16(filter->bp_l);

        int32_t out_l = filter->lp_l;
        if(filter->mode == (uint8_t)FX_BIQUAD_FILTER_MODE_HP) out_l = hp_l;
        else if(filter->mode == (uint8_t)FX_BIQUAD_FILTER_MODE_BP) out_l = filter->bp_l;
        inout_l[i] = (float)out_l * (1.0f / 32767.0f);

        int32_t in_r = (int32_t)(inout_r[i] * 32767.0f);
        int32_t notch_r = in_r - ((filter->bp_r * damp) >> 15);
        filter->lp_r += (f * filter->bp_r) >> 15;
        filter->lp_r = fx_biquad_filter_clip16(filter->lp_r);
        int32_t hp_r = notch_r - filter->lp_r;
        filter->bp_r += (f * hp_r) >> 15;
        filter->bp_r = fx_biquad_filter_clip16(filter->bp_r);

        int32_t out_r = filter->lp_r;
        if(filter->mode == (uint8_t)FX_BIQUAD_FILTER_MODE_HP) out_r = hp_r;
        else if(filter->mode == (uint8_t)FX_BIQUAD_FILTER_MODE_BP) out_r = filter->bp_r;
        inout_r[i] = (float)out_r * (1.0f / 32767.0f);
    }
}

static inline int32_t fx_biquad_filter_process_sample_mono(fx_biquad_filter_mono_t *filter, float input)
{
    const int32_t in = (int32_t)(input * 32767.0f);
    const int32_t notch = in - ((filter->bp * filter->damp_q15) >> 15);
    filter->lp += (filter->f_q15 * filter->bp) >> 15;
    filter->lp = fx_biquad_filter_clip16_mono(filter->lp);
    const int32_t hp = notch - filter->lp;
    filter->bp += (filter->f_q15 * hp) >> 15;
    filter->bp = fx_biquad_filter_clip16_mono(filter->bp);

    if(filter->mode == (uint8_t)FX_BIQUAD_FILTER_MODE_HP)
    {
        return hp;
    }
    if(filter->mode == (uint8_t)FX_BIQUAD_FILTER_MODE_BP)
    {
        return filter->bp;
    }
    return filter->lp;
}

void fx_biquad_filter_mono_init(fx_biquad_filter_mono_t *filter, float sample_rate)
{
    fx_biquad_filter_init_log2_lut();
    if(filter == NULL) return;

    memset(filter, 0, sizeof(*filter));
    filter->sample_rate = (sample_rate > 0.0f) ? sample_rate : FX_BIQUAD_FILTER_DEFAULT_SR;
    filter->cutoff_hz = 16000.0f;
    filter->q = FX_BIQUAD_FILTER_DEFAULT_Q;
    filter->mode = (uint8_t)FX_BIQUAD_FILTER_MODE_LP;
    filter->bypass = 1U;

    fx_biquad_filter_mono_update_coeffs(filter);
    filter->coeffs_pending_update = 0U;
}

void fx_biquad_filter_mono_reset(fx_biquad_filter_mono_t *filter)
{
    if(filter == NULL) return;
    filter->lp = 0;
    filter->bp = 0;
}

void fx_biquad_filter_mono_update_coeffs(fx_biquad_filter_mono_t *filter)
{
    if(filter == NULL) return;

    filter->frequency_q15 = fx_biquad_filter_cutoff_to_peaks_frequency(filter->cutoff_hz, filter->sample_rate);
    filter->resonance_q15 = fx_biquad_filter_q_to_peaks_resonance(filter->q);
    filter->f_q15 = (int32_t)fx_biquad_filter_interpolate824(fx_peaks_lut_svf_cutoff,
                                                              ((uint32_t)(uint16_t)filter->frequency_q15) << 17);
    filter->damp_q15 = (int32_t)fx_biquad_filter_interpolate824(fx_peaks_lut_svf_damp,
                                                                 ((uint32_t)(uint16_t)filter->resonance_q15) << 17);
    filter->coeffs_pending_update = 1U;
}

void fx_biquad_filter_mono_set_sample_rate(fx_biquad_filter_mono_t *filter, float sample_rate)
{
    if(filter == NULL) return;
    filter->sample_rate = (sample_rate > 0.0f) ? sample_rate : FX_BIQUAD_FILTER_DEFAULT_SR;
    fx_biquad_filter_mono_update_coeffs(filter);
}

void fx_biquad_filter_mono_set_mode(fx_biquad_filter_mono_t *filter, fx_biquad_filter_mode_t mode)
{
    if(filter == NULL) return;
    filter->mode = (uint8_t)mode;
}

void fx_biquad_filter_mono_set_cutoff(fx_biquad_filter_mono_t *filter, float cutoff_hz)
{
    if(filter == NULL) return;
    filter->cutoff_hz = cutoff_hz;

    const int16_t frequency_q15 = fx_biquad_filter_cutoff_to_peaks_frequency(filter->cutoff_hz, filter->sample_rate);
    if(frequency_q15 == filter->frequency_q15)
    {
        return;
    }

    filter->frequency_q15 = frequency_q15;
    filter->f_q15 = (int32_t)fx_biquad_filter_interpolate824(fx_peaks_lut_svf_cutoff,
                                                             ((uint32_t)(uint16_t)filter->frequency_q15) << 17);
    filter->coeffs_pending_update = 1U;
}

void fx_biquad_filter_mono_set_q(fx_biquad_filter_mono_t *filter, float q)
{
    if(filter == NULL) return;
    filter->q = q;

    const int16_t resonance_q15 = fx_biquad_filter_q_to_peaks_resonance(filter->q);
    if(resonance_q15 == filter->resonance_q15)
    {
        return;
    }

    filter->resonance_q15 = resonance_q15;
    filter->damp_q15 = (int32_t)fx_biquad_filter_interpolate824(fx_peaks_lut_svf_damp,
                                                                ((uint32_t)(uint16_t)filter->resonance_q15) << 17);
    filter->coeffs_pending_update = 1U;
}

void fx_biquad_filter_mono_set_bypass(fx_biquad_filter_mono_t *filter, uint8_t bypass)
{
    if(filter == NULL) return;
    filter->bypass = (bypass != 0U) ? 1U : 0U;
}

void fx_biquad_filter_mono_process_block(fx_biquad_filter_mono_t *filter,
                                         float *inout,
                                         uint32_t block_size)
{
    if((filter == NULL) || (inout == NULL) || (block_size == 0U) || (filter->bypass != 0U))
    {
        return;
    }

    filter->coeffs_pending_update = 0U;

    for(uint32_t i = 0U; i < block_size; ++i)
    {
        const int32_t out = fx_biquad_filter_process_sample_mono(filter, inout[i]);
        inout[i] = (float)out * (1.0f / 32767.0f);
    }
}
