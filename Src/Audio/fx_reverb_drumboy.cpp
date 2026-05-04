#include "fx_reverb_drumboy.h"
#include "Storage/memory_layout.h"

#include <string.h>

const uint32_t fx_reverb_drumboy_t::comb_size[8] = {1116U, 1188U, 1277U, 1356U, 1422U, 1491U, 1557U, 1617U};
const uint32_t fx_reverb_drumboy_t::apass_size[4] = {225U, 556U, 441U, 341U};

AUDIO_HOT static fx_reverb_drumboy_feedback_buffers_t g_reverb_drumboy_feedback_buffers;
AUDIO_LUT_D2 static fx_reverb_drumboy_delay_buffers_t g_reverb_drumboy_delay_buffers;

static inline float clamp01_local(float v)
{
    if(v < 0.0f)
        return 0.0f;
    if(v > 1.0f)
        return 1.0f;
    return v;
}

static inline uint32_t lag_to_offset(float sample_rate, float seconds, uint32_t buffer_size)
{
    if(sample_rate <= 0.0f)
        sample_rate = 48000.0f;

    const float max_lag = ((float)buffer_size - 1.0f) / sample_rate;
    const float clamped = (seconds < 0.0f) ? 0.0f : ((seconds > max_lag) ? max_lag : seconds);
    return (uint32_t)(clamped * sample_rate + 0.5f);
}

static inline float *comb_buffer_ptr(fx_reverb_drumboy_t *r, uint32_t idx)
{
    if((r == 0) || (r->feedback_buffers == 0))
        return 0;

    switch(idx)
    {
        case 0: return r->feedback_buffers->comb_buffer0;
        case 1: return r->feedback_buffers->comb_buffer1;
        case 2: return r->feedback_buffers->comb_buffer2;
        case 3: return r->feedback_buffers->comb_buffer3;
        case 4: return r->feedback_buffers->comb_buffer4;
        case 5: return r->feedback_buffers->comb_buffer5;
        case 6: return r->feedback_buffers->comb_buffer6;
        default: return r->feedback_buffers->comb_buffer7;
    }
}

static inline float *apass_buffer_ptr(fx_reverb_drumboy_t *r, uint32_t idx)
{
    if((r == 0) || (r->feedback_buffers == 0))
        return 0;

    switch(idx)
    {
        case 0: return r->feedback_buffers->apass_buffer0;
        case 1: return r->feedback_buffers->apass_buffer1;
        case 2: return r->feedback_buffers->apass_buffer2;
        default: return r->feedback_buffers->apass_buffer3;
    }
}

void fx_reverb_drumboy_reset(fx_reverb_drumboy_t *r)
{
    if(r == 0)
        return;

    if((r->delay_buffers == 0) || (r->feedback_buffers == 0))
        return;

    memset(r->delay_buffers->predelay_buffer, 0, sizeof(r->delay_buffers->predelay_buffer));
    memset(r->delay_buffers->surround_buffer, 0, sizeof(r->delay_buffers->surround_buffer));
    memset(r->comb_index, 0, sizeof(r->comb_index));
    memset(r->comb_filter, 0, sizeof(r->comb_filter));
    memset(r->feedback_buffers->comb_buffer0, 0, sizeof(r->feedback_buffers->comb_buffer0));
    memset(r->feedback_buffers->comb_buffer1, 0, sizeof(r->feedback_buffers->comb_buffer1));
    memset(r->feedback_buffers->comb_buffer2, 0, sizeof(r->feedback_buffers->comb_buffer2));
    memset(r->feedback_buffers->comb_buffer3, 0, sizeof(r->feedback_buffers->comb_buffer3));
    memset(r->feedback_buffers->comb_buffer4, 0, sizeof(r->feedback_buffers->comb_buffer4));
    memset(r->feedback_buffers->comb_buffer5, 0, sizeof(r->feedback_buffers->comb_buffer5));
    memset(r->feedback_buffers->comb_buffer6, 0, sizeof(r->feedback_buffers->comb_buffer6));
    memset(r->feedback_buffers->comb_buffer7, 0, sizeof(r->feedback_buffers->comb_buffer7));
    memset(r->apass_index, 0, sizeof(r->apass_index));
    memset(r->feedback_buffers->apass_buffer0, 0, sizeof(r->feedback_buffers->apass_buffer0));
    memset(r->feedback_buffers->apass_buffer1, 0, sizeof(r->feedback_buffers->apass_buffer1));
    memset(r->feedback_buffers->apass_buffer2, 0, sizeof(r->feedback_buffers->apass_buffer2));
    memset(r->feedback_buffers->apass_buffer3, 0, sizeof(r->feedback_buffers->apass_buffer3));

    const uint32_t predelay_lag = lag_to_offset(r->sample_rate, r->predelay_s, fx_reverb_drumboy_t::kPredelayBufferSize);
    const uint32_t surround_lag = lag_to_offset(r->sample_rate, r->surround_s, fx_reverb_drumboy_t::kSurroundBufferSize);

    r->predelay_play = 0U;
    r->predelay_write = predelay_lag;
    if(r->predelay_write >= fx_reverb_drumboy_t::kPredelayBufferSize)
        r->predelay_write = 0U;

    r->surround_play = 0U;
    r->surround_write = surround_lag;
    if(r->surround_write >= fx_reverb_drumboy_t::kSurroundBufferSize)
        r->surround_write = 0U;
}

