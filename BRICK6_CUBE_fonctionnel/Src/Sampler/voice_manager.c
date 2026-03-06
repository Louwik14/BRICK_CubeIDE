#include "Sampler/voice_manager.h"

#include <math.h>
#include <stddef.h>

#define VOICE_MANAGER_MAX_VOICES (24U)

voice_t voices[VOICE_MANAGER_MAX_VOICES];

static uint32_t s_voice_generation[VOICE_MANAGER_MAX_VOICES];
static uint32_t s_generation_counter;

static float finite_or_zero(float v)
{
    return isfinite(v) ? v : 0.0f;
}

static void voice_clear(uint32_t index)
{
    voices[index].sample_id = 0U;
    voices[index].sample = NULL;
    voices[index].position = 0U;
    voices[index].gain_l = 0.0f;
    voices[index].gain_r = 0.0f;
    voices[index].state = VOICE_OFF;
    voices[index].active = 0U;
    s_voice_generation[index] = 0U;
}

void voice_manager_init(void)
{
    for(uint32_t i = 0U; i < VOICE_MANAGER_MAX_VOICES; i++)
        voice_clear(i);

    s_generation_counter = 1U;
}

void voice_manager_trigger(uint16_t sample_id, float gain_l, float gain_r)
{
    const sample_desc_t *sample_desc = sample_pool_get(sample_id);
    if((sample_desc == NULL) || (sample_desc->valid == 0U) || (sample_desc->attack_cache == NULL) ||
       (sample_desc->attack_frames == 0U))
        return;

    uint32_t target_index = VOICE_MANAGER_MAX_VOICES;

    for(uint32_t i = 0U; i < VOICE_MANAGER_MAX_VOICES; i++)
    {
        if(voices[i].active == 0U)
        {
            target_index = i;
            break;
        }
    }

    if(target_index >= VOICE_MANAGER_MAX_VOICES)
    {
        uint32_t oldest_index = 0U;
        uint32_t oldest_generation = s_voice_generation[0U];

        for(uint32_t i = 1U; i < VOICE_MANAGER_MAX_VOICES; i++)
        {
            if(s_voice_generation[i] < oldest_generation)
            {
                oldest_generation = s_voice_generation[i];
                oldest_index = i;
            }
        }

        target_index = oldest_index;
    }

    voices[target_index].sample_id = sample_id;
    voices[target_index].sample = sample_desc;
    voices[target_index].position = 0U;
    voices[target_index].gain_l = finite_or_zero(gain_l);
    voices[target_index].gain_r = finite_or_zero(gain_r);
    voices[target_index].state = VOICE_ATTACK;
    voices[target_index].active = 1U;
    s_voice_generation[target_index] = s_generation_counter++;

    if(s_generation_counter == 0U)
        s_generation_counter = 1U;
}

void voice_manager_process(float *out_l, float *out_r, uint32_t frames)
{
    if((out_l == NULL) || (out_r == NULL) || (frames == 0U))
        return;

    for(uint32_t voice_index = 0U; voice_index < VOICE_MANAGER_MAX_VOICES; voice_index++)
    {
        voice_t *voice = &voices[voice_index];

        if((voice->active == 0U) || (voice->state != VOICE_ATTACK) || (voice->sample == NULL))
            continue;

        const sample_desc_t *sample_desc = voice->sample;
        if((sample_desc->valid == 0U) || (sample_desc->attack_cache == NULL) || (sample_desc->attack_frames == 0U) ||
           (voice->position >= sample_desc->attack_frames))
        {
            voice_clear(voice_index);
            continue;
        }

        const float *cache = sample_desc->attack_cache;
        const uint32_t attack_frames = sample_desc->attack_frames;
        uint32_t position = voice->position;

        for(uint32_t frame = 0U; frame < frames; frame++)
        {
            if(position >= attack_frames)
            {
                voice_clear(voice_index);
                break;
            }

            const uint32_t sample_index = position * 2U;
            float sample_l = finite_or_zero(cache[sample_index]);
            float sample_r = finite_or_zero(cache[sample_index + 1U]);

            out_l[frame] = finite_or_zero(out_l[frame] + (sample_l * voice->gain_l));
            out_r[frame] = finite_or_zero(out_r[frame] + (sample_r * voice->gain_r));

            position++;
        }

        if(voice->active != 0U)
        {
            voice->position = position;
            if(voice->position >= attack_frames)
                voice_clear(voice_index);
        }
    }
}
