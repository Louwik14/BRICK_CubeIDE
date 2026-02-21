#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    float z1;
    float z2;
} fx_biquad_t;

void fx_biquad_reset(fx_biquad_t *bq);
void fx_biquad_set_identity(fx_biquad_t *bq);

void fx_biquad_set_low_shelf(fx_biquad_t *bq,
                             float sample_rate_hz,
                             float freq_hz,
                             float gain_db,
                             float shelf_slope);

void fx_biquad_set_peaking(fx_biquad_t *bq,
                           float sample_rate_hz,
                           float freq_hz,
                           float gain_db,
                           float q);

void fx_biquad_set_high_shelf(fx_biquad_t *bq,
                              float sample_rate_hz,
                              float freq_hz,
                              float gain_db,
                              float shelf_slope);

static inline float fx_biquad_process(fx_biquad_t *bq, float x)
{
    const float y = bq->b0 * x + bq->z1;
    bq->z1 = bq->b1 * x - bq->a1 * y + bq->z2;
    bq->z2 = bq->b2 * x - bq->a2 * y;
    return y;
}

#ifdef __cplusplus
}
#endif
