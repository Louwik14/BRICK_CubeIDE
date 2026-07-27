#include "fx_delay_dual.h"

#include "Audio/fx_delay_shared_pool.h"
#include "Storage/memory_layout.h"

#include <math.h>
#include <string.h>

namespace
{
constexpr float kDefaultSampleRate = 48000.0f;
constexpr float kMaxDelaySeconds = 6.0f;
constexpr uint32_t kDelayBufferSize = FX_DELAY_SHARED_CAPACITY;
constexpr uint32_t kHaasBufferSize = 2402U;
constexpr float kMinDelaySamples = 4.0f;
constexpr float kTimeSmoothSeconds = 0.15f;
constexpr float kMaxHaasMs = 24.0f;
constexpr float kNeutralEpsilon = 1.0e-6f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kInvSqrt2 = 0.70710678118f;

AUDIO_HOT ALIGN32 static float g_haas_l[kHaasBufferSize];
AUDIO_HOT ALIGN32 static float g_haas_r[kHaasBufferSize];

static inline float clampf_local(float v, float lo, float hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static inline uint8_t is_finite_sample(float v)
{
    return isfinite(v) ? 1U : 0U;
}

static inline float sanitize_feedback_sample(float v)
{
    if(is_finite_sample(v) == 0U)
        return 0.0f;
    return clampf_local(v, -8.0f, 8.0f);
}

static inline float sanitize_output_sample(float v)
{
    if(is_finite_sample(v) == 0U)
        return 0.0f;
    return clampf_local(v, -4.0f, 4.0f);
}

typedef struct
{
    float *buffer;
    uint32_t capacity;
    uint32_t size;
    uint32_t pos;
} delay_line_t;

static void delay_line_init(delay_line_t *line, float *buffer, uint32_t capacity)
{
    line->buffer = buffer;
    line->capacity = capacity;
    line->size = capacity;
    line->pos = 0U;
}

static void delay_line_clear(delay_line_t *line)
{
    if((line == 0) || (line->buffer == 0))
        return;
    memset(line->buffer, 0, sizeof(float) * line->capacity);
    line->pos = 0U;
}

static void delay_line_resize(delay_line_t *line, uint32_t new_size)
{
    if(line == 0)
        return;
    if(new_size < 1U)
        new_size = 1U;
    if(new_size > line->capacity)
        new_size = line->capacity;
    line->size = new_size;
    if(line->pos >= line->size)
        line->pos %= line->size;
}

static inline float delay_line_read3(const delay_line_t *line, float delay)
{
    if((line == 0) || (line->buffer == 0) || (line->size == 0U))
        return 0.0f;

    delay = clampf_local(delay, 1.0f, (float)(line->size - 1U));
    float read_pos = (float)line->pos - delay;
    while(read_pos < 0.0f)
        read_pos += (float)line->size;
    while(read_pos >= (float)line->size)
        read_pos -= (float)line->size;

    const uint32_t i1 = (uint32_t)floorf(read_pos);
    const uint32_t i0 = (i1 == 0U) ? (line->size - 1U) : (i1 - 1U);
    const uint32_t i2 = (i1 + 1U >= line->size) ? 0U : (i1 + 1U);
    const uint32_t i3 = (i2 + 1U >= line->size) ? 0U : (i2 + 1U);
    const float frac = read_pos - (float)i1;

    const float y0 = line->buffer[i0];
    const float y1 = line->buffer[i1];
    const float y2 = line->buffer[i2];
    const float y3 = line->buffer[i3];

    const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float a2 = -0.5f * y0 + 0.5f * y2;
    const float a3 = y1;
    return ((a0 * frac + a1) * frac + a2) * frac + a3;
}

static inline float delay_line_read_linear(const delay_line_t *line, float delay)
{
    if((line == 0) || (line->buffer == 0) || (line->size == 0U))
        return 0.0f;

    delay = clampf_local(delay, 1.0f, (float)(line->size - 1U));
    float read_pos = (float)line->pos - delay;
    while(read_pos < 0.0f)
        read_pos += (float)line->size;
    while(read_pos >= (float)line->size)
        read_pos -= (float)line->size;

    const uint32_t i0 = (uint32_t)read_pos;
    const uint32_t i1 = (i0 + 1U >= line->size) ? 0U : (i0 + 1U);
    const float frac = read_pos - (float)i0;
    return line->buffer[i0] + ((line->buffer[i1] - line->buffer[i0]) * frac);
}

static inline void delay_line_write(delay_line_t *line, float sample, uint8_t overdub)
{
    if((line == 0) || (line->buffer == 0) || (line->size == 0U))
        return;
    if(overdub != 0U)
        line->buffer[line->pos] += sample;
    else
        line->buffer[line->pos] = sample;
    line->pos++;
    if(line->pos >= line->size)
        line->pos = 0U;
}

typedef struct
{
    float hp_state;
    float hp_prev_input;
    float lp_state;
} feedback_filter_t;

static inline float hpf_alpha(float hpf)
{
    const float t = clampf_local(hpf, 0.0f, 1.0f);
    return 0.997f - (0.965f * t * t);
}

static inline float lpf_alpha(float lpf)
{
    const float t = 1.0f - clampf_local(lpf, 0.0f, 1.0f);
    return 0.005f + (0.80f * t * t);
}

static inline float feedback_filter_process(feedback_filter_t *filter,
                                            float input,
                                            uint8_t hpf_active,
                                            uint8_t lpf_active,
                                            float hp_a,
                                            float lp_a)
{
    float y = input;
    if(hpf_active != 0U)
    {
        y = hp_a * (filter->hp_state + input - filter->hp_prev_input);
        filter->hp_state = y;
        filter->hp_prev_input = input;
    }
    else
    {
        filter->hp_state = input;
        filter->hp_prev_input = input;
    }

    if(lpf_active != 0U)
    {
        filter->lp_state += (y - filter->lp_state) * lp_a;
        y = filter->lp_state;
    }
    else
    {
        filter->lp_state = y;
    }
    return y;
}

static inline float smooth_coeff(float sample_rate)
{
    return 1.0f / ((kTimeSmoothSeconds * sample_rate) + 1.0f);
}

static inline float smooth_process(float current, float target, float coeff)
{
    current += (target - current) * coeff;
    if(fabsf(target - current) < 0.1f)
        current = target;
    return current;
}

static inline float target_delay_samples(float time_s, float sample_rate)
{
    const float max_delay = (float)(kDelayBufferSize - 4U);
    return clampf_local(time_s * sample_rate, kMinDelaySamples, max_delay);
}

static inline void apply_width(float wet_l, float wet_r, float width, float *out_l, float *out_r)
{
    const float w = clampf_local(width, -1.0f, 1.0f);
    const float mono = 0.5f * (wet_l + wet_r);
    const float side = 0.5f * (wet_l - wet_r);
    const float side_gain = w + 1.0f;
    *out_l = mono + (side * side_gain);
    *out_r = mono - (side * side_gain);
}

typedef struct
{
    float sample_rate;
    uint8_t mode;
    float time_l_s;
    float time_r_s;
    float time_l_samples;
    float time_r_samples;
    float feedback;
    float hpf;
    float lpf;
    float width;
    float feedback_width;
    float mod_depth;
    float mod_rate_hz;
    float mod_depth_smooth;
    float mod_phase;
    float reverb_send;
    float volume;
    delay_line_t delay_l;
    delay_line_t delay_r;
    delay_line_t haas_l;
    delay_line_t haas_r;
    feedback_filter_t filter_delay_l;
    feedback_filter_t filter_delay_r;
    uint8_t initialized;
} fx_delay_dual_global_state_t;

AUDIO_HOT static fx_delay_dual_global_state_t g_dual;

static void resize_runtime_lines(uint8_t mode, float time_l, float time_r)
{
    const uint32_t max_l = (uint32_t)ceilf(time_l) + 8U;
    const uint32_t max_r = (uint32_t)ceilf(time_r) + 8U;
    const uint32_t max_main = (mode == (uint8_t)FX_DELAY_DUAL_MODE_TAP) ? max_r : max_l;

    delay_line_resize(&g_dual.delay_l, max_main);
    delay_line_resize(&g_dual.delay_r, max_r);
    if(mode != (uint8_t)FX_DELAY_DUAL_MODE_PINGPONG
            && mode != (uint8_t)FX_DELAY_DUAL_MODE_CLASSIC_PINGPONG)
    {
        const float haas = clampf_local(g_dual.width, -1.0f, 1.0f);
        const uint32_t haas_left = (haas < 0.0f)
                ? (uint32_t)ceilf((-haas * kMaxHaasMs * g_dual.sample_rate) / 1000.0f) + 1U
                : 1U;
        const uint32_t haas_right = (haas > 0.0f)
                ? (uint32_t)ceilf((haas * kMaxHaasMs * g_dual.sample_rate) / 1000.0f) + 1U
                : 1U;
        delay_line_resize(&g_dual.haas_l, haas_left);
        delay_line_resize(&g_dual.haas_r, haas_right);
    }
}
}

extern "C" void fx_delay_dual_global_init(float sample_rate)
{
    memset(&g_dual, 0, sizeof(g_dual));
    g_dual.sample_rate = (sample_rate > 1.0f) ? sample_rate : kDefaultSampleRate;
    g_dual.mode = (uint8_t)FX_DELAY_DUAL_MODE_NORMAL;
    g_dual.time_l_s = 0.25f;
    g_dual.time_r_s = 0.25f;
    g_dual.time_l_samples = target_delay_samples(g_dual.time_l_s, g_dual.sample_rate);
    g_dual.time_r_samples = target_delay_samples(g_dual.time_r_s, g_dual.sample_rate);
    fx_delay_shared_pool_acquire(FX_DELAY_SHARED_OWNER_DUAL, 0U);
    delay_line_init(&g_dual.delay_l, fx_delay_shared_pool_left(), kDelayBufferSize);
    delay_line_init(&g_dual.delay_r, fx_delay_shared_pool_right(), kDelayBufferSize);
    delay_line_init(&g_dual.haas_l, g_haas_l, kHaasBufferSize);
    delay_line_init(&g_dual.haas_r, g_haas_r, kHaasBufferSize);
    fx_delay_dual_global_clear();
    g_dual.initialized = 1U;
}

extern "C" void fx_delay_dual_global_clear(void)
{
    fx_delay_shared_pool_acquire(FX_DELAY_SHARED_OWNER_DUAL, 1U);
    delay_line_init(&g_dual.delay_l, fx_delay_shared_pool_left(), kDelayBufferSize);
    delay_line_init(&g_dual.delay_r, fx_delay_shared_pool_right(), kDelayBufferSize);
    delay_line_clear(&g_dual.haas_l);
    delay_line_clear(&g_dual.haas_r);
    memset(&g_dual.filter_delay_l, 0, sizeof(g_dual.filter_delay_l));
    memset(&g_dual.filter_delay_r, 0, sizeof(g_dual.filter_delay_r));
    g_dual.time_l_samples = target_delay_samples(g_dual.time_l_s, g_dual.sample_rate);
    g_dual.time_r_samples = target_delay_samples(g_dual.time_r_s, g_dual.sample_rate);
    g_dual.mod_depth_smooth = g_dual.mod_depth;
    g_dual.mod_phase = 0.0f;
}

extern "C" void fx_delay_dual_global_set_mode(uint8_t mode)
{
    if(mode >= (uint8_t)FX_DELAY_DUAL_MODE_COUNT)
        mode = (uint8_t)FX_DELAY_DUAL_MODE_NORMAL;
    g_dual.mode = mode;
}

extern "C" void fx_delay_dual_global_set_time_l(float time_s)
{
    g_dual.time_l_s = clampf_local(time_s, 0.001f, kMaxDelaySeconds);
}

extern "C" void fx_delay_dual_global_set_time_r(float time_s)
{
    g_dual.time_r_s = clampf_local(time_s, 0.001f, kMaxDelaySeconds);
}

extern "C" void fx_delay_dual_global_set_feedback(float feedback)
{
    g_dual.feedback = clampf_local(feedback, -1.20f, 1.20f);
}

extern "C" void fx_delay_dual_global_set_hpf(float hpf)
{
    g_dual.hpf = clampf_local(hpf, 0.0f, 1.0f);
}

extern "C" void fx_delay_dual_global_set_lpf(float lpf)
{
    g_dual.lpf = clampf_local(lpf, 0.0f, 1.0f);
}

extern "C" void fx_delay_dual_global_set_width(float width)
{
    g_dual.width = clampf_local(width, -1.0f, 1.0f);
}

extern "C" void fx_delay_dual_global_set_feedback_width(float width)
{
    g_dual.feedback_width = clampf_local(width, -1.0f, 1.0f);
}

extern "C" void fx_delay_dual_global_set_mod_depth(float depth)
{
    g_dual.mod_depth = clampf_local(depth, 0.0f, 1.0f);
}

extern "C" void fx_delay_dual_global_set_mod_rate(float rate_hz)
{
    g_dual.mod_rate_hz = clampf_local(rate_hz, 0.01f, 12.0f);
}

extern "C" void fx_delay_dual_global_set_reverb_send(float reverb_send)
{
    g_dual.reverb_send = clampf_local(reverb_send, 0.0f, 1.0f);
}

extern "C" void fx_delay_dual_global_set_volume(float volume)
{
    g_dual.volume = clampf_local(volume, 0.0f, 1.0f);
}

extern "C" uint8_t fx_delay_dual_global_is_active(void)
{
    return ((g_dual.volume > 0.0f) || (g_dual.reverb_send > 0.0f)) ? 1U : 0U;
}

extern "C" void fx_delay_dual_global_process_block(const float *in_l,
                                                   const float *in_r,
                                                   float *out_l,
                                                   float *out_r,
                                                   float *rev_l,
                                                   float *rev_r,
                                                   uint32_t frames)
{
    if((in_l == 0) || (in_r == 0) || (out_l == 0) || (out_r == 0))
        return;

    if(g_dual.initialized == 0U)
        fx_delay_dual_global_init(kDefaultSampleRate);

    const uint8_t has_rev = ((rev_l != 0) && (rev_r != 0)) ? 1U : 0U;
    const float target_l = target_delay_samples(g_dual.time_l_s, g_dual.sample_rate);
    const float target_r = target_delay_samples(g_dual.time_r_s, g_dual.sample_rate);
    const uint8_t mode = g_dual.mode;
    const float feedback_abs = fabsf(g_dual.feedback);
    const float feedback_sign = (g_dual.feedback < 0.0f) ? -1.0f : 1.0f;
    const float width = g_dual.width;
    const float fbw = g_dual.feedback_width;
    const float pipo_width = (mode == (uint8_t)FX_DELAY_DUAL_MODE_CLASSIC_PINGPONG)
            ? clampf_local(fbw, -1.0f, 1.0f)
            : width;
    const float lfactor = (pipo_width > 0.0f) ? (1.0f - pipo_width) : 1.0f;
    const float rfactor = (pipo_width < 0.0f) ? (1.0f + pipo_width) : 1.0f;
    const float sr = g_dual.sample_rate;
    const float isr = 1.0f / sr;
    const float smooth_k = smooth_coeff(sr);
    const float vol = g_dual.volume;
    const float rev = g_dual.reverb_send;
    const uint8_t hpf_active = (g_dual.hpf > 0.001f) ? 1U : 0U;
    const uint8_t lpf_active = (g_dual.lpf > 0.001f) ? 1U : 0U;
    const float hp_a = hpf_active ? hpf_alpha(g_dual.hpf) : 0.0f;
    const float lp_a = lpf_active ? lpf_alpha(g_dual.lpf) : 0.0f;
    const uint8_t has_haas = ((mode != (uint8_t)FX_DELAY_DUAL_MODE_PINGPONG)
            && (mode != (uint8_t)FX_DELAY_DUAL_MODE_CLASSIC_PINGPONG)
            && (fabsf(width) >= kNeutralEpsilon)) ? 1U : 0U;
    const uint8_t has_mod = ((g_dual.mod_depth > kNeutralEpsilon)
            || (g_dual.mod_depth_smooth > kNeutralEpsilon)) ? 1U : 0U;
    float mod_phase = g_dual.mod_phase;
    float mod_sin = 0.0f;
    float mod_cos = 1.0f;
    float mod_sin_step = 0.0f;
    float mod_cos_step = 1.0f;
    if(has_mod != 0U)
    {
        mod_sin = sinf(mod_phase * kTwoPi);
        mod_cos = cosf(mod_phase * kTwoPi);
        const float mod_step = g_dual.mod_rate_hz * isr;
        mod_sin_step = sinf(mod_step * kTwoPi);
        mod_cos_step = cosf(mod_step * kTwoPi);
    }

    resize_runtime_lines(mode, target_l, target_r);

    float e = (mode == (uint8_t)FX_DELAY_DUAL_MODE_TAP) ? 1.0f : target_l / target_r;
    float feedback_l = 0.0f;
    float feedback_r = 0.0f;
    if(target_l < target_r)
    {
        feedback_r = feedback_abs;
        feedback_l = powf(feedback_abs, e);
    }
    else
    {
        e = (e != 0.0f) ? (1.0f / e) : 1.0f;
        feedback_l = feedback_abs;
        feedback_r = powf(feedback_abs, e);
    }
    feedback_l *= feedback_sign;
    feedback_r *= feedback_sign;
    if(mode == (uint8_t)FX_DELAY_DUAL_MODE_CLASSIC_PINGPONG)
    {
        if(pipo_width >= 0.0f)
            feedback_l = feedback_sign * feedback_abs;
        else
            feedback_r = feedback_sign * feedback_abs;
    }

    for(uint32_t i = 0U; i < frames; ++i)
    {
        g_dual.time_l_samples = smooth_process(g_dual.time_l_samples, target_l, smooth_k);
        g_dual.time_r_samples = smooth_process(g_dual.time_r_samples, target_r, smooth_k);

        float mod = 0.0f;
        if(has_mod != 0U)
        {
            g_dual.mod_depth_smooth = smooth_process(g_dual.mod_depth_smooth, g_dual.mod_depth, smooth_k);
            const float max_depth = 0.5f * ((mode == (uint8_t)FX_DELAY_DUAL_MODE_TAP)
                    ? g_dual.time_r_samples
                    : fminf(g_dual.time_l_samples, g_dual.time_r_samples));
            float mod_depth_samples = g_dual.mod_depth_smooth * fminf(sr / 500.0f, max_depth);
            if(mod_depth_samples < kNeutralEpsilon)
                mod_depth_samples = 0.0f;

            if(mod_depth_samples > 0.0f)
            {
                const float next_sin = (mod_sin * mod_cos_step) + (mod_cos * mod_sin_step);
                const float next_cos = (mod_cos * mod_cos_step) - (mod_sin * mod_sin_step);
                mod_sin = next_sin;
                mod_cos = next_cos;
                mod_phase += g_dual.mod_rate_hz * isr;
                if(mod_phase >= 1.0f)
                    mod_phase -= floorf(mod_phase);
                mod = mod_sin * mod_depth_samples - mod_depth_samples;
            }
        }
        else
        {
            g_dual.mod_depth_smooth = 0.0f;
        }

        const float time_left = g_dual.time_l_samples;
        const float time_right = g_dual.time_r_samples;

        const float main_base_l = (mode == (uint8_t)FX_DELAY_DUAL_MODE_TAP) ? time_right : time_left;
        const float main_base_r = time_right;
        const float tap1_l = main_base_l;
        const float tap1_r = main_base_r;

        float v0 = delay_line_read_linear(&g_dual.delay_l, tap1_l + mod);
        float v1 = delay_line_read_linear(&g_dual.delay_r, tap1_r + mod);

        if(mode == (uint8_t)FX_DELAY_DUAL_MODE_NORMAL)
        {
            const float cross = 0.5f * (fbw + 1.0f);
            const float wet_fb_l = v0;
            const float wet_fb_r = v1;
            const float wet_filt_l = feedback_filter_process(&g_dual.filter_delay_l, wet_fb_l, hpf_active, lpf_active, hp_a, lp_a);
            const float wet_filt_r = feedback_filter_process(&g_dual.filter_delay_r, wet_fb_r, hpf_active, lpf_active, hp_a, lp_a);
            const float fb_main_l = (wet_filt_l * (1.0f - cross)) + (wet_filt_r * cross);
            const float fb_main_r = (wet_filt_r * (1.0f - cross)) + (wet_filt_l * cross);
            delay_line_write(&g_dual.delay_l, sanitize_feedback_sample(in_l[i] + (fb_main_l * feedback_l)), 0U);
            delay_line_write(&g_dual.delay_r, sanitize_feedback_sample(in_r[i] + (fb_main_r * feedback_r)), 0U);
        }
        else if((mode == (uint8_t)FX_DELAY_DUAL_MODE_PINGPONG)
                || (mode == (uint8_t)FX_DELAY_DUAL_MODE_CLASSIC_PINGPONG))
        {
            const float mono = (in_l[i] + in_r[i]) * kInvSqrt2;
            const float cross = 0.5f * (fbw + 1.0f);
            const float wet_fb_l = v0;
            const float wet_fb_r = v1;
            const float wet_filt_l = feedback_filter_process(&g_dual.filter_delay_l, wet_fb_l, hpf_active, lpf_active, hp_a, lp_a);
            const float wet_filt_r = feedback_filter_process(&g_dual.filter_delay_r, wet_fb_r, hpf_active, lpf_active, hp_a, lp_a);
            const float fb_delay_l = (wet_filt_r * cross) + (wet_filt_l * (1.0f - cross));
            const float fb_delay_r = (wet_filt_l * cross) + (wet_filt_r * (1.0f - cross));
            delay_line_write(&g_dual.delay_l, sanitize_feedback_sample((mono * lfactor) + (fb_delay_l * feedback_l)), 0U);
            delay_line_write(&g_dual.delay_r, sanitize_feedback_sample((mono * rfactor) + (fb_delay_r * feedback_r)), 0U);
        }
        else
        {
            const float wet_fb_l = v0;
            const float wet_fb_r = v1;
            const float wet_filt_l = feedback_filter_process(&g_dual.filter_delay_l, wet_fb_l, hpf_active, lpf_active, hp_a, lp_a);
            const float wet_filt_r = feedback_filter_process(&g_dual.filter_delay_r, wet_fb_r, hpf_active, lpf_active, hp_a, lp_a);
            delay_line_write(&g_dual.delay_l, sanitize_feedback_sample(in_l[i] + (wet_filt_l * feedback_l)), 0U);
            delay_line_write(&g_dual.delay_r, sanitize_feedback_sample(in_r[i] + (wet_filt_r * feedback_r)), 0U);
        }

        float wet_l = v0;
        float wet_r = v1;
        if(has_haas != 0U)
        {
            delay_line_write(&g_dual.haas_l, sanitize_feedback_sample(wet_l), 0U);
            delay_line_write(&g_dual.haas_r, sanitize_feedback_sample(wet_r), 0U);
            wet_l = delay_line_read_linear(&g_dual.haas_l, (float)(g_dual.haas_l.size - 1U));
            wet_r = delay_line_read_linear(&g_dual.haas_r, (float)(g_dual.haas_r.size - 1U));
        }
        apply_width(wet_l, wet_r, width, &wet_l, &wet_r);
        out_l[i] = sanitize_output_sample(wet_l * vol);
        out_r[i] = sanitize_output_sample(wet_r * vol);
        if(has_rev != 0U)
        {
            rev_l[i] = sanitize_output_sample(wet_l * rev);
            rev_r[i] = sanitize_output_sample(wet_r * rev);
        }
    }
    if(has_mod != 0U)
        g_dual.mod_phase = mod_phase;
}
