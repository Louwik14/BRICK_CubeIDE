#include "fx_biquad.h"
#include <math.h>

#define FX_BIQUAD_PI_F      3.14159265358979323846f
#define FX_BIQUAD_MIN_A0_F  1.0e-12f

static float fx_biquad_clamp_freq(float freq_hz, float sample_rate_hz)
{
    const float nyquist = 0.5f * sample_rate_hz;

    if(freq_hz < 1.0f)
        freq_hz = 1.0f;
    if(freq_hz > (nyquist - 1.0f))
        freq_hz = nyquist - 1.0f;

    return freq_hz;
}

static void fx_biquad_normalize(fx_biquad_t *bq,
                                float b0,
                                float b1,
                                float b2,
                                float a0,
                                float a1,
                                float a2)
{
    if(fabsf(a0) < FX_BIQUAD_MIN_A0_F)
        a0 = (a0 < 0.0f) ? -FX_BIQUAD_MIN_A0_F : FX_BIQUAD_MIN_A0_F;

    const float inv_a0 = 1.0f / a0;
    bq->b0 = b0 * inv_a0;
    bq->b1 = b1 * inv_a0;
    bq->b2 = b2 * inv_a0;
    bq->a1 = a1 * inv_a0;
    bq->a2 = a2 * inv_a0;
}

void fx_biquad_reset(fx_biquad_t *bq)
{
    if(!bq)
        return;

    bq->z1 = 0.0f;
    bq->z2 = 0.0f;
}

void fx_biquad_set_identity(fx_biquad_t *bq)
{
    if(!bq)
        return;

    bq->b0 = 1.0f;
    bq->b1 = 0.0f;
    bq->b2 = 0.0f;
    bq->a1 = 0.0f;
    bq->a2 = 0.0f;
}

void fx_biquad_set_low_shelf(fx_biquad_t *bq,
                             float sample_rate_hz,
                             float freq_hz,
                             float gain_db,
                             float shelf_slope)
{
    if(!bq)
        return;

    if(sample_rate_hz <= 0.0f)
        sample_rate_hz = 48000.0f;
    if(shelf_slope <= 0.0f)
        shelf_slope = 1.0f;

    freq_hz = fx_biquad_clamp_freq(freq_hz, sample_rate_hz);

    const float a = powf(10.0f, gain_db * 0.025f);
    const float w0 = 2.0f * FX_BIQUAD_PI_F * (freq_hz / sample_rate_hz);
    const float c = cosf(w0);
    const float s = sinf(w0);
    const float beta = sqrtf(a) / shelf_slope;
    const float alpha = 0.5f * s * sqrtf((a + (1.0f / a)) * (1.0f / shelf_slope - 1.0f) + 2.0f);
    const float a1 = (a + 1.0f);
    const float a2 = (a - 1.0f);

    const float b0 = a * (a1 - a2 * c + 2.0f * beta * alpha);
    const float b1 = 2.0f * a * (a2 - a1 * c);
    const float b2 = a * (a1 - a2 * c - 2.0f * beta * alpha);
    const float d0 = a1 + a2 * c + 2.0f * beta * alpha;
    const float d1 = -2.0f * (a2 + a1 * c);
    const float d2 = a1 + a2 * c - 2.0f * beta * alpha;

    fx_biquad_normalize(bq, b0, b1, b2, d0, d1, d2);
}

void fx_biquad_set_peaking(fx_biquad_t *bq,
                           float sample_rate_hz,
                           float freq_hz,
                           float gain_db,
                           float q)
{
    if(!bq)
        return;

    if(sample_rate_hz <= 0.0f)
        sample_rate_hz = 48000.0f;
    if(q <= 0.0f)
        q = 1.0f;

    freq_hz = fx_biquad_clamp_freq(freq_hz, sample_rate_hz);

    const float a = powf(10.0f, gain_db * 0.025f);
    const float w0 = 2.0f * FX_BIQUAD_PI_F * (freq_hz / sample_rate_hz);
    const float c = cosf(w0);
    const float s = sinf(w0);
    const float alpha = s / (2.0f * q);

    const float b0 = 1.0f + alpha * a;
    const float b1 = -2.0f * c;
    const float b2 = 1.0f - alpha * a;
    const float a0 = 1.0f + alpha / a;
    const float a1 = -2.0f * c;
    const float a2 = 1.0f - alpha / a;

    fx_biquad_normalize(bq, b0, b1, b2, a0, a1, a2);
}

void fx_biquad_set_high_shelf(fx_biquad_t *bq,
                              float sample_rate_hz,
                              float freq_hz,
                              float gain_db,
                              float shelf_slope)
{
    if(!bq)
        return;

    if(sample_rate_hz <= 0.0f)
        sample_rate_hz = 48000.0f;
    if(shelf_slope <= 0.0f)
        shelf_slope = 1.0f;

    freq_hz = fx_biquad_clamp_freq(freq_hz, sample_rate_hz);

    const float a = powf(10.0f, gain_db * 0.025f);
    const float w0 = 2.0f * FX_BIQUAD_PI_F * (freq_hz / sample_rate_hz);
    const float c = cosf(w0);
    const float s = sinf(w0);
    const float beta = sqrtf(a) / shelf_slope;
    const float alpha = 0.5f * s * sqrtf((a + (1.0f / a)) * (1.0f / shelf_slope - 1.0f) + 2.0f);
    const float a1 = (a + 1.0f);
    const float a2 = (a - 1.0f);

    const float b0 = a * (a1 + a2 * c + 2.0f * beta * alpha);
    const float b1 = -2.0f * a * (a2 + a1 * c);
    const float b2 = a * (a1 + a2 * c - 2.0f * beta * alpha);
    const float d0 = a1 - a2 * c + 2.0f * beta * alpha;
    const float d1 = 2.0f * (a2 - a1 * c);
    const float d2 = a1 - a2 * c - 2.0f * beta * alpha;

    fx_biquad_normalize(bq, b0, b1, b2, d0, d1, d2);
}
