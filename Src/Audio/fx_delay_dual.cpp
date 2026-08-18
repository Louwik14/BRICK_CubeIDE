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
constexpr uint32_t kParamSmoothSamples = 480U;
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
    const float clipped = clampf_local(v, -4.0f, 4.0f);
    return clipped;
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

static inline float delay_line_read_linear_at(const float *buffer,
                                              uint32_t size,
                                              uint32_t pos,
                                              float delay)
{
    if((buffer == 0) || (size == 0U))
        return 0.0f;

    delay = clampf_local(delay, 1.0f, (float)(size - 1U));
    float read_pos = (float)pos - delay;
    if(read_pos < 0.0f)
        read_pos += (float)size;

    const uint32_t i0 = (uint32_t)read_pos;
    const uint32_t i1 = (i0 + 1U >= size) ? 0U : (i0 + 1U);
    const float frac = read_pos - (float)i0;
    return buffer[i0] + ((buffer[i1] - buffer[i0]) * frac);
}

static inline uint32_t delay_line_write_at(float *buffer,
                                           uint32_t size,
                                           uint32_t pos,
                                           float sample)
{
    buffer[pos] = sample;
    ++pos;
    if(pos >= size)
        pos = 0U;
    return pos;
}

typedef struct
{
    float hp_state;
    float hp_prev_input;
    float lp_state;
} feedback_filter_t;

static inline float hpf_alpha_hz(float frequency_hz, float sample_rate)
{
    const float fc = clampf_local(frequency_hz, 20.0f, 20000.0f);
    return expf(-6.28318530718f * fc / sample_rate);
}

static inline float lpf_alpha_hz(float frequency_hz, float sample_rate)
{
    const float fc = clampf_local(frequency_hz, 20.0f, 20000.0f);
    return clampf_local(1.0f - expf(-6.28318530718f * fc / sample_rate), 0.005f, 0.805f);
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
    float feedback_target;
    float low_cut_hz;
    float high_cut_hz;
    float width;
    float width_target;
    float feedback_width;
    float mod_depth;
    float mod_rate_hz;
    float mod_depth_smooth;
    float mod_phase;
    float reverb_send;
    float reverb_send_target;
    float volume;
    float volume_target;
    uint16_t feedback_smooth_remaining;
    uint16_t width_smooth_remaining;
    uint16_t reverb_send_smooth_remaining;
    uint16_t volume_smooth_remaining;
    delay_line_t delay_l;
    delay_line_t delay_r;
    delay_line_t haas_l;
    delay_line_t haas_r;
    feedback_filter_t filter_delay_l;
    feedback_filter_t filter_delay_r;
    float cached_hp_a;
    float cached_lp_a;
    float cached_mod_sin_step;
    float cached_mod_cos_step;
    uint8_t filter_coeff_dirty;
    uint8_t mod_step_dirty;
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
        delay_line_resize(&g_dual.haas_l, kHaasBufferSize);
        delay_line_resize(&g_dual.haas_r, kHaasBufferSize);
    }
}

typedef struct
{
    float target_l;
    float target_r;
    float sample_rate;
    float smooth_k;
    float reverb_send;
    float feedback_width;
    float feedback_l;
    float feedback_r;
    float feedback_reference_inv;
    float hpf_a;
    float lpf_a;
    float mod_sin;
    float mod_cos;
    float mod_sin_step;
    float mod_cos_step;
    float mod_phase_step;
    float max_haas_samples;
    uint8_t has_rev;
    uint8_t hpf_active;
    uint8_t lpf_active;
    uint8_t has_mod;
} dual_kernel_config_t;

