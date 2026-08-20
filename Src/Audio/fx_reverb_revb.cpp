#include "fx_reverb_revb.h"

#include "audio_float.h"
#include "Storage/memory_layout.h"
#include "fx_revb_model.h"

#include <cmath>

namespace
{
constexpr float kDefaultSampleRate = 48000.0f;
constexpr uint32_t kEngineBufferSize = 32768U;

AUDIO_WARM ALIGN32 static float g_revb_engine_buffer[kEngineBufferSize];

struct revb_global_state_t
{
    mifx::Reverb engine;
    float sample_rate;
    float wet;
    float hpf_coefficient;
    float lpf_coefficient;
    uint8_t initialized;
};

static revb_global_state_t g_revb;

static inline float clamp01(float value)
{
    return (value < 0.0f) ? 0.0f : ((value > 1.0f) ? 1.0f : value);
}

static float filter_cutoff(float value, float min_hz, float scale_hz)
{
    const float fc_hz = min_hz + (std::exp(1.5f * clamp01(value)) - 1.0f) * scale_hz;
    const float fc = fc_hz / g_revb.sample_rate;
    return fc / (1.0f + fc);
}
}

namespace mifx
{
ITCM_TEXT void Reverb::ProcessStereoWetAdd(const float *in_l,
                                                    const float *in_r,
                                                    float *out_l,
                                                    float *out_r,
                                                    const float *wet,
                                                    size_t size)
{
    StereoWetInput input = {in_l, in_r, wet};
    AddOutput output = {out_l, out_r};
    if (tbd_delays_) {
        ProcessCore<TbdMemory>(input, output, size,
                               6815.2383f, 54.42177f,
                               4854.4219f, 43.53742f);
    } else {
        ProcessCore<DelugeMemory>(input, output, size,
                                  6261.0f, 50.0f,
                                  4460.0f, 40.0f);
    }
}
}

void fx_reverb_revb_global_init(float sample_rate)
{
    g_revb.sample_rate = (sample_rate > 0.0f) ? sample_rate : kDefaultSampleRate;
    g_revb.wet = 0.0f;
    g_revb.initialized = 0U;
    g_revb.engine.Init(g_revb_engine_buffer);
    g_revb.engine.set_amount(1.0f);
    g_revb.engine.set_time(0.665f);
    g_revb.engine.set_diffusion(0.625f);
    g_revb.engine.set_lp(0.7f);
    g_revb.hpf_coefficient = filter_cutoff(0.0f, 20.0f, 150.0f);
    g_revb.lpf_coefficient = filter_cutoff(0.0f, 0.0f, 5083.74f);
    g_revb.engine.set_output_filters(g_revb.hpf_coefficient, g_revb.lpf_coefficient);
    g_revb.initialized = 1U;
}

void fx_reverb_revb_global_set_wet(float wet) { g_revb.wet = clamp01(wet); }

void fx_reverb_revb_global_set_room_size(float room_size)
{
    g_revb.engine.set_time(0.01f + 0.97f * clamp01(room_size));
}

void fx_reverb_revb_global_set_damping(float damping)
{
    const float value = clamp01(damping);
    const float lp = (value == 0.0f)
            ? 1.0f
            : 1.0f - fminf(fmaxf(log2f(((1.0f - value) * 50.0f) + 1.0f) / 5.7f, 0.0f), 1.0f);
    g_revb.engine.set_lp(lp);
}

void fx_reverb_revb_global_set_width(float width)
{
    g_revb.engine.set_diffusion(0.1f + 0.8f * clamp01(width));
}

void fx_reverb_revb_global_set_hpf(float hpf)
{
    g_revb.hpf_coefficient = filter_cutoff(hpf, 20.0f, 150.0f);
    g_revb.engine.set_output_filters(g_revb.hpf_coefficient, g_revb.lpf_coefficient);
}

void fx_reverb_revb_global_set_lpf(float lpf)
{
    g_revb.lpf_coefficient = filter_cutoff(lpf, 0.0f, 5083.74f);
    g_revb.engine.set_output_filters(g_revb.hpf_coefficient, g_revb.lpf_coefficient);
}

void fx_reverb_revb_global_set_delay_mode(uint8_t tbd)
{
    g_revb.engine.set_delay_mode(tbd != 0U);
}

void fx_reverb_revb_global_process_send_mono_to_stereo_wet(const float *in,
                                                           float *out_l,
                                                           float *out_r,
                                                           uint32_t frames)
{
    if((in == nullptr) || (out_l == nullptr) || (out_r == nullptr))
        return;
    if((g_revb.initialized == 0U) || (g_revb.wet <= 0.0f))
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
    g_revb.engine.ProcessMonoWet(in, out_l, out_r, &g_revb.wet, frames);
}

void fx_reverb_revb_global_process_send_stereo_wet(const float *in_l,
                                                   const float *in_r,
                                                   float *out_l,
                                                   float *out_r,
                                                   uint32_t frames)
{
    if((in_l == nullptr) || (in_r == nullptr)
            || (out_l == nullptr) || (out_r == nullptr))
        return;
    if((g_revb.initialized == 0U) || (g_revb.wet <= 0.0f))
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
    g_revb.engine.ProcessStereoWet(in_l,
                                   in_r,
                                   out_l,
                                   out_r,
                                   &g_revb.wet,
                                   frames);
}

void fx_reverb_revb_global_process_send_stereo_wet_add(const float *in_l,
                                                       const float *in_r,
                                                       float *destination_l,
                                                       float *destination_r,
                                                       uint32_t frames)
{
    if((in_l == nullptr) || (in_r == nullptr)
            || (destination_l == nullptr) || (destination_r == nullptr))
        return;
    if((g_revb.initialized == 0U) || (g_revb.wet <= 0.0f))
        return;
    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;
    g_revb.engine.ProcessStereoWetAdd(in_l,
                                      in_r,
                                      destination_l,
                                      destination_r,
                                      &g_revb.wet,
                                      frames);
}
