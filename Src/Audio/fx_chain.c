/**
 * @file fx_chain.c
 * @brief Chaîne de traitement FX block-based sur slots du pool.
 *
 * Rôle du module:
 * - Appliquer un slot FX donné sur un buffer stéréo.
 * - Fournir des helpers de chaînage (track0 complet ou slot unique).
 *
 * Architecture:
 * - Appelé par: mixer.c, audio_float.c (compat historique).
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
#include "fx_daisy_comp.h"

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

        case FX_DAISY_COMP:
            fx_daisy_comp_process_block((fx_daisy_comp_t*)s->state, L, R, frames);
            break;

        default:
            break;
    }
}

/**
 * @brief Applique la chaîne historique de track0 (slots 0..2).
 *
 * @param L Buffer canal gauche (in-place).
 * @param R Buffer canal droit (in-place).
 * @param frames Taille bloc en frames.
 *
 * Contexte d'appel:
 * - IRQ audio.
 */
/**
 * @brief Point d'entrée fx_chain_process_track0.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_chain_process_track0.
 *
 * @param L Paramètre d'entrée de l'API.
 * @param R Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_chain_process_track0(float* L, float* R, uint32_t frames)
{
    for (uint32_t i = 0; i < 3; i++)
    {
        fx_chain_process_fx_slot(fx_pool_get_slot(i), L, R, frames);
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
 * @brief Point d'entrée fx_chain_process_slot.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_chain_process_slot.
 *
 * @param slot Paramètre d'entrée de l'API.
 * @param L Paramètre d'entrée de l'API.
 * @param R Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_chain_process_slot(uint32_t slot, float* L, float* R, uint32_t frames)
{
    fx_chain_process_fx_slot(fx_pool_get_slot(slot), L, R, frames);
}

void fx_chain_process_slot_for_track(uint32_t track, uint32_t slot, float* L, float* R, uint32_t frames)
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

void fx_chain_process_slot_for_track_mono(uint32_t track, uint32_t slot, float* inout, uint32_t frames)
{
    fx_slot_t* s = fx_pool_get_slot(slot);
    if (!s || !s->active || !inout)
        return;

    if (s->type == FX_SAT)
    {
        fx_saturation_process_mono_block((fx_saturation_t*)fx_pool_get_sat_state_for_track(track), inout, frames);
    }
}
