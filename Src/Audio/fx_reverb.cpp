#include "fx_reverb.h"
#include "audio_float.h"
#include "fx_reverb_drumboy.h"
#include "fx_reverb_gverb.h"
#include "fx_reverb_oliverb.h"
#include "fx_reverb_revb.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx.h"
#include <string.h>

static inline float fx_reverb_clamp01(float v)
{
    if(v < 0.0f)
        return 0.0f;
    if(v > 1.0f)
        return 1.0f;
    return v;
}

void fx_reverb_init(fx_reverb_t *rev, float sample_rate)
{
    if(rev == 0)
        return;

    rev->bypass = 0U;
    fx_reverb_drumboy_init(&rev->model, sample_rate);
}

void fx_reverb_process_block(fx_reverb_t *rev,
                             float *in_l,
                             float *in_r,
                             float *out_l,
                             float *out_r,
                             uint32_t frames)
{
    if((rev == 0) || (in_l == 0) || (in_r == 0) || (out_l == 0) || (out_r == 0))
        return;

    if(rev->bypass)
    {
        for(uint32_t i = 0; i < frames; i++)
        {
            out_l[i] = 0.0f;
            out_r[i] = 0.0f;
        }
        return;
    }

    fx_reverb_drumboy_process_block(&rev->model, in_l, in_r, out_l, out_r, frames);
}

void fx_reverb_set_wet_ui(fx_reverb_t *rev, uint8_t wet_ui)
{
    if(rev == 0)
        return;

    if(wet_ui == 0U)
    {
        rev->bypass = 1U;
        return;
    }

    rev->bypass = 0U;
    fx_reverb_drumboy_set_wet(&rev->model, (float)wet_ui * (1.0f / 127.0f));
}

void fx_reverb_set_wet(fx_reverb_t *rev, float wet)
{
    if(rev == 0)
        return;

    float wet_clamped = fx_reverb_clamp01(wet);
    uint8_t wet_ui = (uint8_t)(wet_clamped * 127.0f + 0.5f);
    fx_reverb_set_wet_ui(rev, wet_ui);
}

void fx_reverb_set_room_size(fx_reverb_t *rev, float room)
{
    if(rev == 0)
        return;

    fx_reverb_drumboy_set_size(&rev->model, fx_reverb_clamp01(room));
}

void fx_reverb_set_damping(fx_reverb_t *rev, float damp)
{
    if(rev == 0)
        return;

    fx_reverb_drumboy_set_decay(&rev->model, fx_reverb_clamp01(damp));
}

void fx_reverb_set_width(fx_reverb_t *rev, float width)
{
    if(rev == 0)
        return;

    fx_reverb_drumboy_set_surround(&rev->model, fx_reverb_clamp01(width));
}

void fx_reverb_set_bypass(fx_reverb_t *rev, uint8_t bypass)
{
    if(rev == 0)
        return;

    rev->bypass = bypass ? 1U : 0U;
}

typedef union
{
    fx_reverb_drumboy_t drumboy;
} fx_reverb_global_storage_t;

typedef struct
{
    fx_reverb_global_storage_t storage;
    volatile uint8_t backend_valid;
    fx_reverb_global_type_t type;
    float sample_rate;
    float wet;
    float size;
    float decay;
    float predelay_s;
    float surround_s;
    float lpf;
    volatile uint32_t last_cycles;
    volatile uint32_t max_cycles;
} fx_reverb_global_state_t;

static fx_reverb_global_state_t g_reverb_global = {
    .backend_valid = 0U,
    .type = FX_REVERB_GLOBAL_TYPE_DRUMBOY,
    .sample_rate = 48000.0f,
    .wet = 0.0f,
    .size = 0.0f,
    .decay = 0.50f,
    .predelay_s = 0.045f,
    .surround_s = 0.009f,
    .lpf = 0.0f,
    .last_cycles = 0U,
    .max_cycles = 0U,
};

AUDIO_HOT ALIGN32 static float g_reverb_global_mono[AUDIO_BLOCK_SIZE];

static void fx_reverb_global_apply_params(void)
{
    if(g_reverb_global.backend_valid == 0U)
        return;

    if(g_reverb_global.type == FX_REVERB_GLOBAL_TYPE_REVB)
    {
        fx_reverb_revb_global_set_wet(g_reverb_global.wet);
        fx_reverb_revb_global_set_size(g_reverb_global.size);
        fx_reverb_revb_global_set_decay(g_reverb_global.decay);
        fx_reverb_revb_global_set_predelay(g_reverb_global.predelay_s);
        fx_reverb_revb_global_set_lpf(g_reverb_global.lpf);
        return;
    }

    if(g_reverb_global.type == FX_REVERB_GLOBAL_TYPE_GVERB)
    {
        fx_reverb_gverb_global_set_wet(g_reverb_global.wet);
        fx_reverb_gverb_global_set_size(g_reverb_global.size);
        fx_reverb_gverb_global_set_decay(g_reverb_global.decay);
        fx_reverb_gverb_global_set_lpf(g_reverb_global.lpf);
        return;
    }

    if(g_reverb_global.type == FX_REVERB_GLOBAL_TYPE_OLIVERB)
    {
        fx_reverb_oliverb_global_set_wet(g_reverb_global.wet);
        fx_reverb_oliverb_global_set_size(g_reverb_global.size);
        fx_reverb_oliverb_global_set_decay(g_reverb_global.decay);
        fx_reverb_oliverb_global_set_lpf(g_reverb_global.lpf);
        return;
    }

    fx_reverb_drumboy_t *const rev = &g_reverb_global.storage.drumboy;
    fx_reverb_drumboy_set_wet(rev, g_reverb_global.wet);
    fx_reverb_drumboy_set_size(rev, g_reverb_global.size);
    fx_reverb_drumboy_set_decay(rev, g_reverb_global.decay);
    fx_reverb_drumboy_set_predelay(rev, g_reverb_global.predelay_s);
    fx_reverb_drumboy_set_surround(rev, g_reverb_global.surround_s);
}

