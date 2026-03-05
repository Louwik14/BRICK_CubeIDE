#include "sampler.h"
#include "audio_streamer.h"

volatile uint32_t audio_underrun_count = 0U;

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
        float l = 0.0f;
        float r = 0.0f;
        uint32_t prev_underrun = audio_streamer_get_stats()->underrun_count;

        audio_streamer_get_frame(&l, &r);

        outL[i] += l * v->gainL;
        outR[i] += r * v->gainR;

        if(audio_streamer_get_stats()->underrun_count != prev_underrun)
        {
            audio_underrun_count++;
        }
    }
}
