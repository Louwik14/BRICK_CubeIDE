#include "Sampler/voice_manager.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "audio_debug_log.h"

#include "Streaming/stream_manager.h"

#define VOICE_MANAGER_MAX_VOICES (24U)
#define VOICE_MANAGER_INVALID_STREAMER_ID (0xFFU)

voice_t voices[VOICE_MANAGER_MAX_VOICES];

static uint32_t s_voice_generation[VOICE_MANAGER_MAX_VOICES];
static uint32_t s_generation_counter;
static uint32_t s_process_call_count;

static float finite_or_zero(float v)
{
    return isfinite(v) ? v : 0.0f;
}

static void voice_clear(uint32_t index)
{
    if((index < VOICE_MANAGER_MAX_VOICES) &&
       (voices[index].streamer_id != VOICE_MANAGER_INVALID_STREAMER_ID))
    {
        stream_manager_stop_stream(voices[index].streamer_id);
    }

    voices[index].sample_id = 0U;
    voices[index].sample = NULL;
    voices[index].position = 0U;
    voices[index].stream_pos_frames = 0U;
    voices[index].streamer_id = VOICE_MANAGER_INVALID_STREAMER_ID;
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
    s_process_call_count = 0U;
}

void voice_manager_trigger(uint16_t sample_id, float gain_l, float gain_r)
{
    AUDIO_DEBUG_LOG("[VOICE] trigger sample=%u\r\n", (unsigned int)sample_id);

    const sample_desc_t *sample_desc = sample_pool_get(sample_id);
    if(sample_desc == NULL)
    {
        AUDIO_DEBUG_LOG("[VOICE] trigger rejected: sample_desc=NULL\r\n");
        return;
    }

    if(sample_desc->valid == 0U)
    {
        AUDIO_DEBUG_LOG("[VOICE] trigger rejected: sample invalid\r\n");
        return;
    }

    if(sample_desc->attack_cache == NULL)
    {
        AUDIO_DEBUG_LOG("[VOICE] trigger rejected: attack_cache=NULL\r\n");
        return;
    }

    if(sample_desc->attack_frames == 0U)
    {
        AUDIO_DEBUG_LOG("[VOICE] trigger rejected: attack_frames=0\r\n");
        return;
    }

    if(sample_desc->length_frames <= sample_desc->attack_frames)
    {
        AUDIO_DEBUG_LOG("[VOICE] trigger rejected: length=%lu attack=%lu\r\n",
               (unsigned long)sample_desc->length_frames,
               (unsigned long)sample_desc->attack_frames);
        return;
    }

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
        voice_clear(target_index);
    }

    uint8_t streamer_id = VOICE_MANAGER_INVALID_STREAMER_ID;
    if(!stream_manager_start_stream(sample_desc, sample_desc->attack_frames, &streamer_id))
    {
        AUDIO_DEBUG_LOG("[VOICE] trigger rejected: stream_manager_start_stream failed\r\n");
        return;
    }

    voices[target_index].sample_id = sample_id;
    voices[target_index].sample = sample_desc;
    voices[target_index].position = 0U;
    voices[target_index].stream_pos_frames = sample_desc->attack_frames;
    voices[target_index].streamer_id = streamer_id;
    voices[target_index].gain_l = finite_or_zero(gain_l);
    voices[target_index].gain_r = finite_or_zero(gain_r);
    voices[target_index].state = VOICE_ATTACK;
    voices[target_index].active = 1U;
    s_voice_generation[target_index] = s_generation_counter++;

    AUDIO_DEBUG_LOG("[VOICE] created idx=%lu active=%u state=%u streamer=%u\r\n",
           (unsigned long)target_index,
           (unsigned int)voices[target_index].active,
           (unsigned int)voices[target_index].state,
           (unsigned int)voices[target_index].streamer_id);

    if(s_generation_counter == 0U)
        s_generation_counter = 1U;
}

void voice_manager_process(float *out_l, float *out_r, uint32_t frames)
{
    if((out_l == NULL) || (out_r == NULL) || (frames == 0U))
        return;

    s_process_call_count++;

    for(uint32_t voice_index = 0U; voice_index < VOICE_MANAGER_MAX_VOICES; voice_index++)
    {
        voice_t *voice = &voices[voice_index];

        if((voice->active == 0U) || (voice->sample == NULL))
            continue;

        const sample_desc_t *sample_desc = voice->sample;
        if((sample_desc->valid == 0U) || (sample_desc->attack_cache == NULL) || (sample_desc->attack_frames == 0U))
        {
            voice_clear(voice_index);
            continue;
        }

        uint32_t position = voice->position;
        uint32_t stream_pos = voice->stream_pos_frames;
        voice_state_t initial_state = voice->state;

        for(uint32_t frame = 0U; frame < frames; frame++)
        {
            float sample_l = 0.0f;
            float sample_r = 0.0f;

            if(voice->state == VOICE_ATTACK)
            {
                if(position < sample_desc->attack_frames)
                {
                    const uint32_t sample_index = position * 2U;
                    sample_l = finite_or_zero(sample_desc->attack_cache[sample_index]);
                    sample_r = finite_or_zero(sample_desc->attack_cache[sample_index + 1U]);
                    position++;
                }

                if(position >= sample_desc->attack_frames)
                {
                    voice->state = VOICE_STREAM;

                    AUDIO_DEBUG_LOG("[VOICE] attack->stream idx=%lu pos=%lu attack=%lu streamer=%u\r\n",
                           (unsigned long)voice_index,
                           (unsigned long)position,
                           (unsigned long)sample_desc->attack_frames,
                           (unsigned int)voice->streamer_id);
                }
            }

            if(voice->state == VOICE_STREAM)
            {
                if(stream_pos >= sample_desc->length_frames)
                {
                    voice_clear(voice_index);
                    break;
                }

                if(!stream_manager_get_stream_frame(voice->streamer_id, &sample_l, &sample_r))
                {
                    /* streamer pas encore prêt → silence temporaire */
                    sample_l = 0.0f;
                    sample_r = 0.0f;
                }

                sample_l = finite_or_zero(sample_l);
                sample_r = finite_or_zero(sample_r);
                stream_pos++;
            }

            out_l[frame] = finite_or_zero(out_l[frame] + (sample_l * voice->gain_l));
            out_r[frame] = finite_or_zero(out_r[frame] + (sample_r * voice->gain_r));
        }

        if(voice->active != 0U)
        {
            voice->position = position;
            voice->stream_pos_frames = stream_pos;

            if((voice->state == VOICE_STREAM) && (voice->stream_pos_frames >= sample_desc->length_frames))
                voice_clear(voice_index);
        }

        if((s_process_call_count <= 8U) || ((s_process_call_count % 512U) == 0U) ||
           (initial_state != voice->state))
        {
            AUDIO_DEBUG_LOG("[VOICE] process idx=%lu active=%u state=%u pos=%lu stream_pos=%lu\r\n",
                   (unsigned long)voice_index,
                   (unsigned int)voice->active,
                   (unsigned int)voice->state,
                   (unsigned long)voice->position,
                   (unsigned long)voice->stream_pos_frames);
        }
    }
}
