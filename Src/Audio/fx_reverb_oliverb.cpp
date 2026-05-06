#include "fx_reverb_oliverb.h"

#include "audio_float.h"
#include "Storage/memory_layout.h"
#include "fx_oliverb_model.h"

#include <string.h>

namespace
{
constexpr float kDefaultSampleRate = 48000.0f;
constexpr uint32_t kEngineBufferSize = 32768U;

AUDIO_COLD_SDRAM ALIGN32 static float g_oliverb_engine_buffer[kEngineBufferSize];
AUDIO_HOT ALIGN32 static clouds::FloatFrame g_oliverb_frames[AUDIO_BLOCK_SIZE];

struct oliverb_global_state_t
{
    mifx::Oliverb engine;
    float sample_rate;
    float wet;
    float size;
    float decay;
    float lpf;
    uint8_t initialized;
};

static oliverb_global_state_t g_oliverb;

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
    if(g_oliverb.initialized == 0U)
        return;

    const float diffusion = 0.45f + (0.45f * g_oliverb.size);
    const float size = 0.04f + (0.93f * g_oliverb.size);
    const float decay = 0.12f + (0.84f * g_oliverb.decay);
    const float lp = 0.02f + (0.88f * (1.0f - g_oliverb.lpf));

    g_oliverb.engine.set_amount(1.0f);
    g_oliverb.engine.set_input_gain(g_oliverb.wet);
    g_oliverb.engine.set_diffusion(clampf_local(diffusion, 0.0f, 0.95f));
    g_oliverb.engine.set_size(clampf_local(size, 0.01f, 0.98f));
    g_oliverb.engine.set_decay(clampf_local(decay, 0.0f, 0.98f));
    g_oliverb.engine.set_lp(clampf_local(lp, 0.01f, 0.95f));
    g_oliverb.engine.set_hp(0.0f);
    g_oliverb.engine.set_mod_amount(0.0f);
    g_oliverb.engine.set_mod_rate(0.0f);
    g_oliverb.engine.set_ratio(0.0f);
    g_oliverb.engine.set_pitch_shift_amount(0.0f);
}
}

void fx_reverb_oliverb_global_init(float sample_rate)
{
    g_oliverb.sample_rate = (sample_rate > 0.0f) ? sample_rate : kDefaultSampleRate;
    g_oliverb.decay = 0.50f;
    g_oliverb.size = 0.0f;
    g_oliverb.wet = 0.0f;
    g_oliverb.lpf = 0.0f;
    g_oliverb.initialized = 0U;
    g_oliverb.engine.Init(g_oliverb_engine_buffer);
    g_oliverb.initialized = 1U;
    apply_params();
}

void fx_reverb_oliverb_global_reset(void)
{
    if(g_oliverb.initialized == 0U)
        return;

    g_oliverb.engine.Clear();
}

void fx_reverb_oliverb_global_set_wet(float wet)
{
    g_oliverb.wet = clamp01_local(wet);
    apply_params();
}

void fx_reverb_oliverb_global_set_size(float size)
{
    g_oliverb.size = clamp01_local(size);
    apply_params();
}

void fx_reverb_oliverb_global_set_decay(float decay)
{
    g_oliverb.decay = clamp01_local(decay);
    apply_params();
}

void fx_reverb_oliverb_global_set_lpf(float lpf)
{
    g_oliverb.lpf = clamp01_local(lpf);
    apply_params();
}

void fx_reverb_oliverb_global_process_send_mono_to_stereo_wet(const float *in,
                                                               float *out_l,
                                                               float *out_r,
                                                               uint32_t frames)
{
    if((in == 0) || (out_l == 0) || (out_r == 0))
        return;

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    if((g_oliverb.initialized == 0U) || (g_oliverb.wet <= 0.0f))
    {
        memset(out_l, 0, sizeof(float) * frames);
        memset(out_r, 0, sizeof(float) * frames);
        return;
    }

    for(uint32_t i = 0U; i < frames; ++i)
    {
        const float mono = 0.5f * in[i];
        g_oliverb_frames[i].l = mono;
        g_oliverb_frames[i].r = mono;
    }

    g_oliverb.engine.Process(g_oliverb_frames, frames);

    for(uint32_t i = 0U; i < frames; ++i)
    {
        out_l[i] = g_oliverb_frames[i].l;
        out_r[i] = g_oliverb_frames[i].r;
    }
}
