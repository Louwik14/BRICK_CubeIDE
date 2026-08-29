/**
 * @file fx_chain.c
 * @brief Chaîne de traitement FX block-based sur slots du pool.
 *
 * Rôle du module:
 * - Appliquer un slot FX donné sur un buffer stéréo.
 * - Fournir les helpers de traitement par slot.
 *
 * Architecture:
 * - Appelé par: mixer.c.
 * - Appelle: fx_pool_get_slot, processeurs FX (EQ/SAT/COMP).
 *
 * Contraintes temps réel:
 * - IRQ: oui.
 * - Hard realtime: oui.
 * - malloc: interdit.
 *
 * Notes:
 * - Un slot inactif est ignoré immédiatement.
 */

#include "fx_chain.h"
#include "fx_pool.h"
#include "fx_dj_eq3_cmsis.h"
#include "fx_saturation.h"
#include "fx_comp_lab.h"
#include "Audio/audio_fx_runtime.h"

/**
 * @brief Traite un bloc stéréo avec un slot FX donné.
 *
 * @param s Slot FX à appliquer.
 * @param L Buffer canal gauche (in-place).
 * @param R Buffer canal droit (in-place).
 * @param frames Taille bloc en frames.
 *
 * Rôle:
 * - Router vers l'implémentation DSP correspondant au type du slot.
 *
 * Contexte d'appel:
 * - IRQ audio.
 *
 * Contraintes:
 * - Hard realtime, sans appel bloquant.
 */
/**
 * @brief Point d'entrée fx_chain_process_fx_slot.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_chain_process_fx_slot.
 *
 * @param s Paramètre d'entrée de l'API.
 * @param L Paramètre d'entrée de l'API.
 * @param R Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void fx_chain_process_fx_slot(fx_slot_t* s, float* L, float* R, uint32_t frames)
{
    if (!s || !s->active || !s->state)
        return;

    switch (s->type)
    {
        case FX_EQ3:
            fx_dj_eq3_process_block((fx_dj_eq3_t*)s->state, L, R, frames);
            break;

        case FX_SAT:
            fx_saturation_process_block((fx_saturation_t*)s->state, L, R, frames);
            break;

        case FX_COMP_LAB:
            fx_comp_lab_process_block((fx_comp_lab_t*)s->state, L, R, frames);
            break;

        default:
            break;
    }
}

/**
 * @brief Applique un slot unique du pool sur un bloc stéréo.
 *
 * @param slot Index du slot FX.
 * @param L Buffer canal gauche (in-place).
 * @param R Buffer canal droit (in-place).
 * @param frames Taille bloc en frames.
 *
 * Contexte d'appel:
 * - IRQ audio.
 */
