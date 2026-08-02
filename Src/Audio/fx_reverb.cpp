#include "fx_reverb.h"
#include "audio_float.h"
#include "fx_reverb_revb.h"
#include "Storage/memory_layout.h"
#include <string.h>

static inline float fx_reverb_clamp01(float v)
{
    if(v < 0.0f)
        return 0.0f;
    if(v > 1.0f)
        return 1.0f;
    return v;
}

typedef struct
{
    volatile uint8_t backend_valid;
    float sample_rate;
    float wet;
    float size;
    float decay;
    float damp;
    float predelay_s;
    float low_cut_hz;
    float high_cut_hz;
    uint8_t mutable_geometry;
} fx_reverb_global_state_t;

static fx_reverb_global_state_t g_reverb_global = {
    .backend_valid = 0U,
    .sample_rate = 48000.0f,
    .wet = 0.0f,
    .size = 0.0f,
    .decay = 0.50f,
    .damp = 0.70f,
    .predelay_s = 0.045f,
    .low_cut_hz = 20.0f,
    .high_cut_hz = 20000.0f,
    .mutable_geometry = 0U,
};

AUDIO_HOT ALIGN32 static float g_reverb_global_mono[AUDIO_BLOCK_SIZE];

static void fx_reverb_global_apply_params(void)
{
    if(g_reverb_global.backend_valid == 0U)
        return;

    fx_reverb_revb_global_set_wet(g_reverb_global.wet);
    fx_reverb_revb_global_set_size(g_reverb_global.size);
    fx_reverb_revb_global_set_decay(g_reverb_global.decay);
    fx_reverb_revb_global_set_damp(g_reverb_global.damp);
    fx_reverb_revb_global_set_mutable(g_reverb_global.mutable_geometry);
    fx_reverb_revb_global_set_predelay(g_reverb_global.predelay_s);
    fx_reverb_revb_global_set_filter_hz(g_reverb_global.low_cut_hz,
                                        g_reverb_global.high_cut_hz);
}

void fx_reverb_global_init(float sample_rate)
{
    g_reverb_global.sample_rate = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    fx_reverb_revb_global_init(g_reverb_global.sample_rate);
    g_reverb_global.backend_valid = 1U;
    fx_reverb_global_apply_params();
}

void fx_reverb_global_set_wet(float wet)
{
    const float previous = g_reverb_global.wet;
    g_reverb_global.wet = fx_reverb_clamp01(wet);
    if((previous > 0.0f) && (g_reverb_global.wet <= 0.0f))
        fx_reverb_revb_global_reset();
    fx_reverb_global_apply_params();
}

void fx_reverb_global_set_size(float size)
{
    g_reverb_global.size = fx_reverb_clamp01(size);
    fx_reverb_global_apply_params();
}

void fx_reverb_global_set_decay(float decay)
{
    g_reverb_global.decay = fx_reverb_clamp01(decay);
    fx_reverb_global_apply_params();
}

void fx_reverb_global_set_damp(float damp)
{
    g_reverb_global.damp = fx_reverb_clamp01(damp);
    fx_reverb_global_apply_params();
}

void fx_reverb_global_set_mutable(uint8_t enabled)
{
    g_reverb_global.mutable_geometry = (enabled != 0U) ? 1U : 0U;
    if(g_reverb_global.backend_valid != 0U)
        fx_reverb_revb_global_set_mutable(g_reverb_global.mutable_geometry);
}

void fx_reverb_global_set_predelay(float predelay_s)
{
    g_reverb_global.predelay_s = (predelay_s < 0.0f) ? 0.0f : predelay_s;
    fx_reverb_global_apply_params();
}

void fx_reverb_global_set_filter_hz(float low_cut_hz, float high_cut_hz)
{
    g_reverb_global.low_cut_hz = (low_cut_hz < 20.0f) ? 20.0f
        : ((low_cut_hz > 20000.0f) ? 20000.0f : low_cut_hz);
    g_reverb_global.high_cut_hz = (high_cut_hz < 20.0f) ? 20.0f
        : ((high_cut_hz > 20000.0f) ? 20000.0f : high_cut_hz);
    if (g_reverb_global.high_cut_hz <= g_reverb_global.low_cut_hz)
    {
        g_reverb_global.high_cut_hz = g_reverb_global.low_cut_hz + 1.0f;
        if (g_reverb_global.high_cut_hz > 20000.0f)
        {
            g_reverb_global.high_cut_hz = 20000.0f;
            g_reverb_global.low_cut_hz = 19999.0f;
        }
    }
    fx_reverb_global_apply_params();
}

uint8_t fx_reverb_global_is_active(void)
{
    return (g_reverb_global.wet > 0.0f) ? 1U : 0U;
}

void fx_reverb_global_process_block(float *in_l,
                                    float *in_r,
                                    float *out_l,
                                    float *out_r,
                                    uint32_t frames)
{
    if((in_l == 0) || (in_r == 0) || (out_l == 0) || (out_r == 0))
        return;

    if(g_reverb_global.backend_valid == 0U)
    {
        for(uint32_t i = 0; i < frames; i++)
        {
            out_l[i] = 0.0f;
            out_r[i] = 0.0f;
        }
        return;
    }

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;
    for(uint32_t i = 0U; i < frames; ++i)
        g_reverb_global_mono[i] = 0.5f * (in_l[i] + in_r[i]);
    fx_reverb_revb_global_process_send_mono_to_stereo_wet(g_reverb_global_mono, out_l, out_r, frames);
}
