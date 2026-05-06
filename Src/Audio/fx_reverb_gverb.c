/*
 * GVerb-derived send reverb backend.
 *
 * Original source: gverb by Juhana Sadeharju, GPL-2.0-or-later.
 * This local port removes all dynamic allocation and uses repo memory sections.
 */

#include "fx_reverb_gverb.h"

#include "audio_float.h"
#include "Storage/memory_layout.h"

#include <math.h>
#include <string.h>

#define GVERB_FDN_ORDER 4U
#define GVERB_SAMPLE_RATE_DEFAULT 48000.0f
#define GVERB_MAX_ROOMSIZE_M 300.0f
#define GVERB_MIN_ROOMSIZE_M 5.0f
#define GVERB_UI_MAX_ROOMSIZE_M 120.0f
#define GVERB_FDN_DELAY_SIZE 43360U
#define GVERB_TAP_DELAY_SIZE 44000U
#define GVERB_DIFFUSER_SIZE 12288U

typedef struct
{
    uint32_t size;
    uint32_t idx;
    float *buf;
} gverb_fixeddelay_t;

typedef struct
{
    uint32_t size;
    float coeff;
    uint32_t idx;
    float *buf;
} gverb_diffuser_t;

typedef struct
{
    float damping;
    float delay;
} gverb_damper_t;

typedef struct
{
    float sample_rate;
    float wet;
    float size;
    float decay;
    float lpf;
    float input_bandwidth;
    float early_level;
    float tail_level;
    float roomsize_m;
    float revtime_s;
    float fdn_damping;
    float largest_delay;
    float alpha;
    gverb_damper_t input_damper;
    gverb_fixeddelay_t fdn_delays[GVERB_FDN_ORDER];
    gverb_damper_t fdn_dampers[GVERB_FDN_ORDER];
    uint32_t fdn_lens[GVERB_FDN_ORDER];
    float fdn_gains[GVERB_FDN_ORDER];
    gverb_diffuser_t l_diffusers[GVERB_FDN_ORDER];
    gverb_diffuser_t r_diffusers[GVERB_FDN_ORDER];
    gverb_fixeddelay_t tap_delay;
    uint32_t taps[GVERB_FDN_ORDER];
    float tap_gains[GVERB_FDN_ORDER];
    float d[GVERB_FDN_ORDER];
    float u[GVERB_FDN_ORDER];
    float f[GVERB_FDN_ORDER];
    uint8_t initialized;
} gverb_state_t;

AUDIO_COLD_SDRAM ALIGN32 static float g_gverb_fdn_delay_buffers[GVERB_FDN_ORDER][GVERB_FDN_DELAY_SIZE];
AUDIO_COLD_SDRAM ALIGN32 static float g_gverb_tap_delay_buffer[GVERB_TAP_DELAY_SIZE];
AUDIO_COLD_SDRAM ALIGN32 static float g_gverb_l_diffuser_buffers[GVERB_FDN_ORDER][GVERB_DIFFUSER_SIZE];
AUDIO_COLD_SDRAM ALIGN32 static float g_gverb_r_diffuser_buffers[GVERB_FDN_ORDER][GVERB_DIFFUSER_SIZE];
AUDIO_HOT static gverb_state_t g_gverb;

static inline float gverb_clamp01(float v)
{
    if(v < 0.0f)
        return 0.0f;
    if(v > 1.0f)
        return 1.0f;
    return v;
}

