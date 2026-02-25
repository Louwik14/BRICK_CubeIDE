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
 * - Appelle: fx_pool_get_slot, processeurs FX (EQ/SAT/GRANULAR).
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
#include "fx_granular.h"

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
static void fx_chain_process_fx_slot(fx_slot_t* s, float* L, float* R, uint32_t frames)
{
    if (!s || !s->active)
        return;

    switch (s->type)
    {
        case FX_EQ3:
            fx_dj_eq3_process_block((fx_dj_eq3_t*)s->state, L, R, frames);
            break;

        case FX_SAT:
            fx_saturation_process_block((fx_saturation_t*)s->state, L, R, frames);
            break;

        case FX_GRANULAR:
            fx_granular_process_block(L, R, L, R, frames);
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
void fx_chain_process_slot(uint32_t slot, float* L, float* R, uint32_t frames)
{
    fx_chain_process_fx_slot(fx_pool_get_slot(slot), L, R, frames);
}
