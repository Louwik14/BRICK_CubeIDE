#include "fx_reverb.h"

#include "../freeverb-main/Components/allpass.cpp"
#include "../freeverb-main/Components/comb.cpp"
#include "../freeverb-main/Components/revmodel.cpp"

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

void fx_reverb_set_bypass(fx_reverb_t *rev, uint8_t bypass)
{
    if(rev == 0)
        return;

    rev->bypass = bypass ? 1U : 0U;
}

fx_reverb_t *fx_reverb_get_instance(void)
{
    static fx_reverb_t reverb;
    return &reverb;
}
