/**
 * @file voice_manager.c
 * @brief Module applicatif voice_manager.
 *
 * RÃ´le du module:
 * - ImplÃ©menter les traitements liÃ©s Ã  voice_manager.
 * - Fournir les services internes utilisÃ©s par le firmware utilisateur.
 *
 * Architecture:
 * - AppelÃ© par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dÃ©pendances matÃ©rielles et/ou modules utilisateur associÃ©s.
 *
 * Contraintes temps rÃ©el:
 * - IRQ: selon les API appelÃ©es.
 * - Hard realtime: selon le chemin d'exÃ©cution.
 * - malloc: Ã©viter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutÃ©e sans modification de la logique d'exÃ©cution.
 */

#include "Sampler/voice_manager.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>

#include "Audio/audio_float.h"
#include "Sampler/sample_cache.h"

#define VOICE_MANAGER_MAX_VOICES (2U)

voice_t voices[VOICE_MANAGER_MAX_VOICES];

static uint32_t s_voice_generation[VOICE_MANAGER_MAX_VOICES];
static uint32_t s_generation_counter;
static uint32_t s_process_call_count;

/**
 * @brief Point d'entrÃ©e finite_or_zero.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  finite_or_zero.
 *
 * @param v ParamÃ¨tre d'entrÃ©e de l'API.
 *
 * @return Valeur de retour dÃ©finie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float finite_or_zero(float v)
{
    return isfinite(v) ? v : 0.0f;
}

/**
 * @brief Point d'entrÃ©e voice_clear.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  voice_clear.
 *
 * @param index ParamÃ¨tre d'entrÃ©e de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void voice_clear(uint32_t index)
{
    voices[index].sample_id = 0U;
    voices[index].sample = NULL;
    voices[index].position = 0U;
    voices[index].gain_l = 0.0f;
    voices[index].gain_r = 0.0f;
    voices[index].loop_enabled = 0U;
    voices[index].loop_start_frame = 0U;
    voices[index].loop_end_frame = 0U;
    voices[index].state = VOICE_OFF;
    voices[index].use_sample_cache = 0U;
    voices[index].active = 0U;
    sample_cache_stop_voice((uint8_t)index);
    s_voice_generation[index] = 0U;
}

/**
 * @brief Point d'entrÃ©e voice_manager_init.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  voice_manager_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void voice_manager_init(void)
{
    for(uint32_t i = 0U; i < VOICE_MANAGER_MAX_VOICES; i++)
        voice_clear(i);

    s_generation_counter = 1U;
    s_process_call_count = 0U;
}

/**
 * @brief Point d'entrÃ©e voice_manager_trigger.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  voice_manager_trigger.
 *
 * @param sample_id ParamÃ¨tre d'entrÃ©e de l'API.
 * @param gain_l ParamÃ¨tre d'entrÃ©e de l'API.
 * @param gain_r ParamÃ¨tre d'entrÃ©e de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void voice_manager_trigger(uint16_t sample_id, float gain_l, float gain_r)
{
    /*
     * Legacy compat path. The track-aware Sampler runtime is sample_cache-only;
     * keep this fallback for older callers until they are migrated.
     */
    const sample_desc_t *sample_desc = sample_pool_get(sample_id);
    if((sample_desc == NULL) || (sample_desc->valid == 0U) ||
       (sample_desc->data == NULL) || (sample_desc->length_frames == 0U))
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
        voice_clear(target_index);
    }

    voices[target_index].sample_id = sample_id;
    voices[target_index].sample = sample_desc;
    voices[target_index].position = 0U;
    voices[target_index].gain_l = finite_or_zero(gain_l);
    voices[target_index].gain_r = finite_or_zero(gain_r);
    voices[target_index].loop_enabled = 1U;
    voices[target_index].loop_start_frame = 0U;
    voices[target_index].loop_end_frame = sample_desc->length_frames;
    voices[target_index].state = VOICE_ON;
    voices[target_index].use_sample_cache = sample_cache_start_voice(sample_id, (uint8_t)target_index);
    voices[target_index].active = 1U;
    s_voice_generation[target_index] = s_generation_counter++;

    if(s_generation_counter == 0U)
        s_generation_counter = 1U;
}