template<uint8_t Mode, bool HaasActive>
__attribute__((noinline)) static void process_dual_kernel(const float *in_l,
                                const float *in_r,
                                float *out_l,
                                float *out_r,
                                float *rev_l,
                                float *rev_r,
                                uint32_t frames,
                                const dual_kernel_config_t &cfg)
{
    float feedback = g_dual.feedback;
    float width = g_dual.width;
    float reverb_send = g_dual.reverb_send;
    float volume = g_dual.volume;
    uint16_t feedback_remaining = g_dual.feedback_smooth_remaining;
    uint16_t width_remaining = g_dual.width_smooth_remaining;
    uint16_t reverb_send_remaining = g_dual.reverb_send_smooth_remaining;
    uint16_t volume_remaining = g_dual.volume_smooth_remaining;
    const float feedback_step = (feedback_remaining != 0U)
        ? (g_dual.feedback_target - feedback) / (float)feedback_remaining : 0.0f;
    const float width_step = (width_remaining != 0U)
        ? (g_dual.width_target - width) / (float)width_remaining : 0.0f;
    const float reverb_send_step = (reverb_send_remaining != 0U)
        ? (g_dual.reverb_send_target - reverb_send) / (float)reverb_send_remaining : 0.0f;
    const float volume_step = (volume_remaining != 0U)
        ? (g_dual.volume_target - volume) / (float)volume_remaining : 0.0f;

    float time_l = g_dual.time_l_samples;
    float time_r = g_dual.time_r_samples;
    float mod_depth_smooth = g_dual.mod_depth_smooth;
    float mod_phase = g_dual.mod_phase;
    float mod_sin = cfg.mod_sin;
    float mod_cos = cfg.mod_cos;
    feedback_filter_t filter_l = g_dual.filter_delay_l;
    feedback_filter_t filter_r = g_dual.filter_delay_r;

    float *const delay_buffer_l = g_dual.delay_l.buffer;
    float *const delay_buffer_r = g_dual.delay_r.buffer;
    const uint32_t delay_size_l = g_dual.delay_l.size;
    const uint32_t delay_size_r = g_dual.delay_r.size;
    uint32_t delay_pos_l = g_dual.delay_l.pos;
    uint32_t delay_pos_r = g_dual.delay_r.pos;
    uint32_t haas_pos_l = g_dual.haas_l.pos;
    uint32_t haas_pos_r = g_dual.haas_r.pos;

    for(uint32_t i = 0U; i < frames; ++i)
    {
        if(feedback_remaining != 0U)
        {
            feedback += feedback_step;
            --feedback_remaining;
            if(feedback_remaining == 0U)
                feedback = g_dual.feedback_target;
        }
        if(width_remaining != 0U)
        {
            width += width_step;
            --width_remaining;
            if(width_remaining == 0U)
                width = g_dual.width_target;
        }
        if(reverb_send_remaining != 0U)
        {
            reverb_send += reverb_send_step;
            --reverb_send_remaining;
            if(reverb_send_remaining == 0U)
                reverb_send = g_dual.reverb_send_target;
        }
        if(volume_remaining != 0U)
        {
            volume += volume_step;
            --volume_remaining;
            if(volume_remaining == 0U)
                volume = g_dual.volume_target;
        }

        const float feedback_scale = (cfg.feedback_reference_inv != 0.0f)
            ? (feedback * cfg.feedback_reference_inv) : 0.0f;
        const float feedback_l_now = cfg.feedback_l * feedback_scale;
        const float feedback_r_now = cfg.feedback_r * feedback_scale;
        const float pipo_width = (Mode == (uint8_t)FX_DELAY_DUAL_MODE_CLASSIC_PINGPONG)
            ? clampf_local(cfg.feedback_width, -1.0f, 1.0f) : width;
        const float lfactor = (pipo_width > 0.0f) ? (1.0f - pipo_width) : 1.0f;
        const float rfactor = (pipo_width < 0.0f) ? (1.0f + pipo_width) : 1.0f;
        time_l = smooth_process(time_l, cfg.target_l, cfg.smooth_k);
        time_r = smooth_process(time_r, cfg.target_r, cfg.smooth_k);

        float mod = 0.0f;
        if(cfg.has_mod != 0U)
        {
            mod_depth_smooth = smooth_process(mod_depth_smooth,
                                              g_dual.mod_depth,
                                              cfg.smooth_k);
            const float max_depth = 0.5f * ((Mode == (uint8_t)FX_DELAY_DUAL_MODE_TAP)
                ? time_r : fminf(time_l, time_r));
            float mod_depth_samples = mod_depth_smooth
                * fminf(cfg.sample_rate / 500.0f, max_depth);
            if(mod_depth_samples < kNeutralEpsilon)
                mod_depth_samples = 0.0f;

            if(mod_depth_samples > 0.0f)
            {
                const float next_sin = (mod_sin * cfg.mod_cos_step)
                    + (mod_cos * cfg.mod_sin_step);
                const float next_cos = (mod_cos * cfg.mod_cos_step)
                    - (mod_sin * cfg.mod_sin_step);
                mod_sin = next_sin;
                mod_cos = next_cos;
                mod_phase += cfg.mod_phase_step;
                if(mod_phase >= 1.0f)
                    mod_phase -= floorf(mod_phase);
                mod = mod_sin * mod_depth_samples - mod_depth_samples;
            }
        }
        else
        {
            mod_depth_smooth = 0.0f;
        }

        const float time_left = time_l;
        const float time_right = time_r;
        const float main_base_l = (Mode == (uint8_t)FX_DELAY_DUAL_MODE_TAP)
            ? time_right : time_left;
        const float v0 = delay_line_read_linear_at(delay_buffer_l,
                                                   delay_size_l,
                                                   delay_pos_l,
                                                   main_base_l + mod);
        const float v1 = delay_line_read_linear_at(delay_buffer_r,
                                                   delay_size_r,
                                                   delay_pos_r,
                                                   time_right + mod);

        if(Mode == (uint8_t)FX_DELAY_DUAL_MODE_NORMAL)
        {
            const float cross = 0.5f * (cfg.feedback_width + 1.0f);
            const float wet_filt_l = feedback_filter_process(&filter_l,
                                                              v0,
                                                              cfg.hpf_active,
                                                              cfg.lpf_active,
                                                              cfg.hpf_a,
                                                              cfg.lpf_a);
            const float wet_filt_r = feedback_filter_process(&filter_r,
                                                              v1,
                                                              cfg.hpf_active,
                                                              cfg.lpf_active,
                                                              cfg.hpf_a,
                                                              cfg.lpf_a);
            const float fb_main_l = (wet_filt_l * (1.0f - cross))
                + (wet_filt_r * cross);
            const float fb_main_r = (wet_filt_r * (1.0f - cross))
                + (wet_filt_l * cross);
            delay_pos_l = delay_line_write_at(delay_buffer_l,
                                              delay_size_l,
                                              delay_pos_l,
                                              sanitize_feedback_sample(in_l[i]
                                                  + (fb_main_l * feedback_l_now)));
            delay_pos_r = delay_line_write_at(delay_buffer_r,
                                              delay_size_r,
                                              delay_pos_r,
                                              sanitize_feedback_sample(in_r[i]
                                                  + (fb_main_r * feedback_r_now)));
        }
        else if((Mode == (uint8_t)FX_DELAY_DUAL_MODE_PINGPONG)
                || (Mode == (uint8_t)FX_DELAY_DUAL_MODE_CLASSIC_PINGPONG))
        {
            const float mono = (in_l[i] + in_r[i]) * kInvSqrt2;
            const float cross = 0.5f * (cfg.feedback_width + 1.0f);
            const float wet_filt_l = feedback_filter_process(&filter_l,
                                                              v0,
                                                              cfg.hpf_active,
                                                              cfg.lpf_active,
                                                              cfg.hpf_a,
                                                              cfg.lpf_a);
            const float wet_filt_r = feedback_filter_process(&filter_r,
                                                              v1,
                                                              cfg.hpf_active,
                                                              cfg.lpf_active,
                                                              cfg.hpf_a,
                                                              cfg.lpf_a);
            const float fb_delay_l = (wet_filt_r * cross)
                + (wet_filt_l * (1.0f - cross));
            const float fb_delay_r = (wet_filt_l * cross)
                + (wet_filt_r * (1.0f - cross));
            delay_pos_l = delay_line_write_at(delay_buffer_l,
                                              delay_size_l,
                                              delay_pos_l,
                                              sanitize_feedback_sample((mono * lfactor)
                                                  + (fb_delay_l * feedback_l_now)));
            delay_pos_r = delay_line_write_at(delay_buffer_r,
                                              delay_size_r,
                                              delay_pos_r,
                                              sanitize_feedback_sample((mono * rfactor)
                                                  + (fb_delay_r * feedback_r_now)));
        }
        else
        {
            const float wet_filt_l = feedback_filter_process(&filter_l,
                                                              v0,
                                                              cfg.hpf_active,
                                                              cfg.lpf_active,
                                                              cfg.hpf_a,
                                                              cfg.lpf_a);
            const float wet_filt_r = feedback_filter_process(&filter_r,
                                                              v1,
                                                              cfg.hpf_active,
                                                              cfg.lpf_active,
                                                              cfg.hpf_a,
                                                              cfg.lpf_a);
            delay_pos_l = delay_line_write_at(delay_buffer_l,
                                              delay_size_l,
                                              delay_pos_l,
                                              sanitize_feedback_sample(in_l[i]
                                                  + (wet_filt_l * feedback_l_now)));
            delay_pos_r = delay_line_write_at(delay_buffer_r,
                                              delay_size_r,
                                              delay_pos_r,
                                              sanitize_feedback_sample(in_r[i]
                                                  + (wet_filt_r * feedback_r_now)));
        }

        float wet_l = v0;
        float wet_r = v1;
        if((Mode == (uint8_t)FX_DELAY_DUAL_MODE_NORMAL)
                || (Mode == (uint8_t)FX_DELAY_DUAL_MODE_TAP))
        {
            haas_pos_l = delay_line_write_at(g_dual.haas_l.buffer,
                                             g_dual.haas_l.size,
                                             haas_pos_l,
                                             sanitize_feedback_sample(wet_l));
            haas_pos_r = delay_line_write_at(g_dual.haas_r.buffer,
                                             g_dual.haas_r.size,
                                             haas_pos_r,
                                             sanitize_feedback_sample(wet_r));
            if(HaasActive)
            {
                const float haas_l = (width < 0.0f)
                    ? (1.0f + (-width * cfg.max_haas_samples)) : 1.0f;
                const float haas_r = (width > 0.0f)
                    ? (1.0f + ( width * cfg.max_haas_samples)) : 1.0f;
                wet_l = delay_line_read_linear_at(g_dual.haas_l.buffer,
                                                  g_dual.haas_l.size,
                                                  haas_pos_l,
                                                  haas_l);
                wet_r = delay_line_read_linear_at(g_dual.haas_r.buffer,
                                                  g_dual.haas_r.size,
                                                  haas_pos_r,
                                                  haas_r);
            }
        }
        apply_width(wet_l, wet_r, width, &wet_l, &wet_r);
        out_l[i] = sanitize_output_sample(wet_l * volume);
        out_r[i] = sanitize_output_sample(wet_r * volume);
        if(cfg.has_rev != 0U)
        {
            rev_l[i] = sanitize_output_sample(wet_l * reverb_send);
            rev_r[i] = sanitize_output_sample(wet_r * reverb_send);
        }
    }

    g_dual.feedback = feedback;
    g_dual.width = width;
    g_dual.reverb_send = reverb_send;
    g_dual.volume = volume;
    g_dual.feedback_smooth_remaining = feedback_remaining;
    g_dual.width_smooth_remaining = width_remaining;
    g_dual.reverb_send_smooth_remaining = reverb_send_remaining;
    g_dual.volume_smooth_remaining = volume_remaining;
    g_dual.time_l_samples = time_l;
    g_dual.time_r_samples = time_r;
    g_dual.mod_depth_smooth = mod_depth_smooth;
    if(cfg.has_mod != 0U)
        g_dual.mod_phase = mod_phase;
    g_dual.filter_delay_l = filter_l;
    g_dual.filter_delay_r = filter_r;
    g_dual.delay_l.pos = delay_pos_l;
    g_dual.delay_r.pos = delay_pos_r;
    if((Mode == (uint8_t)FX_DELAY_DUAL_MODE_NORMAL)
            || (Mode == (uint8_t)FX_DELAY_DUAL_MODE_TAP))
    {
        g_dual.haas_l.pos = haas_pos_l;
        g_dual.haas_r.pos = haas_pos_r;
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
    g_dual.feedback_target = g_dual.feedback;
    g_dual.width_target = g_dual.width;
    g_dual.volume_target = g_dual.volume;
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
    g_dual.filter_coeff_dirty = 1U;
    g_dual.mod_step_dirty = 1U;
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
    const float target = clampf_local(feedback, -1.20f, 1.20f);
    if(fabsf(target - g_dual.feedback_target) <= 1.0e-7f) return;
    g_dual.feedback_target = target;
    g_dual.feedback_smooth_remaining = (uint16_t)kParamSmoothSamples;
}

extern "C" void fx_delay_dual_global_set_filter_hz(float low_cut_hz, float high_cut_hz)
{
    const float previous_low = g_dual.low_cut_hz;
    const float previous_high = g_dual.high_cut_hz;
    g_dual.low_cut_hz = clampf_local(low_cut_hz, 20.0f, 20000.0f);
    g_dual.high_cut_hz = clampf_local(high_cut_hz, 20.0f, 20000.0f);
    if (g_dual.high_cut_hz <= g_dual.low_cut_hz)
    {
        g_dual.high_cut_hz = g_dual.low_cut_hz + 1.0f;
        if (g_dual.high_cut_hz > 20000.0f)
        {
            g_dual.high_cut_hz = 20000.0f;
            g_dual.low_cut_hz = 19999.0f;
        }
    }
    if ((g_dual.low_cut_hz != previous_low) || (g_dual.high_cut_hz != previous_high))
        g_dual.filter_coeff_dirty = 1U;
}

extern "C" void fx_delay_dual_global_set_width(float width)
{
    const float target = clampf_local(width, -1.0f, 1.0f);
    if(fabsf(target - g_dual.width_target) <= 1.0e-7f) return;
    g_dual.width_target = target;
    g_dual.width_smooth_remaining = (uint16_t)kParamSmoothSamples;
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
    const float next = clampf_local(rate_hz, 0.01f, 12.0f);
    if (next == g_dual.mod_rate_hz) return;
    g_dual.mod_rate_hz = next;
    g_dual.mod_step_dirty = 1U;
}

extern "C" void fx_delay_dual_global_set_reverb_send(float reverb_send)
{
    const float target = clampf_local(reverb_send, 0.0f, 1.0f);
    if(fabsf(target - g_dual.reverb_send_target) <= 1.0e-7f) return;
    g_dual.reverb_send_target = target;
    g_dual.reverb_send_smooth_remaining = (uint16_t)kParamSmoothSamples;
}

extern "C" void fx_delay_dual_global_set_volume(float volume)
{
    const float target = clampf_local(volume, 0.0f, 1.0f);
    if(fabsf(target - g_dual.volume_target) <= 1.0e-7f) return;
    g_dual.volume_target = target;
    g_dual.volume_smooth_remaining = (uint16_t)kParamSmoothSamples;
}

extern "C" uint8_t fx_delay_dual_global_is_active(void)
{
    return ((g_dual.volume_target > 0.0f)
         || (g_dual.volume > 0.0f)
         || (g_dual.reverb_send_target > 0.0f)) ? 1U : 0U;
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
    const float feedback_reference =
        (fabsf(g_dual.feedback_target) > kNeutralEpsilon)
        ? g_dual.feedback_target : g_dual.feedback;
    const float feedback_abs = fabsf(feedback_reference);
    const float feedback_sign = (feedback_reference < 0.0f) ? -1.0f : 1.0f;
    const float fbw = g_dual.feedback_width;
    const float sr = g_dual.sample_rate;
    const float isr = 1.0f / sr;
    const float smooth_k = smooth_coeff(sr);
    const float rev = g_dual.reverb_send;
    const uint8_t hpf_active = (g_dual.low_cut_hz > 20.1f) ? 1U : 0U;
    const uint8_t lpf_active = (g_dual.high_cut_hz < 19999.9f) ? 1U : 0U;
    if (g_dual.filter_coeff_dirty != 0U)
    {
        g_dual.cached_hp_a = hpf_active ? hpf_alpha_hz(g_dual.low_cut_hz, sr) : 0.0f;
        g_dual.cached_lp_a = lpf_active ? lpf_alpha_hz(g_dual.high_cut_hz, sr) : 0.0f;
        g_dual.filter_coeff_dirty = 0U;
    }
    const float hp_a = g_dual.cached_hp_a;
    const float lp_a = g_dual.cached_lp_a;
    const uint8_t has_haas = ((mode != (uint8_t)FX_DELAY_DUAL_MODE_PINGPONG)
            && (mode != (uint8_t)FX_DELAY_DUAL_MODE_CLASSIC_PINGPONG)
            && ((fabsf(g_dual.width) >= kNeutralEpsilon)
             || (fabsf(g_dual.width_target) >= kNeutralEpsilon))) ? 1U : 0U;
    const uint8_t has_mod = ((g_dual.mod_depth > kNeutralEpsilon)
            || (g_dual.mod_depth_smooth > kNeutralEpsilon)) ? 1U : 0U;
    const float mod_phase = g_dual.mod_phase;
    float mod_sin = 0.0f;
    float mod_cos = 1.0f;
    float mod_sin_step = 0.0f;
    float mod_cos_step = 1.0f;
    if(has_mod != 0U)
    {
        mod_sin = sinf(mod_phase * kTwoPi);
        mod_cos = cosf(mod_phase * kTwoPi);
        if (g_dual.mod_step_dirty != 0U)
        {
            const float mod_step = g_dual.mod_rate_hz * isr;
            g_dual.cached_mod_sin_step = sinf(mod_step * kTwoPi);
            g_dual.cached_mod_cos_step = cosf(mod_step * kTwoPi);
            g_dual.mod_step_dirty = 0U;
        }
        mod_sin_step = g_dual.cached_mod_sin_step;
        mod_cos_step = g_dual.cached_mod_cos_step;
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
        if(fbw >= 0.0f)
            feedback_l = feedback_sign * feedback_abs;
        else
            feedback_r = feedback_sign * feedback_abs;
    }

    dual_kernel_config_t cfg = {};
    cfg.target_l = target_l;
    cfg.target_r = target_r;
    cfg.sample_rate = sr;
    cfg.smooth_k = smooth_k;
    cfg.reverb_send = rev;
    cfg.feedback_width = fbw;
    cfg.feedback_l = feedback_l;
    cfg.feedback_r = feedback_r;
    cfg.feedback_reference_inv = (fabsf(feedback_reference) > kNeutralEpsilon)
        ? (1.0f / feedback_reference) : 0.0f;
    cfg.hpf_a = hp_a;
    cfg.lpf_a = lp_a;
    cfg.mod_sin = mod_sin;
    cfg.mod_cos = mod_cos;
    cfg.mod_sin_step = mod_sin_step;
    cfg.mod_cos_step = mod_cos_step;
    cfg.mod_phase_step = has_mod ? (g_dual.mod_rate_hz * isr) : 0.0f;
    cfg.max_haas_samples = kMaxHaasMs * sr * (1.0f / 1000.0f);
    cfg.has_rev = has_rev;
    cfg.hpf_active = hpf_active;
    cfg.lpf_active = lpf_active;
    cfg.has_mod = has_mod;

    switch(mode)
    {
    case (uint8_t)FX_DELAY_DUAL_MODE_NORMAL:
        if(has_haas != 0U)
            process_dual_kernel<(uint8_t)FX_DELAY_DUAL_MODE_NORMAL, true>(in_l, in_r, out_l, out_r, rev_l, rev_r, frames, cfg);
        else
            process_dual_kernel<(uint8_t)FX_DELAY_DUAL_MODE_NORMAL, false>(in_l, in_r, out_l, out_r, rev_l, rev_r, frames, cfg);
        break;
    case (uint8_t)FX_DELAY_DUAL_MODE_TAP:
        if(has_haas != 0U)
            process_dual_kernel<(uint8_t)FX_DELAY_DUAL_MODE_TAP, true>(in_l, in_r, out_l, out_r, rev_l, rev_r, frames, cfg);
        else
            process_dual_kernel<(uint8_t)FX_DELAY_DUAL_MODE_TAP, false>(in_l, in_r, out_l, out_r, rev_l, rev_r, frames, cfg);
        break;
    case (uint8_t)FX_DELAY_DUAL_MODE_PINGPONG:
        process_dual_kernel<(uint8_t)FX_DELAY_DUAL_MODE_PINGPONG, false>(in_l, in_r, out_l, out_r, rev_l, rev_r, frames, cfg);
        break;
    case (uint8_t)FX_DELAY_DUAL_MODE_CLASSIC_PINGPONG:
        process_dual_kernel<(uint8_t)FX_DELAY_DUAL_MODE_CLASSIC_PINGPONG, false>(in_l, in_r, out_l, out_r, rev_l, rev_r, frames, cfg);
        break;
    default:
        process_dual_kernel<(uint8_t)FX_DELAY_DUAL_MODE_TAP, true>(in_l, in_r, out_l, out_r, rev_l, rev_r, frames, cfg);
        break;
    }
}
