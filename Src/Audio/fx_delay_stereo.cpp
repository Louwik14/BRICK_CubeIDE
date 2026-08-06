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
constexpr uint32_t kParamSmoothSamples = 480U;
constexpr float kLpfMinAlpha = 0.01f;
constexpr float kLpfMaxAlpha = 0.85f;

typedef struct
{
    float sample_rate;
    float time_target_s;
    float time_current_samples_l;
    float time_current_samples_r;
    float feedback;
    float feedback_target;
    float low_cut_hz;
    float high_cut_hz;
    uint8_t pingpong;
    float width;
    float width_target;
    float reverb_send;
    float reverb_send_target;
    float volume;
    float volume_target;
    uint16_t feedback_smooth_remaining;
    uint16_t width_smooth_remaining;
    uint16_t reverb_send_smooth_remaining;
    uint16_t volume_smooth_remaining;
    float feedback_hpf_l;
    float feedback_hpf_r;
    float feedback_hpf_prev_l;
    float feedback_hpf_prev_r;
    float feedback_lp_l;
    float feedback_lp_r;
    uint32_t write_index;
    float cached_hpf_a;
    float cached_lpf_a;
    uint8_t filter_coeff_dirty;
    uint8_t initialized;
} fx_delay_stereo_global_state_t;

static fx_delay_stereo_global_state_t g_delay = {};

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

    uint32_t idx_a = write_index + kDelayBufferSize - delay_i;
    if(idx_a >= kDelayBufferSize)
        idx_a -= kDelayBufferSize;
    uint32_t idx_b = (idx_a == 0U) ? (kDelayBufferSize - 1U) : (idx_a - 1U);
    const float a = buffer[idx_a];
    const float b = buffer[idx_b];
    return a + ((b - a) * frac);
}

static inline float lpf_alpha_hz(float frequency_hz, float sample_rate)
{
    const float fc = clampf_local(frequency_hz, 20.0f, 20000.0f);
    return clampf_local(1.0f - expf(-6.28318530718f * fc / sample_rate),
                        kLpfMinAlpha,
                        kLpfMaxAlpha);
}

static inline float hpf_alpha_hz(float frequency_hz, float sample_rate)
{
    const float fc = clampf_local(frequency_hz, 20.0f, 20000.0f);
    return expf(-6.28318530718f * fc / sample_rate);
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
    g_delay.feedback_target = 0.0f;
    g_delay.low_cut_hz = 20.0f;
    g_delay.high_cut_hz = 20000.0f;
    g_delay.pingpong = 0U;
    g_delay.width = 0.0f;
    g_delay.width_target = 0.0f;
    g_delay.reverb_send = 0.0f;
    g_delay.reverb_send_target = 0.0f;
    g_delay.volume = 0.0f;
    g_delay.volume_target = 0.0f;
    g_delay.feedback_smooth_remaining = 0U;
    g_delay.width_smooth_remaining = 0U;
    g_delay.reverb_send_smooth_remaining = 0U;
    g_delay.volume_smooth_remaining = 0U;
    g_delay.feedback_hpf_l = 0.0f;
    g_delay.feedback_hpf_r = 0.0f;
    g_delay.feedback_hpf_prev_l = 0.0f;
    g_delay.feedback_hpf_prev_r = 0.0f;
    g_delay.feedback_lp_l = 0.0f;
    g_delay.feedback_lp_r = 0.0f;
    g_delay.write_index = 0U;
    g_delay.cached_hpf_a = 0.0f;
    g_delay.cached_lpf_a = 0.0f;
    g_delay.filter_coeff_dirty = 1U;
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
    const float target = clampf_local(feedback, 0.0f, kFeedbackMax);
    if(fabsf(target - g_delay.feedback_target) <= 1.0e-7f) return;
    g_delay.feedback_target = target;
    g_delay.feedback_smooth_remaining = (uint16_t)kParamSmoothSamples;
}

