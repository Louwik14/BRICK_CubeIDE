/**
 * @file sampler.c
 * @brief Module applicatif sampler.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à sampler.
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
 * - Legacy low-level sampler helper. The product track-aware Sampler runtime
 *   is brick6_sampler_runtime reading sample_cache; do not add new product
 *   audio ownership through this file.
 */

#include "sampler.h"

/**
 * @brief Point d'entrée sample_voice_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à sample_voice_init.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

/**
 * @brief Point d'entrée sample_voice_trigger.
 *
 * Rôle:
 * - Exécuter le traitement associé à sample_voice_trigger.
 *
 * @param v Paramètre d'entrée de l'API.
 * @param data Paramètre d'entrée de l'API.
 * @param length Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

/**
 * @brief Point d'entrée sampler_stop.
 *
 * Rôle:
 * - Exécuter le traitement associé à sampler_stop.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void sampler_stop(sample_voice_t *v)
{
    if(v == 0)
        return;

    v->active = false;
    v->pos = 0U;
}

/**
 * @brief Point d'entrée sample_voice_process.
 *
 * Rôle:
 * - Exécuter le traitement associé à sample_voice_process.
 *
 * @param v Paramètre d'entrée de l'API.
 * @param outL Paramètre d'entrée de l'API.
 * @param outR Paramètre d'entrée de l'API.
 * @param nframes Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
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

        v->pos += 2;

        if(v->loop && (v->pos >= v->loop_end))
        {
            v->pos = (v->loop_start < v->loop_end) ? v->loop_start : 0U;
        }
    }
}
