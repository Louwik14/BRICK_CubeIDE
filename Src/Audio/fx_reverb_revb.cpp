#include "fx_reverb_revb.h"

#include "audio_float.h"
#include "Storage/memory_layout.h"
#include "fx_revb_model.h"

#include <string.h>
#include <math.h>

namespace
{
constexpr float kDefaultSampleRate = 48000.0f;
constexpr uint32_t kEngineBufferSize = 32768U;
constexpr float kPredelayMaxSeconds = 0.090f;
constexpr uint32_t kPredelayBufferSize = 4322U;
constexpr uint32_t kTopologySamples48k = 23528U;
static_assert(kTopologySamples48k + 10U < kEngineBufferSize,
              "48 kHz Mutable topology must fit the 32768-sample engine buffer");

AUDIO_WARM ALIGN32 static float g_revb_engine_buffer[kEngineBufferSize];
AUDIO_WARM ALIGN32 static float g_revb_predelay_buffer[kPredelayBufferSize];
AUDIO_HOT ALIGN32 static float g_revb_predelayed[AUDIO_BLOCK_SIZE];

struct revb_global_state_t
{
    mifx::Reverb engine;
    float sample_rate;
    float wet;
    float size;
    float decay;
    float predelay_s;
    float lpf;
    float damp;
    float hpf;
    float smear;
    float wet_current;
    float size_current;
    float decay_current;
    float damp_current;
    float hpf_current;
    float lpf_current;
    float smear_current;
    float predelay_lag_samples;
    float predelay_current_samples;
    uint32_t predelay_write;
    uint8_t initialized;
};

static revb_global_state_t g_revb;

static inline float clamp01_local(float v)
{
    if(v < 0.0f)
        return 0.0f;
    if(v > 1.0f)
        return 1.0f;
    return v;
}

static inline float clampf_local(float v, float lo, float hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static void apply_params(void)
{
    if(g_revb.initialized == 0U)
        return;

    const float diffusion = 0.45f + (0.45f * g_revb.size_current);
    const float time = 0.20f + (0.78f * g_revb.decay_current);
    const float damping_arg = ((1.0f - g_revb.damp_current) * 50.0f) + 1.0f;
    const float damping_curve = clampf_local(log2f(damping_arg) / 5.7f, 0.0f, 1.0f);
    const float lp = (g_revb.damp_current <= 0.0f)
            ? 1.0f
            : (1.0f - damping_curve);
    const float lfo1_hz = 0.25f + (0.50f * g_revb.size_current);
    const float lfo2_hz = 0.15f + (0.35f * g_revb.size_current);
    const float hp_hz = 20.0f + ((expf(1.5f * g_revb.hpf_current) - 1.0f) * 150.0f);
    const float lpf_deluge_value = 1.0f - g_revb.lpf_current;
    const float lp_hz = (expf(1.5f * lpf_deluge_value) - 1.0f) * 5083.74f;
    const float hp_fc = hp_hz / g_revb.sample_rate;
    const float lp_fc = lp_hz / g_revb.sample_rate;

    g_revb.engine.set_amount(1.0f);
    g_revb.engine.set_input_gain(1.0f);
    g_revb.engine.set_diffusion(clampf_local(diffusion, 0.0f, 0.95f));
    g_revb.engine.set_time(clampf_local(time, 0.0f, 0.98f));
    g_revb.engine.set_lp(clampf_local(lp, 0.0f, 1.0f));
    g_revb.engine.set_output_filters(hp_fc / (1.0f + hp_fc),
                                      (g_revb.lpf_current <= 0.0f)
                                              ? 1.0f
                                              : (lp_fc / (1.0f + lp_fc)));
    g_revb.engine.set_smear_depth((g_revb.smear_current <= 0.0001f)
                                      ? 0.0f
                                      : (80.0f * g_revb.smear_current));
    g_revb.engine.set_lfo1_freq(lfo1_hz);
    g_revb.engine.set_lfo2_freq(lfo2_hz);

    g_revb.predelay_s = clampf_local(g_revb.predelay_s, 0.0f, kPredelayMaxSeconds);
    g_revb.predelay_lag_samples = clampf_local(g_revb.predelay_s * g_revb.sample_rate,
                                               0.0f,
                                               (float)(kPredelayBufferSize - 2U));
}

static inline float read_predelay(float delay_samples)
{
    const float clamped = clampf_local(delay_samples, 0.0f, (float)(kPredelayBufferSize - 2U));
    const uint32_t delay_i = (uint32_t)clamped;
    const float frac = clamped - (float)delay_i;

    const uint32_t idx_a = (g_revb.predelay_write + kPredelayBufferSize - delay_i) % kPredelayBufferSize;
    const uint32_t idx_b = (idx_a == 0U) ? (kPredelayBufferSize - 1U) : (idx_a - 1U);
    const float a = g_revb_predelay_buffer[idx_a];
    const float b = g_revb_predelay_buffer[idx_b];
    return a + ((b - a) * frac);
}
}

void fx_reverb_revb_global_init(float sample_rate)
{
    g_revb.sample_rate = (sample_rate > 0.0f) ? sample_rate : kDefaultSampleRate;
    g_revb.decay = 0.50f;
    g_revb.size = 0.0f;
    g_revb.predelay_s = 0.045f;
    g_revb.wet = 0.0f;
    g_revb.lpf = 0.0f;
    g_revb.damp = 0.70f;
    g_revb.hpf = 0.0f;
    g_revb.smear = 1.0f;
    g_revb.wet_current = 0.0f;
    g_revb.size_current = g_revb.size;
    g_revb.decay_current = g_revb.decay;
    g_revb.damp_current = g_revb.damp;
    g_revb.hpf_current = g_revb.hpf;
    g_revb.lpf_current = g_revb.lpf;
    g_revb.smear_current = g_revb.smear;
    g_revb.predelay_lag_samples = 0.0f;
    g_revb.predelay_write = 0U;
    g_revb.predelay_current_samples = g_revb.predelay_s * g_revb.sample_rate;
    g_revb.initialized = 0U;
    memset(g_revb_predelay_buffer, 0, sizeof(g_revb_predelay_buffer));
    g_revb.engine.Init(g_revb_engine_buffer);
    g_revb.initialized = 1U;
    apply_params();
}

void fx_reverb_revb_global_reset(void)
{
    if(g_revb.initialized == 0U)
        return;

    memset(g_revb_predelay_buffer, 0, sizeof(g_revb_predelay_buffer));
    g_revb.predelay_write = 0U;
    g_revb.predelay_current_samples = g_revb.predelay_lag_samples;
    g_revb.wet_current = 0.0f;
    g_revb.engine.Clear();
}

void fx_reverb_revb_global_set_wet(float wet)
{
    g_revb.wet = clamp01_local(wet);
    apply_params();
}

void fx_reverb_revb_global_set_size(float size)
{
    g_revb.size = clamp01_local(size);
    apply_params();
}

void fx_reverb_revb_global_set_decay(float decay)
{
    g_revb.decay = clamp01_local(decay);
    apply_params();
}

void fx_reverb_revb_global_set_damp(float damp)
{
    g_revb.damp = clamp01_local(damp);
}

void fx_reverb_revb_global_set_predelay(float predelay_s)
{
    g_revb.predelay_s = (predelay_s < 0.0f) ? 0.0f : predelay_s;
    apply_params();
}

void fx_reverb_revb_global_set_hpf(float hpf)
{
    g_revb.hpf = clamp01_local(hpf);
}

void fx_reverb_revb_global_set_lpf(float lpf)
{
    g_revb.lpf = clamp01_local(lpf);
}

void fx_reverb_revb_global_set_smear(float smear)
{
    g_revb.smear = clamp01_local(smear);
}

ITCM_TEXT_NAMED("fx_reverb_revb_global")
void fx_reverb_revb_global_process_send_mono_to_stereo_wet(const float *in,
                                                           float *out_l,
                                                           float *out_r,
                                                           uint32_t frames)
{
    if((in == 0) || (out_l == 0) || (out_r == 0))
        return;

    if((g_revb.initialized == 0U) || (g_revb.wet <= 0.0f))
    {
        memset(out_l, 0, sizeof(float) * frames);
        memset(out_r, 0, sizeof(float) * frames);
        return;
    }

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    const float block_smooth = 0.125f;
    g_revb.size_current += (g_revb.size - g_revb.size_current) * block_smooth;
    g_revb.decay_current += (g_revb.decay - g_revb.decay_current) * block_smooth;
    g_revb.damp_current += (g_revb.damp - g_revb.damp_current) * block_smooth;
    g_revb.hpf_current += (g_revb.hpf - g_revb.hpf_current) * block_smooth;
    g_revb.lpf_current += (g_revb.lpf - g_revb.lpf_current) * block_smooth;
    g_revb.smear_current += (g_revb.smear - g_revb.smear_current) * block_smooth;
    if((g_revb.smear == 0.0f) && (g_revb.smear_current < 0.0001f))
        g_revb.smear_current = 0.0f;
    apply_params();

    const float wet_step = (g_revb.wet - g_revb.wet_current) / (float)frames;
    const float predelay_old = g_revb.predelay_current_samples;
    const float predelay_new = g_revb.predelay_lag_samples;
    const float predelay_delta = predelay_new - predelay_old;
    const uint8_t predelay_crossfade = ((predelay_delta > 0.0001f)
            || (predelay_delta < -0.0001f)) ? 1U : 0U;
    for(uint32_t i = 0U; i < frames; ++i)
    {
        g_revb.wet_current += wet_step;
        g_revb_predelay_buffer[g_revb.predelay_write] = in[i] * g_revb.wet_current;
        if(predelay_crossfade != 0U)
        {
            const float xfade = (float)(i + 1U) / (float)frames;
            const float old_read = read_predelay(predelay_old);
            const float new_read = read_predelay(predelay_new);
            g_revb_predelayed[i] = old_read + ((new_read - old_read) * xfade);
        }
        else
        {
            g_revb_predelayed[i] = read_predelay(predelay_new);
        }
        g_revb.predelay_write++;
        if(g_revb.predelay_write >= kPredelayBufferSize)
            g_revb.predelay_write = 0U;
    }
    g_revb.wet_current = g_revb.wet;
    g_revb.predelay_current_samples = predelay_new;

    g_revb.engine.Process(g_revb_predelayed, out_l, out_r, frames);
}
