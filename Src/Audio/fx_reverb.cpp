#include "fx_reverb.h"
#include "fx_reverb_drumboy.h"

#include "../freeverb-main/Components/allpass.cpp"
#include "../freeverb-main/Components/comb.cpp"
#include "../freeverb-main/Components/revmodel.cpp"
#include <string.h>
#include <new>

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

    (void)sample_rate;
    rev->bypass = 0U;
    rev->model.mute();
    rev->model.setdry(0.0f);
    rev->model.setwet(0.30f);
    rev->model.setroomsize(0.70f);
    rev->model.setdamp(0.20f);
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

    rev->model.processreplace(in_l, in_r, out_l, out_r, (long)frames, 1);
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
    rev->model.setwet((float)wet_ui * (1.0f / 127.0f));
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

    rev->model.setroomsize(fx_reverb_clamp01(room));
}

void fx_reverb_set_damping(fx_reverb_t *rev, float damp)
{
    if(rev == 0)
        return;

    rev->model.setdamp(fx_reverb_clamp01(damp));
}

void fx_reverb_set_width(fx_reverb_t *rev, float width)
{
    if(rev == 0)
        return;

    rev->model.setwidth(fx_reverb_clamp01(width));
}

void fx_reverb_set_bypass(fx_reverb_t *rev, uint8_t bypass)
{
    if(rev == 0)
        return;

    rev->bypass = bypass ? 1U : 0U;
}

typedef union
{
    fx_reverb_t freeverb;
    fx_reverb_drumboy_t drumboy;
} fx_reverb_global_storage_t;

typedef struct
{
    fx_reverb_global_storage_t storage;
    volatile fx_reverb_global_type_t requested_type;
    volatile fx_reverb_global_type_t active_type;
    volatile uint8_t pending_reinit;
    volatile uint8_t backend_valid;
    float sample_rate;
    float wet;
    float size;
    float decay;
    float predelay_s;
    float surround_s;
} fx_reverb_global_state_t;

static fx_reverb_global_state_t g_reverb_global = {
    .requested_type = FX_REVERB_GLOBAL_TYPE_MONO,
    .active_type = FX_REVERB_GLOBAL_TYPE_MONO,
    .pending_reinit = 0U,
    .backend_valid = 0U,
    .sample_rate = 48000.0f,
    .wet = 0.0f,
    .size = 0.70f,
    .decay = 0.20f,
    .predelay_s = 0.0f,
    .surround_s = 0.018f,
};

static void fx_reverb_global_apply_params(void)
{
    if(g_reverb_global.backend_valid == 0U)
        return;

    if(g_reverb_global.active_type == FX_REVERB_GLOBAL_TYPE_STEREO)
    {
        fx_reverb_t *const rev = &g_reverb_global.storage.freeverb;
        fx_reverb_set_wet(rev, g_reverb_global.wet);
        fx_reverb_set_room_size(rev, g_reverb_global.size);
        fx_reverb_set_damping(rev, g_reverb_global.decay);
        fx_reverb_set_width(rev, g_reverb_global.surround_s);
    }
    else
    {
        fx_reverb_drumboy_t *const rev = &g_reverb_global.storage.drumboy;
        fx_reverb_drumboy_set_wet(rev, g_reverb_global.wet);
        fx_reverb_drumboy_set_size(rev, g_reverb_global.size);
        fx_reverb_drumboy_set_decay(rev, g_reverb_global.decay);
        fx_reverb_drumboy_set_predelay(rev, g_reverb_global.predelay_s);
        fx_reverb_drumboy_set_surround(rev, g_reverb_global.surround_s);
    }
}

static void fx_reverb_global_reinit_active_backend(void)
{
    if((g_reverb_global.backend_valid != 0U) && (g_reverb_global.active_type == FX_REVERB_GLOBAL_TYPE_STEREO))
    {
        g_reverb_global.storage.freeverb.~fx_reverb_t();
    }

    if(g_reverb_global.requested_type == FX_REVERB_GLOBAL_TYPE_STEREO)
    {
        new (&g_reverb_global.storage.freeverb) fx_reverb_t();
        fx_reverb_init(&g_reverb_global.storage.freeverb, g_reverb_global.sample_rate);
    }
    else
    {
        memset(&g_reverb_global.storage.drumboy, 0, sizeof(g_reverb_global.storage.drumboy));
        fx_reverb_drumboy_init(&g_reverb_global.storage.drumboy, g_reverb_global.sample_rate);
    }

    g_reverb_global.active_type = g_reverb_global.requested_type;
    g_reverb_global.backend_valid = 1U;
    g_reverb_global.pending_reinit = 0U;
    fx_reverb_global_apply_params();
}

void fx_reverb_global_init(float sample_rate)
{
    g_reverb_global.sample_rate = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    g_reverb_global.backend_valid = 0U;
    g_reverb_global.pending_reinit = 1U;
    fx_reverb_global_reinit_active_backend();
}

void fx_reverb_global_set_type(fx_reverb_global_type_t type)
{
    if(type != FX_REVERB_GLOBAL_TYPE_STEREO)
        type = FX_REVERB_GLOBAL_TYPE_MONO;

    if(g_reverb_global.requested_type == type)
        return;

    g_reverb_global.requested_type = type;
    if((g_reverb_global.backend_valid == 0U) || (g_reverb_global.active_type != g_reverb_global.requested_type))
    {
        g_reverb_global.pending_reinit = 1U;
    }
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

    if((g_reverb_global.pending_reinit != 0U)
       && ((g_reverb_global.backend_valid == 0U) || (g_reverb_global.active_type != g_reverb_global.requested_type)))
    {
        fx_reverb_global_reinit_active_backend();
        for(uint32_t i = 0; i < frames; i++)
        {
            out_l[i] = 0.0f;
            out_r[i] = 0.0f;
        }
        return;
    }

    if(g_reverb_global.backend_valid == 0U)
    {
        for(uint32_t i = 0; i < frames; i++)
        {
            out_l[i] = 0.0f;
            out_r[i] = 0.0f;
        }
        return;
    }

    if(g_reverb_global.active_type == FX_REVERB_GLOBAL_TYPE_STEREO)
    {
        fx_reverb_process_block(&g_reverb_global.storage.freeverb, in_l, in_r, out_l, out_r, frames);
        return;
    }

    fx_reverb_drumboy_process_block(&g_reverb_global.storage.drumboy, in_l, in_r, out_l, out_r, frames);
}