extern "C" void fx_delay_stereo_global_set_filter_hz(float low_cut_hz, float high_cut_hz)
{
    const float previous_low = g_delay.low_cut_hz;
    const float previous_high = g_delay.high_cut_hz;
    g_delay.low_cut_hz = clampf_local(low_cut_hz, 20.0f, 20000.0f);
    g_delay.high_cut_hz = clampf_local(high_cut_hz, 20.0f, 20000.0f);
    if (g_delay.high_cut_hz <= g_delay.low_cut_hz)
    {
        g_delay.high_cut_hz = g_delay.low_cut_hz + 1.0f;
        if (g_delay.high_cut_hz > 20000.0f)
        {
            g_delay.high_cut_hz = 20000.0f;
            g_delay.low_cut_hz = 19999.0f;
        }
    }
    if ((g_delay.low_cut_hz != previous_low) || (g_delay.high_cut_hz != previous_high))
        g_delay.filter_coeff_dirty = 1U;
}

extern "C" void fx_delay_stereo_global_set_pingpong(uint8_t enabled)
{
    g_delay.pingpong = (enabled != 0U) ? 1U : 0U;
}

extern "C" void fx_delay_stereo_global_set_width(float width)
{
    const float target = clampf_local(width, -1.0f, 1.0f);
    if(fabsf(target - g_delay.width_target) <= 1.0e-7f) return;
    g_delay.width_target = target;
    g_delay.width_smooth_remaining = (uint16_t)kParamSmoothSamples;
}

extern "C" void fx_delay_stereo_global_set_reverb_send(float reverb_send)
{
    const float target = clampf_local(reverb_send, 0.0f, 1.0f);
    if(fabsf(target - g_delay.reverb_send_target) <= 1.0e-7f) return;
    g_delay.reverb_send_target = target;
    g_delay.reverb_send_smooth_remaining = (uint16_t)kParamSmoothSamples;
}

extern "C" void fx_delay_stereo_global_set_volume(float volume)
{
    const float target = clampf_local(volume, 0.0f, 1.0f);
    if(fabsf(target - g_delay.volume_target) <= 1.0e-7f) return;
    g_delay.volume_target = target;
    g_delay.volume_smooth_remaining = (uint16_t)kParamSmoothSamples;
}

