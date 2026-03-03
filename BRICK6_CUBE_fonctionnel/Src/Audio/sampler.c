#include "sampler.h"
#include "sampler_stream.h"
#include <stdio.h>

#define DBG(...) printf(__VA_ARGS__)

void sample_voice_init(sample_voice_t *v)
{
    if(v == 0)
        return;

    v->active = false;
    v->gainL = 1.0f;
    v->gainR = 1.0f;
    v->pos = 0U;
    v->length = 0U;
    v->data = 0;
    v->loop = false;
    v->loop_start = 0U;
    v->loop_end = 0U;
}

void sample_voice_trigger(sample_voice_t *v, const float *data, uint32_t length)
{
    if(v == 0)
        return;

    v->data = data;
    v->length = length;
    v->pos = 0U;

    if((data == 0) || (length == 0U))
    {
        v->active = false;
        return;
    }

    /* Clamp loop end to valid sample length. */
    if(v->loop_end == 0U || v->loop_end > length)
        v->loop_end = length;

    if(v->loop_start >= v->loop_end)
    {
        v->loop_start = 0U;
        v->loop_end = length;
    }

    v->active = true;
}

void sampler_stop(sample_voice_t *v)
{
    if(v == 0)
        return;

    v->active = false;
    v->pos = 0U;
}

void sample_voice_process(sample_voice_t *v, float *outL, float *outR, uint32_t nframes)
{
    if((v == 0) || (outL == 0) || (outR == 0) || (nframes == 0U))
        return;

    if(!v->active)
        return;

    for(uint32_t i = 0U; i < nframes; i++)
    {
        uint32_t rp = g_stream_read_pos;

        if(rp == g_stream_write_pos)
        {
            g_stream_underrun_count++;
            break;
        }

        outL[i] += stream_buffer[rp] * v->gainL;
        rp = (rp + 1U) % (STREAM_BUFFER_FRAMES * 2U);
        outR[i] += stream_buffer[rp] * v->gainR;
        rp = (rp + 1U) % (STREAM_BUFFER_FRAMES * 2U);

        g_stream_read_pos = rp;
    }
}