static inline float gverb_clampf(float v, float lo, float hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static inline uint32_t gverb_round_u32(float v)
{
    if(v <= 0.0f)
        return 0U;
    return (uint32_t)(v + 0.5f);
}

static inline float gverb_flush_to_zero(float v)
{
    return (fabsf(v) < 1.0e-20f) ? 0.0f : v;
}

static void fixeddelay_init(gverb_fixeddelay_t *d, float *buffer, uint32_t size)
{
    d->size = size;
    d->idx = 0U;
    d->buf = buffer;
}

static void fixeddelay_flush(gverb_fixeddelay_t *d)
{
    if((d == 0) || (d->buf == 0))
        return;
    memset(d->buf, 0, sizeof(float) * d->size);
    d->idx = 0U;
}

static inline float fixeddelay_read(const gverb_fixeddelay_t *d, uint32_t n)
{
    if((d == 0) || (d->buf == 0) || (d->size == 0U))
        return 0.0f;
    if(n >= d->size)
        n = d->size - 1U;
    const uint32_t idx = (d->idx + d->size - n) % d->size;
    return d->buf[idx];
}

static inline void fixeddelay_write(gverb_fixeddelay_t *d, float x)
{
    d->buf[d->idx] = x;
    d->idx++;
    if(d->idx >= d->size)
        d->idx = 0U;
}

static void diffuser_init(gverb_diffuser_t *d, float *buffer, uint32_t size, float coeff)
{
    d->size = (size < 1U) ? 1U : ((size > GVERB_DIFFUSER_SIZE) ? GVERB_DIFFUSER_SIZE : size);
    d->coeff = coeff;
    d->idx = 0U;
    d->buf = buffer;
}

static void diffuser_flush(gverb_diffuser_t *d)
{
    if((d == 0) || (d->buf == 0))
        return;
    memset(d->buf, 0, sizeof(float) * d->size);
    d->idx = 0U;
}

static inline float diffuser_do(gverb_diffuser_t *d, float x)
{
    const float w = gverb_flush_to_zero(x - (d->buf[d->idx] * d->coeff));
    const float y = d->buf[d->idx] + (w * d->coeff);
    d->buf[d->idx] = w;
    d->idx++;
    if(d->idx >= d->size)
        d->idx = 0U;
    return y;
}

static inline void damper_set(gverb_damper_t *d, float damping)
{
    d->damping = gverb_clamp01(damping);
}

static inline void damper_flush(gverb_damper_t *d)
{
    d->delay = 0.0f;
}

static inline float damper_do(gverb_damper_t *d, float x)
{
    const float y = (x * (1.0f - d->damping)) + (d->delay * d->damping);
    d->delay = y;
    return y;
}

static void gverb_fdnmatrix(const float *a, float *b)
{
    const float dl0 = a[0];
    const float dl1 = a[1];
    const float dl2 = a[2];
    const float dl3 = a[3];
    b[0] = 0.5f * (+dl0 + dl1 - dl2 - dl3);
    b[1] = 0.5f * (+dl0 - dl1 - dl2 + dl3);
    b[2] = 0.5f * (-dl0 + dl1 - dl2 + dl3);
    b[3] = 0.5f * (+dl0 + dl1 + dl2 + dl3);
}

static void gverb_update_room_dependent_params(void)
{
    g_gverb.largest_delay = g_gverb.sample_rate * g_gverb.roomsize_m / 340.0f;
    g_gverb.largest_delay = gverb_clampf(g_gverb.largest_delay, 1.0f, (float)(GVERB_FDN_DELAY_SIZE - 2U));

    g_gverb.fdn_lens[0] = gverb_round_u32(1.000000f * g_gverb.largest_delay);
    g_gverb.fdn_lens[1] = gverb_round_u32(0.816490f * g_gverb.largest_delay);
    g_gverb.fdn_lens[2] = gverb_round_u32(0.707100f * g_gverb.largest_delay);
    g_gverb.fdn_lens[3] = gverb_round_u32(0.632450f * g_gverb.largest_delay);

    for(uint32_t i = 0U; i < GVERB_FDN_ORDER; ++i)
    {
        if(g_gverb.fdn_lens[i] >= GVERB_FDN_DELAY_SIZE)
            g_gverb.fdn_lens[i] = GVERB_FDN_DELAY_SIZE - 1U;
        g_gverb.fdn_gains[i] = -powf(g_gverb.alpha, (float)g_gverb.fdn_lens[i]);
    }

    g_gverb.taps[0] = 5U + gverb_round_u32(0.410f * g_gverb.largest_delay);
    g_gverb.taps[1] = 5U + gverb_round_u32(0.300f * g_gverb.largest_delay);
    g_gverb.taps[2] = 5U + gverb_round_u32(0.155f * g_gverb.largest_delay);
    g_gverb.taps[3] = 5U;
    for(uint32_t i = 0U; i < GVERB_FDN_ORDER; ++i)
    {
        if(g_gverb.taps[i] >= GVERB_TAP_DELAY_SIZE)
            g_gverb.taps[i] = GVERB_TAP_DELAY_SIZE - 1U;
        g_gverb.tap_gains[i] = powf(g_gverb.alpha, (float)g_gverb.taps[i]);
    }
}

static void gverb_update_revtime(void)
{
    const float ga = powf(10.0f, -60.0f / 20.0f);
    const float n = gverb_clampf(g_gverb.sample_rate * g_gverb.revtime_s, 1.0f, 120.0f * g_gverb.sample_rate);
    g_gverb.alpha = powf(ga, 1.0f / n);
    for(uint32_t i = 0U; i < GVERB_FDN_ORDER; ++i)
        g_gverb.fdn_gains[i] = -powf(g_gverb.alpha, (float)g_gverb.fdn_lens[i]);
}

static void gverb_update_damping(void)
{
    for(uint32_t i = 0U; i < GVERB_FDN_ORDER; ++i)
        damper_set(&g_gverb.fdn_dampers[i], g_gverb.fdn_damping);
    damper_set(&g_gverb.input_damper, 1.0f - g_gverb.input_bandwidth);
}

static void gverb_apply_params(void)
{
    if(g_gverb.initialized == 0U)
        return;

    g_gverb.roomsize_m = GVERB_MIN_ROOMSIZE_M + ((GVERB_UI_MAX_ROOMSIZE_M - GVERB_MIN_ROOMSIZE_M) * g_gverb.size);
    g_gverb.revtime_s = 0.35f + (11.65f * g_gverb.decay);
    g_gverb.fdn_damping = 0.10f + (0.80f * g_gverb.lpf);
    g_gverb.input_bandwidth = 1.0f - (0.65f * g_gverb.lpf);
    g_gverb.early_level = 0.12f * g_gverb.wet;
    g_gverb.tail_level = 0.35f * g_gverb.wet;

    gverb_update_revtime();
    gverb_update_room_dependent_params();
    gverb_update_damping();
}

static void gverb_configure_diffusers(void)
{
    const float max_largest_delay = GVERB_SAMPLE_RATE_DEFAULT * GVERB_MAX_ROOMSIZE_M / 340.0f;
    const float diffscale = (0.632450f * max_largest_delay) / (210.0f + 159.0f + 562.0f + 410.0f);
    const float spread1 = 15.0f;
    const float spread2 = 45.0f;

    const float lb = 210.0f;
    const float lc = 210.0f + 159.0f + (spread1 * 0.125541f);
    const float ld = 210.0f + 159.0f + 562.0f + (spread2 * 0.854046f);
    diffuser_init(&g_gverb.l_diffusers[0], g_gverb_l_diffuser_buffers[0], gverb_round_u32(diffscale * lb), 0.75f);
    diffuser_init(&g_gverb.l_diffusers[1], g_gverb_l_diffuser_buffers[1], gverb_round_u32(diffscale * (lc - lb)), 0.75f);
    diffuser_init(&g_gverb.l_diffusers[2], g_gverb_l_diffuser_buffers[2], gverb_round_u32(diffscale * (ld - lc)), 0.625f);
    diffuser_init(&g_gverb.l_diffusers[3], g_gverb_l_diffuser_buffers[3], gverb_round_u32(diffscale * (1341.0f - ld)), 0.625f);

    const float rb = 210.0f;
    const float rc = 210.0f + 159.0f + (spread1 * -0.568366f);
    const float rd = 210.0f + 159.0f + 562.0f + (spread2 * -0.126815f);
    diffuser_init(&g_gverb.r_diffusers[0], g_gverb_r_diffuser_buffers[0], gverb_round_u32(diffscale * rb), 0.75f);
    diffuser_init(&g_gverb.r_diffusers[1], g_gverb_r_diffuser_buffers[1], gverb_round_u32(diffscale * (rc - rb)), 0.75f);
    diffuser_init(&g_gverb.r_diffusers[2], g_gverb_r_diffuser_buffers[2], gverb_round_u32(diffscale * (rd - rc)), 0.625f);
    diffuser_init(&g_gverb.r_diffusers[3], g_gverb_r_diffuser_buffers[3], gverb_round_u32(diffscale * (1341.0f - rd)), 0.625f);
}

static void gverb_process_sample(float input, float *out_l, float *out_r)
{
    if((input != input) || (fabsf(input) > 100000.0f))
        input = 0.0f;

    float z = damper_do(&g_gverb.input_damper, input);
    z = diffuser_do(&g_gverb.l_diffusers[0], z);

    for(uint32_t i = 0U; i < GVERB_FDN_ORDER; ++i)
        g_gverb.u[i] = g_gverb.tap_gains[i] * fixeddelay_read(&g_gverb.tap_delay, g_gverb.taps[i]);
    fixeddelay_write(&g_gverb.tap_delay, z);

    for(uint32_t i = 0U; i < GVERB_FDN_ORDER; ++i)
    {
        const float delayed = fixeddelay_read(&g_gverb.fdn_delays[i], g_gverb.fdn_lens[i]);
        g_gverb.d[i] = damper_do(&g_gverb.fdn_dampers[i], g_gverb.fdn_gains[i] * delayed);
    }

    float sum = 0.0f;
    float sign = 1.0f;
    for(uint32_t i = 0U; i < GVERB_FDN_ORDER; ++i)
    {
        sum += sign * ((g_gverb.tail_level * g_gverb.d[i]) + (g_gverb.early_level * g_gverb.u[i]));
        sign = -sign;
    }
    sum += input * g_gverb.early_level;

    gverb_fdnmatrix(g_gverb.d, g_gverb.f);

    for(uint32_t i = 0U; i < GVERB_FDN_ORDER; ++i)
        fixeddelay_write(&g_gverb.fdn_delays[i], g_gverb.u[i] + g_gverb.f[i]);

    float lsum = sum;
    float rsum = sum;
    lsum = diffuser_do(&g_gverb.l_diffusers[1], lsum);
    lsum = diffuser_do(&g_gverb.l_diffusers[2], lsum);
    lsum = diffuser_do(&g_gverb.l_diffusers[3], lsum);
    rsum = diffuser_do(&g_gverb.r_diffusers[1], rsum);
    rsum = diffuser_do(&g_gverb.r_diffusers[2], rsum);
    rsum = diffuser_do(&g_gverb.r_diffusers[3], rsum);

    *out_l = gverb_clampf(gverb_flush_to_zero(lsum), -4.0f, 4.0f);
    *out_r = gverb_clampf(gverb_flush_to_zero(rsum), -4.0f, 4.0f);
}

void fx_reverb_gverb_global_init(float sample_rate)
{
    memset(&g_gverb, 0, sizeof(g_gverb));
    g_gverb.sample_rate = (sample_rate > 0.0f) ? sample_rate : GVERB_SAMPLE_RATE_DEFAULT;
    g_gverb.wet = 0.0f;
    g_gverb.size = 0.0f;
    g_gverb.decay = 0.5f;
    g_gverb.lpf = 0.0f;

    for(uint32_t i = 0U; i < GVERB_FDN_ORDER; ++i)
        fixeddelay_init(&g_gverb.fdn_delays[i], g_gverb_fdn_delay_buffers[i], GVERB_FDN_DELAY_SIZE);
    fixeddelay_init(&g_gverb.tap_delay, g_gverb_tap_delay_buffer, GVERB_TAP_DELAY_SIZE);
    gverb_configure_diffusers();
    g_gverb.initialized = 1U;
    fx_reverb_gverb_global_reset();
    gverb_apply_params();
}

void fx_reverb_gverb_global_reset(void)
{
    if(g_gverb.initialized == 0U)
        return;

    for(uint32_t i = 0U; i < GVERB_FDN_ORDER; ++i)
    {
        fixeddelay_flush(&g_gverb.fdn_delays[i]);
        damper_flush(&g_gverb.fdn_dampers[i]);
        diffuser_flush(&g_gverb.l_diffusers[i]);
        diffuser_flush(&g_gverb.r_diffusers[i]);
    }
    fixeddelay_flush(&g_gverb.tap_delay);
    damper_flush(&g_gverb.input_damper);
    memset(g_gverb.d, 0, sizeof(g_gverb.d));
    memset(g_gverb.u, 0, sizeof(g_gverb.u));
    memset(g_gverb.f, 0, sizeof(g_gverb.f));
}

void fx_reverb_gverb_global_set_wet(float wet)
{
    g_gverb.wet = gverb_clamp01(wet);
    gverb_apply_params();
}

void fx_reverb_gverb_global_set_size(float size)
{
    g_gverb.size = gverb_clamp01(size);
    gverb_apply_params();
}

void fx_reverb_gverb_global_set_decay(float decay)
{
    g_gverb.decay = gverb_clamp01(decay);
    gverb_apply_params();
}

void fx_reverb_gverb_global_set_lpf(float lpf)
{
    g_gverb.lpf = gverb_clamp01(lpf);
    gverb_apply_params();
}

void fx_reverb_gverb_global_process_send_mono_to_stereo_wet(const float *in,
                                                             float *out_l,
                                                             float *out_r,
                                                             uint32_t frames)
{
    if((in == 0) || (out_l == 0) || (out_r == 0))
        return;

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    if((g_gverb.initialized == 0U) || (g_gverb.wet <= 0.0f))
    {
        memset(out_l, 0, sizeof(float) * frames);
        memset(out_r, 0, sizeof(float) * frames);
        return;
    }

    for(uint32_t i = 0U; i < frames; ++i)
        gverb_process_sample(in[i], &out_l[i], &out_r[i]);
}