void fx_reverb_global_init(float sample_rate)
{
    g_reverb_global.sample_rate = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    memset(&g_reverb_global.storage.drumboy, 0, sizeof(g_reverb_global.storage.drumboy));
    fx_reverb_drumboy_init(&g_reverb_global.storage.drumboy, g_reverb_global.sample_rate);
    fx_reverb_revb_global_init(g_reverb_global.sample_rate);
    fx_reverb_gverb_global_init(g_reverb_global.sample_rate);
    fx_reverb_oliverb_global_init(g_reverb_global.sample_rate);
    g_reverb_global.type = FX_REVERB_GLOBAL_TYPE_DRUMBOY;
    g_reverb_global.backend_valid = 1U;
    fx_reverb_global_apply_params();
}

void fx_reverb_global_set_type(fx_reverb_global_type_t type)
{
    fx_reverb_global_type_t next = FX_REVERB_GLOBAL_TYPE_DRUMBOY;
    if(type == FX_REVERB_GLOBAL_TYPE_REVB)
        next = FX_REVERB_GLOBAL_TYPE_REVB;
    else if(type == FX_REVERB_GLOBAL_TYPE_GVERB)
        next = FX_REVERB_GLOBAL_TYPE_GVERB;
    else if(type == FX_REVERB_GLOBAL_TYPE_OLIVERB)
        next = FX_REVERB_GLOBAL_TYPE_OLIVERB;
    if(g_reverb_global.type == next)
        return;

    g_reverb_global.type = next;
    if(next == FX_REVERB_GLOBAL_TYPE_REVB)
        fx_reverb_revb_global_reset();
    else if(next == FX_REVERB_GLOBAL_TYPE_GVERB)
        fx_reverb_gverb_global_reset();
    else if(next == FX_REVERB_GLOBAL_TYPE_OLIVERB)
        fx_reverb_oliverb_global_reset();
    else
        fx_reverb_drumboy_reset(&g_reverb_global.storage.drumboy);
    fx_reverb_global_apply_params();
}

void fx_reverb_global_set_wet(float wet)
{
    g_reverb_global.wet = fx_reverb_clamp01(wet);
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

void fx_reverb_global_set_predelay(float predelay_s)
{
    g_reverb_global.predelay_s = (predelay_s < 0.0f) ? 0.0f : predelay_s;
    fx_reverb_global_apply_params();
}

void fx_reverb_global_set_surround(float surround_s)
{
    g_reverb_global.surround_s = (surround_s < 0.0f) ? 0.0f : surround_s;
    fx_reverb_global_apply_params();
}

void fx_reverb_global_set_lpf(float lpf)
{
    g_reverb_global.lpf = fx_reverb_clamp01(lpf);
    fx_reverb_global_apply_params();
}

uint8_t fx_reverb_global_is_active(void)
{
    return (g_reverb_global.wet > 0.0f) ? 1U : 0U;
}

uint32_t fx_reverb_global_get_last_cycles(void)
{
    return g_reverb_global.last_cycles;
}

uint32_t fx_reverb_global_get_max_cycles(void)
{
    return g_reverb_global.max_cycles;
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

    const uint32_t t0 = DWT->CYCCNT;
    if(g_reverb_global.type == FX_REVERB_GLOBAL_TYPE_REVB)
    {
        if(frames > AUDIO_BLOCK_SIZE)
            frames = AUDIO_BLOCK_SIZE;
        for(uint32_t i = 0U; i < frames; ++i)
            g_reverb_global_mono[i] = 0.5f * (in_l[i] + in_r[i]);
        fx_reverb_revb_global_process_send_mono_to_stereo_wet(g_reverb_global_mono, out_l, out_r, frames);
    }
    else if(g_reverb_global.type == FX_REVERB_GLOBAL_TYPE_GVERB)
    {
        if(frames > AUDIO_BLOCK_SIZE)
            frames = AUDIO_BLOCK_SIZE;
        for(uint32_t i = 0U; i < frames; ++i)
            g_reverb_global_mono[i] = 0.5f * (in_l[i] + in_r[i]);
        fx_reverb_gverb_global_process_send_mono_to_stereo_wet(g_reverb_global_mono, out_l, out_r, frames);
    }
    else if(g_reverb_global.type == FX_REVERB_GLOBAL_TYPE_OLIVERB)
    {
        if(frames > AUDIO_BLOCK_SIZE)
            frames = AUDIO_BLOCK_SIZE;
        for(uint32_t i = 0U; i < frames; ++i)
            g_reverb_global_mono[i] = 0.5f * (in_l[i] + in_r[i]);
        fx_reverb_oliverb_global_process_send_mono_to_stereo_wet(g_reverb_global_mono, out_l, out_r, frames);
    }
    else
    {
        fx_reverb_drumboy_process_block(&g_reverb_global.storage.drumboy, in_l, in_r, out_l, out_r, frames);
    }
    const uint32_t elapsed = DWT->CYCCNT - t0;
    g_reverb_global.last_cycles = elapsed;
    if(elapsed > g_reverb_global.max_cycles)
        g_reverb_global.max_cycles = elapsed;
}
