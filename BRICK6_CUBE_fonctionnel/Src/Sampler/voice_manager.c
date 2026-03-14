/**
 * @file voice_manager.c
 * @brief Module applicatif voice_manager.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à voice_manager.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "Sampler/voice_manager.h"

#include <math.h>
#include <stddef.h>

#include "audio_debug_log.h"

#define VOICE_MANAGER_MAX_VOICES (2U)

voice_t voices[VOICE_MANAGER_MAX_VOICES];

static uint32_t s_voice_generation[VOICE_MANAGER_MAX_VOICES];
static uint32_t s_generation_counter;
static uint32_t s_process_call_count;

/**
 * @brief Point d'entrée finite_or_zero.
 *
 * Rôle:
 * - Exécuter le traitement associé à finite_or_zero.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float finite_or_zero(float v)
{
    return isfinite(v) ? v : 0.0f;
}

/**
 * @brief Point d'entrée voice_clear.
 *
 * Rôle:
 * - Exécuter le traitement associé à voice_clear.
 *
 * @param index Paramètre d'entrée de l'API.
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
    voices[index].active = 0U;
    s_voice_generation[index] = 0U;
}

/**
 * @brief Point d'entrée voice_manager_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à voice_manager_init.
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
 * @brief Point d'entrée voice_manager_trigger.
 *
 * Rôle:
 * - Exécuter le traitement associé à voice_manager_trigger.
 *
 * @param sample_id Paramètre d'entrée de l'API.
 * @param gain_l Paramètre d'entrée de l'API.
 * @param gain_r Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void voice_manager_trigger(uint16_t sample_id, float gain_l, float gain_r)
{
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
    voices[target_index].active = 1U;
    s_voice_generation[target_index] = s_generation_counter++;

    if(s_generation_counter == 0U)
        s_generation_counter = 1U;
}

/**
 * @brief Point d'entrée voice_manager_service.
 *
 * Rôle:
 * - Exécuter le traitement associé à voice_manager_service.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void voice_manager_service(void)
{
}

/**
 * @brief Point d'entrée voice_manager_process.
 *
 * Rôle:
 * - Exécuter le traitement associé à voice_manager_process.
 *
 * @param out_l Paramètre d'entrée de l'API.
 * @param out_r Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
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
        if((sample_desc->valid == 0U) || (sample_desc->data == NULL) || (sample_desc->length_frames == 0U))
        {
            voice_clear(voice_index);
            continue;
        }

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
            const float sample_l = finite_or_zero(sample_desc->data[sample_index]);
            const float sample_r = finite_or_zero(sample_desc->data[sample_index + 1U]);

            out_l[frame] = finite_or_zero(out_l[frame] + (sample_l * voice->gain_l));
            out_r[frame] = finite_or_zero(out_r[frame] + (sample_r * voice->gain_r));

            position++;

            if((voice->loop_enabled != 0U) && (position >= voice->loop_end_frame))
                position = voice->loop_start_frame;
        }

        if(voice->active != 0U)
            voice->position = position;

        if((s_process_call_count <= 8U) || ((s_process_call_count % 512U) == 0U))
        {
            AUDIO_DEBUG_LOG("[VOICE] process idx=%lu active=%u state=%u pos=%lu\r\n",
                            (unsigned long)voice_index,
                            (unsigned int)voice->active,
                            (unsigned int)voice->state,
                            (unsigned long)voice->position);
        }
    }
}