/**
 * @brief Point d'entrée fx_chain_process_global_slot.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_chain_process_global_slot.
 *
 * @param slot Paramètre d'entrée de l'API.
 * @param L Paramètre d'entrée de l'API.
 * @param R Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_chain_process_global_slot(uint32_t slot, float* L, float* R, uint32_t frames)
{
    fx_chain_process_fx_slot(fx_pool_get_slot(slot), L, R, frames);
}

static void fx_chain_process_pool_track_slot(uint32_t track,
                                              uint32_t slot,
                                              float* L,
                                              float* R,
                                              uint32_t frames)
{
    fx_slot_t* s = fx_pool_get_slot(slot);
    if (!s || !s->active)
        return;

    if (s->type == FX_SAT)
    {
        fx_saturation_process_block((fx_saturation_t*)fx_pool_get_sat_state_for_track(track), L, R, frames);
        return;
    }

    fx_chain_process_fx_slot(s, L, R, frames);
}

static void fx_chain_process_pool_track_inserts(uint32_t pool_track,
                                                const int8_t *pool_slots,
                                                size_t pool_slot_count,
                                                float* L,
                                                float* R,
                                                uint32_t frames)
{
    if ((L == NULL) || (R == NULL))
    {
        return;
    }

    for (size_t insert = 0U;
         (pool_slots != NULL) && (insert < pool_slot_count);
         ++insert)
    {
        const int8_t slot = pool_slots[insert];
        if (slot >= 0)
        {
            fx_chain_process_pool_track_slot(pool_track,
                                                (uint32_t)slot,
                                                L,
                                                R,
                                                frames);
        }
    }

}

void fx_chain_process_track_inserts_pre_fader(brick_entity_id_t entity_id,
                                              uint32_t pool_track,
                                              const int8_t *pool_slots,
                                              size_t pool_slot_count,
                                              uint8_t process_audio_fx_comp,
                                              float* L,
                                              float* R,
                                              uint32_t frames)
{
    fx_chain_process_pool_track_inserts(pool_track,
                                        pool_slots,
                                        pool_slot_count,
                                        L,
                                        R,
                                        frames);
    if (process_audio_fx_comp != 0U)
    {
        audio_fx_runtime_process(entity_id, L, R, frames);
    }
}

void fx_chain_process_audio_fx_post_fader(brick_entity_id_t entity_id,
                                          float* L,
                                          float* R,
                                          uint32_t frames)
{
    audio_fx_runtime_process(entity_id, L, R, frames);
}

uint8_t fx_chain_track_inserts_require_stereo(brick_entity_id_t entity_id,
                                              const int8_t *pool_slots,
                                              size_t pool_slot_count)
{
    if (pool_slots != NULL)
    {
        for (size_t insert = 0U; insert < pool_slot_count; ++insert)
        {
            if (pool_slots[insert] >= 0)
            {
                return 1U;
            }
        }
    }

    return audio_fx_runtime_requires_stereo(entity_id);
}

uint8_t fx_chain_track_has_pre_fader_insert(brick_entity_id_t entity_id,
                                            const int8_t *pool_slots,
                                            size_t pool_slot_count)
{
    if (pool_slots != NULL)
    {
        for (size_t insert = 0U; insert < pool_slot_count; ++insert)
        {
            if (pool_slots[insert] >= 0)
                return 1U;
        }
    }
    return audio_fx_runtime_is_comp(entity_id);
}

uint8_t fx_chain_audio_fx_is_pre_filter(brick_entity_id_t entity_id)
{
    return (uint8_t)(audio_fx_runtime_get_placement(entity_id)
                     == AUDIO_FX_PLACEMENT_PRE_FILTER);
}

uint8_t fx_chain_audio_fx_is_active(brick_entity_id_t entity_id)
{
    return audio_fx_runtime_is_active(entity_id);
}

uint8_t fx_chain_audio_fx_is_comp(brick_entity_id_t entity_id)
{
    return audio_fx_runtime_is_comp(entity_id);
}

void fx_chain_process_audio_fx_pre_filter_mono(brick_entity_id_t entity_id,
                                               float *buffer,
                                               uint32_t frames)
{
    audio_fx_runtime_process_mono(entity_id, buffer, frames);
}

void fx_chain_process_audio_fx_pre_filter_stereo(brick_entity_id_t entity_id,
                                                 float *left,
                                                 float *right,
                                                 uint32_t frames)
{
    audio_fx_runtime_process_stereo(entity_id, left, right, frames);
}

float fx_chain_process_audio_fx_comp_mono_sample(brick_entity_id_t entity_id,
                                                 float sample)
{
    return audio_fx_runtime_process_mono_sample(entity_id, sample);
}

void fx_chain_process_audio_fx_comp_stereo_sample(brick_entity_id_t entity_id,
                                                  float *left,
                                                  float *right)
{
    audio_fx_runtime_process_stereo_sample(entity_id, left, right);
}

void fx_chain_process_audio_fx_post_fader_stereo_sample(
    brick_entity_id_t entity_id,
    float *left,
    float *right)
{
    audio_fx_runtime_process_stereo_sample(entity_id, left, right);
}
