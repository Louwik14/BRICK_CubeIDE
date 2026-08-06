#include "fx_reverb.h"
#include "audio_float.h"
#include "fx_reverb_revb.h"
#include "Storage/memory_layout.h"

#include <algorithm>

static inline float clamp01(float v)
{
    return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
}

typedef struct
{
    volatile uint8_t backend_valid;
    float sample_rate;
    float wet;
    float room_size;
    float damping;
    float width;
    float hpf;
    float lpf;
    float wet_current;
    float room_size_current;
    float damping_current;
    float width_current;
    float hpf_current;
    float lpf_current;
    uint32_t smooth_remaining;
} fx_reverb_global_state_t;

static constexpr uint32_t kParamSmoothSamples = 480U;

static fx_reverb_global_state_t g_reverb_global = {
    .backend_valid = 0U,
    .sample_rate = 48000.0f,
    .wet = 0.0f,
    .room_size = 0.6f,
    .damping = 0.72f,
    .width = 1.0f,
    .hpf = 0.0f,
    .lpf = 1.0f,
};

static void apply_params(void)
{
    if(g_reverb_global.backend_valid == 0U)
        return;
    fx_reverb_revb_global_set_wet(g_reverb_global.wet_current);
    fx_reverb_revb_global_set_room_size(g_reverb_global.room_size_current);
    fx_reverb_revb_global_set_damping(g_reverb_global.damping_current);
    fx_reverb_revb_global_set_width(g_reverb_global.width_current);
    fx_reverb_revb_global_set_hpf(g_reverb_global.hpf_current);
    fx_reverb_revb_global_set_lpf(g_reverb_global.lpf_current);
}

static void advance_params(uint32_t frames)
{
    if (g_reverb_global.smooth_remaining == 0U)
    {
        return;
    }

    const float progress = (float)frames
                         / (float)g_reverb_global.smooth_remaining;
    const float clamped_progress = (progress >= 1.0f) ? 1.0f : progress;
    g_reverb_global.wet_current +=
        (g_reverb_global.wet - g_reverb_global.wet_current) * clamped_progress;
    g_reverb_global.room_size_current +=
        (g_reverb_global.room_size - g_reverb_global.room_size_current) * clamped_progress;
    g_reverb_global.damping_current +=
        (g_reverb_global.damping - g_reverb_global.damping_current) * clamped_progress;
    g_reverb_global.width_current +=
        (g_reverb_global.width - g_reverb_global.width_current) * clamped_progress;
    g_reverb_global.hpf_current +=
        (g_reverb_global.hpf - g_reverb_global.hpf_current) * clamped_progress;
    g_reverb_global.lpf_current +=
        (g_reverb_global.lpf - g_reverb_global.lpf_current) * clamped_progress;
    g_reverb_global.smooth_remaining -=
        (frames >= g_reverb_global.smooth_remaining)
            ? g_reverb_global.smooth_remaining : frames;
    if (g_reverb_global.smooth_remaining == 0U)
    {
        g_reverb_global.wet_current = g_reverb_global.wet;
        g_reverb_global.room_size_current = g_reverb_global.room_size;
        g_reverb_global.damping_current = g_reverb_global.damping;
        g_reverb_global.width_current = g_reverb_global.width;
        g_reverb_global.hpf_current = g_reverb_global.hpf;
        g_reverb_global.lpf_current = g_reverb_global.lpf;
    }
}

static void request_param_smoothing(void)
{
    g_reverb_global.smooth_remaining = kParamSmoothSamples;
}

void fx_reverb_global_init(float sample_rate)
{
    g_reverb_global.sample_rate = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    fx_reverb_revb_global_init(g_reverb_global.sample_rate);
    g_reverb_global.backend_valid = 1U;
    g_reverb_global.wet_current = g_reverb_global.wet;
    g_reverb_global.room_size_current = g_reverb_global.room_size;
    g_reverb_global.damping_current = g_reverb_global.damping;
    g_reverb_global.width_current = g_reverb_global.width;
    g_reverb_global.hpf_current = g_reverb_global.hpf;
    g_reverb_global.lpf_current = g_reverb_global.lpf;
    g_reverb_global.smooth_remaining = 0U;
    apply_params();
}

void fx_reverb_global_set_wet(float wet)
{
    g_reverb_global.wet = clamp01(wet);
    request_param_smoothing();
}

void fx_reverb_global_set_room_size(float value) { g_reverb_global.room_size = clamp01(value); request_param_smoothing(); }
void fx_reverb_global_set_damping(float value) { g_reverb_global.damping = clamp01(value); request_param_smoothing(); }
void fx_reverb_global_set_width(float value) { g_reverb_global.width = clamp01(value); request_param_smoothing(); }
void fx_reverb_global_set_hpf(float value) { g_reverb_global.hpf = clamp01(value); request_param_smoothing(); }
void fx_reverb_global_set_lpf(float value) { g_reverb_global.lpf = clamp01(value); request_param_smoothing(); }
void fx_reverb_global_set_delay_mode(uint8_t tbd) { fx_reverb_revb_global_set_delay_mode(tbd); }

uint8_t fx_reverb_global_is_active(void)
{
    return (g_reverb_global.wet > 0.0f) ? 1U : 0U;
}

void fx_reverb_global_process_block(float *in_l, float *in_r, float *out_l, float *out_r, uint32_t frames)
{
    if((in_l == 0) || (in_r == 0) || (out_l == 0) || (out_r == 0))
        return;
    if(g_reverb_global.backend_valid == 0U)
    {
        volatile float *zero_l = out_l;
        volatile float *zero_r = out_r;
        for(uint32_t i = 0U; i < frames; ++i)
        {
            zero_l[i] = 0.0f;
            zero_r[i] = 0.0f;
        }
        return;
    }
    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;
    uint32_t offset = 0U;
    while (offset < frames)
    {
        const uint32_t span = (g_reverb_global.smooth_remaining != 0U)
            ? std::min(frames - offset, g_reverb_global.smooth_remaining)
            : (frames - offset);
        if (span == 0U)
        {
            break;
        }
        if (g_reverb_global.smooth_remaining != 0U)
        {
            advance_params(span);
            apply_params();
        }
        fx_reverb_revb_global_process_send_stereo_wet(&in_l[offset],
                                                       &in_r[offset],
                                                       &out_l[offset],
                                                       &out_r[offset],
                                                       span);
        offset += span;
    }
}

void fx_reverb_global_process_block_add(const float *in_l,
                                        const float *in_r,
                                        float *destination_l,
                                        float *destination_r,
                                        uint32_t frames)
{
    if((in_l == 0) || (in_r == 0)
            || (destination_l == 0) || (destination_r == 0))
        return;
    if(g_reverb_global.backend_valid == 0U)
        return;
    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;
    uint32_t offset = 0U;
    while (offset < frames)
    {
        const uint32_t span = (g_reverb_global.smooth_remaining != 0U)
            ? std::min(frames - offset, g_reverb_global.smooth_remaining)
            : (frames - offset);
        if (span == 0U)
        {
            break;
        }
        if (g_reverb_global.smooth_remaining != 0U)
        {
            advance_params(span);
            apply_params();
        }
        fx_reverb_revb_global_process_send_stereo_wet_add(&in_l[offset],
                                                          &in_r[offset],
                                                          &destination_l[offset],
                                                          &destination_r[offset],
                                                          span);
        offset += span;
    }
}