void fx_reverb_drumboy_init(fx_reverb_drumboy_t *r, float sample_rate)
{
    if(r == 0)
        return;

    r->bypass = 0U;
    r->sample_rate = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    r->size = 0.25f;
    r->decay = 0.50f;
    r->predelay_s = 0.005f;
    r->surround_s = 0.005f;
    r->wet = 0.50f;
    r->apass_feedback = 0.5f;
    r->delay_buffers = &g_reverb_drumboy_delay_buffers;
    r->feedback_buffers = &g_reverb_drumboy_feedback_buffers;

    fx_reverb_drumboy_set_size(r, r->size);
    fx_reverb_drumboy_set_decay(r, r->decay);
    fx_reverb_drumboy_reset(r);
}

void fx_reverb_drumboy_set_size(fx_reverb_drumboy_t *r, float size_0_1)
{
    if(r == 0)
        return;

    r->size = clamp01_local(size_0_1);
    r->comb_feedback = (r->size * 0.28f) + 0.70f;
}

void fx_reverb_drumboy_set_decay(fx_reverb_drumboy_t *r, float decay_0_1)
{
    if(r == 0)
        return;

    r->decay = clamp01_local(decay_0_1);
    r->comb_decay1 = r->decay * 0.40f;
    r->comb_decay2 = 1.0f - r->comb_decay1;
}

void fx_reverb_drumboy_set_predelay(fx_reverb_drumboy_t *r, float predelay_s)
{
    if(r == 0)
        return;

    r->predelay_s = (predelay_s < 0.0f) ? 0.0f : predelay_s;
    const uint32_t lag = lag_to_offset(r->sample_rate, r->predelay_s, fx_reverb_drumboy_t::kPredelayBufferSize);
    if(r->predelay_write >= lag)
        r->predelay_play = r->predelay_write - lag;
    else
        r->predelay_play = fx_reverb_drumboy_t::kPredelayBufferSize + r->predelay_write - lag;
}

void fx_reverb_drumboy_set_surround(fx_reverb_drumboy_t *r, float surround_s)
{
    if(r == 0)
        return;

    r->surround_s = (surround_s < 0.0f) ? 0.0f : surround_s;
    const uint32_t lag = lag_to_offset(r->sample_rate, r->surround_s, fx_reverb_drumboy_t::kSurroundBufferSize);
    if(r->surround_write >= lag)
        r->surround_play = r->surround_write - lag;
    else
        r->surround_play = fx_reverb_drumboy_t::kSurroundBufferSize + r->surround_write - lag;
}

void fx_reverb_drumboy_set_wet(fx_reverb_drumboy_t *r, float wet_0_1)
{
    if(r == 0)
        return;

    r->wet = clamp01_local(wet_0_1);
}

void fx_reverb_drumboy_set_bypass(fx_reverb_drumboy_t *r, uint8_t bypass)
{
    if(r == 0)
        return;

    r->bypass = bypass ? 1U : 0U;
}

void fx_reverb_drumboy_process_block(fx_reverb_drumboy_t *r,
                                     const float *in_l,
                                     const float *in_r,
                                     float *out_l,
                                     float *out_r,
                                     uint32_t frames)
{
    if((r == 0) || (in_l == 0) || (in_r == 0) || (out_l == 0) || (out_r == 0))
        return;

    if((r->delay_buffers == 0) || (r->feedback_buffers == 0))
        return;

    if(r->bypass != 0U)
    {
        memset(out_l, 0, sizeof(float) * frames);
        memset(out_r, 0, sizeof(float) * frames);
        return;
    }

    for(uint32_t n = 0U; n < frames; ++n)
    {
        const float input = 0.5f * (in_l[n] + in_r[n]);

        r->delay_buffers->predelay_buffer[r->predelay_write] = input;
        const float reverb_input = r->delay_buffers->predelay_buffer[r->predelay_play];
        r->predelay_play = (r->predelay_play + 1U) % fx_reverb_drumboy_t::kPredelayBufferSize;
        r->predelay_write = (r->predelay_write + 1U) % fx_reverb_drumboy_t::kPredelayBufferSize;

        const float input_data = reverb_input * 0.015f;

        float comb_output = 0.0f;
        for(uint32_t i = 0U; i < 8U; ++i)
        {
            float *const buf = comb_buffer_ptr(r, i);
            const uint32_t idx = r->comb_index[i];
            const float out = buf[idx];
            r->comb_filter[i] = (out * r->comb_decay2) + (r->comb_filter[i] * r->comb_decay1);
            buf[idx] = input_data + (r->comb_filter[i] * r->comb_feedback);
            r->comb_index[i] = (idx + 1U >= fx_reverb_drumboy_t::comb_size[i]) ? 0U : (idx + 1U);
            comb_output += out;
        }

        float ap = comb_output;
        for(uint32_t i = 0U; i < 4U; ++i)
        {
            float *const buf = apass_buffer_ptr(r, i);
            const uint32_t idx = r->apass_index[i];
            const float delayed = buf[idx];
            const float out = delayed - ap;
            buf[idx] = ap + (delayed * r->apass_feedback);
            r->apass_index[i] = (idx + 1U >= fx_reverb_drumboy_t::apass_size[i]) ? 0U : (idx + 1U);
            ap = out;
        }

        const float wet = ap * r->wet;

        r->delay_buffers->surround_buffer[r->surround_write] = wet;
        const float delayed_r = r->delay_buffers->surround_buffer[r->surround_play];
        r->surround_play = (r->surround_play + 1U) % fx_reverb_drumboy_t::kSurroundBufferSize;
        r->surround_write = (r->surround_write + 1U) % fx_reverb_drumboy_t::kSurroundBufferSize;

        out_l[n] = wet;
        out_r[n] = delayed_r;
    }
}