/**
 * @brief Point d'entrÃ©e voice_manager_service.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  voice_manager_service.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void voice_manager_service(void)
{
}

static uint8_t voice_has_valid_sample(uint32_t voice_index, const voice_t *voice, const sample_desc_t *sample_desc)
{
    (void)voice_index;

    if((voice == NULL) || (sample_desc == NULL))
        return 0U;

    if(voice->sample_id >= SAMPLE_POOL_SIZE)
        return 0U;

    const sample_desc_t *pool_desc = sample_pool_get(voice->sample_id);
    if((pool_desc == NULL) || (pool_desc != sample_desc))
        return 0U;

    if((sample_desc->valid == 0U) || (sample_desc->data == NULL) || (sample_desc->length_frames == 0U))
        return 0U;

    if(sample_desc->length_frames > (UINT32_MAX / 2U))
        return 0U;

    if(voice->loop_start_frame >= sample_desc->length_frames)
        return 0U;

    if(voice->loop_enabled != 0U)
    {
        if((voice->loop_end_frame == 0U) ||
           (voice->loop_end_frame > sample_desc->length_frames) ||
           (voice->loop_start_frame >= voice->loop_end_frame))
            return 0U;
    }

    return 1U;
}

static void voice_manager_process_cache_voice(uint32_t voice_index,
                                              voice_t *voice,
                                              float *out_l,
                                              float *out_r,
                                              uint32_t frames)
{
    static float cache_l[VOICE_MANAGER_MAX_VOICES][AUDIO_BLOCK_SIZE];
    static float cache_r[VOICE_MANAGER_MAX_VOICES][AUDIO_BLOCK_SIZE];

    if ((voice_index >= VOICE_MANAGER_MAX_VOICES) || (voice == NULL)
        || (out_l == NULL) || (out_r == NULL) || (frames == 0U)
        || (frames > AUDIO_BLOCK_SIZE))
    {
        return;
    }

    uint32_t offset = 0U;
    while ((offset < frames) && (voice->active != 0U))
    {
        const uint32_t remaining = frames - offset;
        for (uint32_t i = 0U; i < remaining; ++i)
        {
            cache_l[voice_index][i] = 0.0f;
            cache_r[voice_index][i] = 0.0f;
        }

        const uint32_t produced = sample_cache_read_voice((uint8_t)voice_index,
                                                          cache_l[voice_index],
                                                          cache_r[voice_index],
                                                          remaining);
        for (uint32_t i = 0U; i < produced; ++i)
        {
            out_l[offset + i] = finite_or_zero(out_l[offset + i]
                                               + (cache_l[voice_index][i] * voice->gain_l));
            out_r[offset + i] = finite_or_zero(out_r[offset + i]
                                               + (cache_r[voice_index][i] * voice->gain_r));
        }

        offset += produced;
        if (produced == remaining)
        {
            break;
        }

        if ((voice->loop_enabled == 0U)
            || (sample_cache_start_voice(voice->sample_id, (uint8_t)voice_index) == 0U))
        {
            voice_clear(voice_index);
            break;
        }
    }
}

/**
 * @brief Point d'entrÃ©e voice_manager_process.
 *
 * RÃ´le:
 * - ExÃ©cuter le traitement associÃ© Ã  voice_manager_process.
 *
 * @param out_l ParamÃ¨tre d'entrÃ©e de l'API.
 * @param out_r ParamÃ¨tre d'entrÃ©e de l'API.
 * @param frames ParamÃ¨tre d'entrÃ©e de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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
        if(voice_has_valid_sample(voice_index, voice, sample_desc) == 0U)
        {
            voice_clear(voice_index);
            continue;
        }

        if ((voice->use_sample_cache != 0U) && (frames <= AUDIO_BLOCK_SIZE))
        {
            voice_manager_process_cache_voice(voice_index, voice, out_l, out_r, frames);
            continue;
        }

        const uint32_t sample_data_len = sample_desc->length_frames * 2U;
        uint32_t position = voice->position;

        for(uint32_t frame = 0U; frame < frames; frame++)
        {
            if(position >= sample_desc->length_frames)
            {
                if(voice->loop_enabled != 0U)
                    position = voice->loop_start_frame;
                else
                {
                    voice_clear(voice_index);
                    break;
                }
            }

            const uint32_t sample_index = position * 2U;
            if((sample_index + 1U) >= sample_data_len)
            {
                voice_clear(voice_index);
                break;
            }

            const float sample_l = finite_or_zero(sample_desc->data[sample_index]);
            const float sample_r = finite_or_zero(sample_desc->data[sample_index + 1U]);

            out_l[frame] = finite_or_zero(out_l[frame] + (sample_l * voice->gain_l));
            out_r[frame] = finite_or_zero(out_r[frame] + (sample_r * voice->gain_r));

            position++;

            if((voice->loop_enabled != 0U) && (position >= voice->loop_end_frame))
                position = voice->loop_start_frame;
        }

        if(voice->active != 0U)
            voice->position = position;    }
}


