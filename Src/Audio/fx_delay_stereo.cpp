#include "fx_delay_stereo.h"

#include "Audio/fx_delay_shared_pool.h"
#include "Storage/memory_layout.h"

#include <math.h>
#include <string.h>

namespace
{
constexpr float kDefaultSampleRate = 48000.0f;
constexpr float kMaxDelaySeconds = 6.0f;
constexpr uint32_t kDelayBufferSize = FX_DELAY_SHARED_CAPACITY;
constexpr float kMinDelaySamples = 1.0f;
constexpr float kFeedbackMax = 0.95f;
constexpr float kTimeSmooth = 0.0025f;
constexpr float kLpfMinAlpha = 0.01f;
constexpr float kLpfMaxAlpha = 0.85f;

typedef struct
{
    float sample_rate;
    float time_target_s;
    float time_current_samples_l;
    float time_current_samples_r;
    float feedback;
    float hpf;
    float lpf;
    uint8_t pingpong;
    float width;
    float reverb_send;
    float volume;
    float feedback_hpf_l;
    float feedback_hpf_r;
    float feedback_hpf_prev_l;
    float feedback_hpf_prev_r;
    float feedback_lp_l;
    float feedback_lp_r;
    uint32_t write_index;
    uint8_t initialized;
} fx_delay_stereo_global_state_t;

static fx_delay_stereo_global_state_t g_delay = {
    kDefaultSampleRate,
    0.25f,
    0.25f * kDefaultSampleRate,
    0.25f * kDefaultSampleRate,
    0.0f,
    0.0f,
    0.0f,
    0U,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    0U,
    0U,
};

static inline float clampf_local(float v, float lo, float hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static inline float target_delay_samples(float time_s, float sample_rate)
{
    const float max_delay = (float)(kDelayBufferSize - 2U);
    const float samples = time_s * sample_rate;
    return clampf_local(samples, kMinDelaySamples, max_delay);
}

static inline float read_delay(const float *buffer, uint32_t write_index, float delay_samples)
{
    const float clamped = clampf_local(delay_samples, kMinDelaySamples, (float)(kDelayBufferSize - 2U));
    const uint32_t delay_i = (uint32_t)clamped;
    const float frac = clamped - (float)delay_i;

    uint32_t idx_a = (write_index + kDelayBufferSize - delay_i) % kDelayBufferSize;
    uint32_t idx_b = (idx_a == 0U) ? (kDelayBufferSize - 1U) : (idx_a - 1U);
    const float a = buffer[idx_a];
    const float b = buffer[idx_b];
    return a + ((b - a) * frac);
}

static inline float lpf_alpha(float lpf)
{
    const float t = 1.0f - clampf_local(lpf, 0.0f, 1.0f);
    return kLpfMinAlpha + ((kLpfMaxAlpha - kLpfMinAlpha) * t * t);
}

static inline float hpf_alpha(float hpf)
{
    const float t = clampf_local(hpf, 0.0f, 1.0f);
    return 0.995f - (0.94f * t * t);
}

static inline float process_hpf(float input, float alpha, float *state, float *prev_input)
{
    const float y = alpha * (*state + input - *prev_input);
    *prev_input = input;
    *state = y;
    return y;
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
}

extern "C" void fx_delay_stereo_global_init(float sample_rate)
{
    g_delay.sample_rate = (sample_rate > 1.0f) ? sample_rate : kDefaultSampleRate;
    g_delay.time_target_s = 0.25f;
    g_delay.time_current_samples_l = target_delay_samples(g_delay.time_target_s, g_delay.sample_rate);
    g_delay.time_current_samples_r = g_delay.time_current_samples_l;
    g_delay.feedback = 0.0f;
    g_delay.hpf = 0.0f;
    g_delay.lpf = 0.0f;
    g_delay.pingpong = 0U;
    g_delay.width = 0.0f;
    g_delay.reverb_send = 0.0f;
    g_delay.volume = 0.0f;
    g_delay.feedback_hpf_l = 0.0f;
    g_delay.feedback_hpf_r = 0.0f;
    g_delay.feedback_hpf_prev_l = 0.0f;
    g_delay.feedback_hpf_prev_r = 0.0f;
    g_delay.feedback_lp_l = 0.0f;
    g_delay.feedback_lp_r = 0.0f;
    g_delay.write_index = 0U;
    fx_delay_shared_pool_acquire(FX_DELAY_SHARED_OWNER_CLASSIC, 1U);
    g_delay.initialized = 1U;
}

extern "C" void fx_delay_stereo_global_clear(void)
{
    fx_delay_shared_pool_acquire(FX_DELAY_SHARED_OWNER_CLASSIC, 1U);
    g_delay.time_current_samples_l = target_delay_samples(g_delay.time_target_s, g_delay.sample_rate);
    g_delay.time_current_samples_r = g_delay.time_current_samples_l;
    g_delay.feedback_hpf_l = 0.0f;
    g_delay.feedback_hpf_r = 0.0f;
    g_delay.feedback_hpf_prev_l = 0.0f;
    g_delay.feedback_hpf_prev_r = 0.0f;
    g_delay.feedback_lp_l = 0.0f;
    g_delay.feedback_lp_r = 0.0f;
    g_delay.write_index = 0U;
}

extern "C" void fx_delay_stereo_global_set_time(float time_s)
{
    g_delay.time_target_s = clampf_local(time_s, 0.001f, kMaxDelaySeconds);
}

extern "C" void fx_delay_stereo_global_set_feedback(float feedback)
{
    g_delay.feedback = clampf_local(feedback, 0.0f, kFeedbackMax);
}

extern "C" void fx_delay_stereo_global_set_hpf(float hpf)
{
    g_delay.hpf = clampf_local(hpf, 0.0f, 1.0f);
}

extern "C" void fx_delay_stereo_global_set_lpf(float lpf)
{
    g_delay.lpf = clampf_local(lpf, 0.0f, 1.0f);
}

extern "C" void fx_delay_stereo_global_set_pingpong(uint8_t enabled)
{
    g_delay.pingpong = (enabled != 0U) ? 1U : 0U;
}

extern "C" void fx_delay_stereo_global_set_width(float width)
{
    g_delay.width = clampf_local(width, -1.0f, 1.0f);
}

extern "C" void fx_delay_stereo_global_set_reverb_send(float reverb_send)
{
    g_delay.reverb_send = clampf_local(reverb_send, 0.0f, 1.0f);
}

extern "C" void fx_delay_stereo_global_set_volume(float volume)
{
    g_delay.volume = clampf_local(volume, 0.0f, 1.0f);
}

extern "C" uint8_t fx_delay_stereo_global_is_active(void)
{
    return ((g_delay.volume > 0.0f) || (g_delay.reverb_send > 0.0f)) ? 1U : 0U;
}

extern "C" void fx_delay_stereo_global_process_block(const float *in_l,
                                                     const float *in_r,
                                                     float *out_l,
                                                     float *out_r,
                                                     float *rev_l,
                                                     float *rev_r,
                                                     uint32_t frames)
{
    if((in_l == 0) || (in_r == 0) || (out_l == 0) || (out_r == 0))
        return;

    if(g_delay.initialized == 0U)
    {
        fx_delay_stereo_global_init(kDefaultSampleRate);
    }

    const float target_l = target_delay_samples(g_delay.time_target_s, g_delay.sample_rate);
    const float target_r = target_l;
    const float fb = g_delay.feedback;
    const uint8_t hpf_active = (g_delay.hpf > 0.001f) ? 1U : 0U;
    const uint8_t lpf_active = (g_delay.lpf > 0.001f) ? 1U : 0U;
    const float hpf_a = (hpf_active != 0U) ? hpf_alpha(g_delay.hpf) : 0.0f;
    const float lpf_a = (lpf_active != 0U) ? lpf_alpha(g_delay.lpf) : 0.0f;
    const float vol = g_delay.volume;
    const float rev = g_delay.reverb_send;
    const float width = g_delay.width;
    const uint8_t has_rev = ((rev_l != 0) && (rev_r != 0)) ? 1U : 0U;
    float *const delay_buffer_l = fx_delay_shared_pool_left();
    float *const delay_buffer_r = fx_delay_shared_pool_right();

    for(uint32_t i = 0U; i < frames; ++i)
    {
        g_delay.time_current_samples_l += (target_l - g_delay.time_current_samples_l) * kTimeSmooth;
        g_delay.time_current_samples_r += (target_r - g_delay.time_current_samples_r) * kTimeSmooth;

        const float dl = read_delay(delay_buffer_l, g_delay.write_index, g_delay.time_current_samples_l);
        const float dr = read_delay(delay_buffer_r, g_delay.write_index, g_delay.time_current_samples_r);

        float fb_src_l = dl;
        float fb_src_r = dr;
        if(g_delay.pingpong != 0U)
        {
            fb_src_l = dr;
            fb_src_r = dl;
        }

        if(hpf_active != 0U)
        {
            fb_src_l = process_hpf(fb_src_l, hpf_a, &g_delay.feedback_hpf_l, &g_delay.feedback_hpf_prev_l);
            fb_src_r = process_hpf(fb_src_r, hpf_a, &g_delay.feedback_hpf_r, &g_delay.feedback_hpf_prev_r);
        }
        else
        {
            g_delay.feedback_hpf_prev_l = fb_src_l;
            g_delay.feedback_hpf_prev_r = fb_src_r;
            g_delay.feedback_hpf_l = fb_src_l;
            g_delay.feedback_hpf_r = fb_src_r;
        }

        if(lpf_active != 0U)
        {
            g_delay.feedback_lp_l += (fb_src_l - g_delay.feedback_lp_l) * lpf_a;
            g_delay.feedback_lp_r += (fb_src_r - g_delay.feedback_lp_r) * lpf_a;
            fb_src_l = g_delay.feedback_lp_l;
            fb_src_r = g_delay.feedback_lp_r;
        }
        else
        {
            g_delay.feedback_lp_l = fb_src_l;
            g_delay.feedback_lp_r = fb_src_r;
        }

        float input_l = in_l[i];
        float input_r = in_r[i];
        if((g_delay.pingpong != 0U) && (fabsf(input_l - input_r) <= 1.0e-9f))
        {
            input_r = 0.0f;
        }

        delay_buffer_l[g_delay.write_index] = input_l + (fb_src_l * fb);
        delay_buffer_r[g_delay.write_index] = input_r + (fb_src_r * fb);

        float wet_l = 0.0f;
        float wet_r = 0.0f;
        apply_width(dl, dr, width, &wet_l, &wet_r);
        out_l[i] = wet_l * vol;
        out_r[i] = wet_r * vol;
        if(has_rev != 0U)
        {
            rev_l[i] = wet_l * rev;
            rev_r[i] = wet_r * rev;
        }

        g_delay.write_index++;
        if(g_delay.write_index >= kDelayBufferSize)
        {
            g_delay.write_index = 0U;
        }
    }
}
