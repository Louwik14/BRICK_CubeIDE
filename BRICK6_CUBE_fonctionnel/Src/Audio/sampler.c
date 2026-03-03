#include "sampler.h"
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

    if(!v->active || (v->data == 0) || (v->length == 0U))
        return;

    for(uint32_t i = 0U; i < nframes; i++)
    {
        if(v->pos >= v->length)
        {
            if(v->loop)
            {
                v->pos = (v->loop_start < v->length) ? v->loop_start : 0U;
            }
            else
            {
                v->active = false;
                break;
            }
        }

        const uint32_t idx = v->pos * 2U;
        outL[i] += v->data[idx] * v->gainL;
        outR[i] += v->data[idx + 1U] * v->gainR;

        v->pos++;

        if(v->loop && (v->pos >= v->loop_end))
        {
            v->pos = (v->loop_start < v->loop_end) ? v->loop_start : 0U;
        }
    }
}