extern "C" uint8_t fx_delay_stereo_global_is_active(void)
{
    return ((g_delay.volume_target > 0.0f)
         || (g_delay.volume > 0.0f)
         || (g_delay.reverb_send_target > 0.0f)) ? 1U : 0U;
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
    const uint8_t hpf_active = (g_delay.low_cut_hz > 20.1f) ? 1U : 0U;
    const uint8_t lpf_active = (g_delay.high_cut_hz < 19999.9f) ? 1U : 0U;
    if (g_delay.filter_coeff_dirty != 0U)
    {
        g_delay.cached_hpf_a = (hpf_active != 0U)
            ? hpf_alpha_hz(g_delay.low_cut_hz, g_delay.sample_rate) : 0.0f;
        g_delay.cached_lpf_a = (lpf_active != 0U)
            ? lpf_alpha_hz(g_delay.high_cut_hz, g_delay.sample_rate) : 0.0f;
        g_delay.filter_coeff_dirty = 0U;
    }
    const float hpf_a = g_delay.cached_hpf_a;
    const float lpf_a = g_delay.cached_lpf_a;
    const uint8_t has_rev = ((rev_l != 0) && (rev_r != 0)) ? 1U : 0U;
    float *const delay_buffer_l = fx_delay_shared_pool_left();
    float *const delay_buffer_r = fx_delay_shared_pool_right();

    float feedback = g_delay.feedback;
    float width = g_delay.width;
    float volume = g_delay.volume;
    uint16_t feedback_remaining = g_delay.feedback_smooth_remaining;
    uint16_t width_remaining = g_delay.width_smooth_remaining;
    uint16_t reverb_send_remaining = g_delay.reverb_send_smooth_remaining;
    uint16_t volume_remaining = g_delay.volume_smooth_remaining;
    const float feedback_step = (feedback_remaining != 0U)
        ? (g_delay.feedback_target - feedback) / (float)feedback_remaining : 0.0f;
    const float width_step = (width_remaining != 0U)
        ? (g_delay.width_target - width) / (float)width_remaining : 0.0f;
    float reverb_send = g_delay.reverb_send;
    const float reverb_send_step = (reverb_send_remaining != 0U)
        ? (g_delay.reverb_send_target - reverb_send) / (float)reverb_send_remaining : 0.0f;
    const float volume_step = (volume_remaining != 0U)
        ? (g_delay.volume_target - volume) / (float)volume_remaining : 0.0f;
    float time_l = g_delay.time_current_samples_l;
    float time_r = g_delay.time_current_samples_r;
    float hpf_l = g_delay.feedback_hpf_l;
    float hpf_r = g_delay.feedback_hpf_r;
    float hpf_prev_l = g_delay.feedback_hpf_prev_l;
    float hpf_prev_r = g_delay.feedback_hpf_prev_r;
    float lp_l = g_delay.feedback_lp_l;
    float lp_r = g_delay.feedback_lp_r;
    uint32_t write_index = g_delay.write_index;

    for(uint32_t i = 0U; i < frames; ++i)
    {
        if(feedback_remaining != 0U)
        {
            feedback += feedback_step;
            --feedback_remaining;
            if(feedback_remaining == 0U)
                feedback = g_delay.feedback_target;
        }
        if(width_remaining != 0U)
        {
            width += width_step;
            --width_remaining;
            if(width_remaining == 0U)
                width = g_delay.width_target;
        }
        if(reverb_send_remaining != 0U)
        {
            reverb_send += reverb_send_step;
            --reverb_send_remaining;
            if(reverb_send_remaining == 0U)
                reverb_send = g_delay.reverb_send_target;
        }
        if(volume_remaining != 0U)
        {
            volume += volume_step;
            --volume_remaining;
            if(volume_remaining == 0U)
                volume = g_delay.volume_target;
        }
        const float fb = feedback;
        const float vol = volume;
        time_l += (target_l - time_l) * kTimeSmooth;
        time_r += (target_r - time_r) * kTimeSmooth;

        const float dl = read_delay(delay_buffer_l, write_index, time_l);
        const float dr = read_delay(delay_buffer_r, write_index, time_r);

        float fb_src_l = dl;
        float fb_src_r = dr;
        if(g_delay.pingpong != 0U)
        {
            fb_src_l = dr;
            fb_src_r = dl;
        }

        if(hpf_active != 0U)
        {
            fb_src_l = process_hpf(fb_src_l, hpf_a, &hpf_l, &hpf_prev_l);
            fb_src_r = process_hpf(fb_src_r, hpf_a, &hpf_r, &hpf_prev_r);
        }
        else
        {
            hpf_prev_l = fb_src_l;
            hpf_prev_r = fb_src_r;
            hpf_l = fb_src_l;
            hpf_r = fb_src_r;
        }

        if(lpf_active != 0U)
        {
            lp_l += (fb_src_l - lp_l) * lpf_a;
            lp_r += (fb_src_r - lp_r) * lpf_a;
            fb_src_l = lp_l;
            fb_src_r = lp_r;
        }
        else
        {
            lp_l = fb_src_l;
            lp_r = fb_src_r;
        }

        float input_l = in_l[i];
        float input_r = in_r[i];
        if((g_delay.pingpong != 0U) && (fabsf(input_l - input_r) <= 1.0e-9f))
        {
            input_r = 0.0f;
        }

        delay_buffer_l[write_index] = input_l + (fb_src_l * fb);
        delay_buffer_r[write_index] = input_r + (fb_src_r * fb);

        float wet_l = 0.0f;
        float wet_r = 0.0f;
        apply_width(dl, dr, width, &wet_l, &wet_r);
        out_l[i] = wet_l * vol;
        out_r[i] = wet_r * vol;
        if(has_rev != 0U)
        {
            rev_l[i] = wet_l * reverb_send;
            rev_r[i] = wet_r * reverb_send;
        }

        write_index++;
        if(write_index >= kDelayBufferSize)
        {
            write_index = 0U;
        }
    }

    g_delay.feedback = feedback;
    g_delay.width = width;
    g_delay.volume = volume;
    g_delay.feedback_smooth_remaining = feedback_remaining;
    g_delay.width_smooth_remaining = width_remaining;
    g_delay.reverb_send = reverb_send;
    g_delay.reverb_send_smooth_remaining = reverb_send_remaining;
    g_delay.volume_smooth_remaining = volume_remaining;
    g_delay.time_current_samples_l = time_l;
    g_delay.time_current_samples_r = time_r;
    g_delay.feedback_hpf_l = hpf_l;
    g_delay.feedback_hpf_r = hpf_r;
    g_delay.feedback_hpf_prev_l = hpf_prev_l;
    g_delay.feedback_hpf_prev_r = hpf_prev_r;
    g_delay.feedback_lp_l = lp_l;
    g_delay.feedback_lp_r = lp_r;
    g_delay.write_index = write_index;
}
