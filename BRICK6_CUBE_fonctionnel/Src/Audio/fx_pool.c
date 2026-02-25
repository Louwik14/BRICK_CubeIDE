/**
 * @file fx_pool.c
 * @brief Pool statique de slots FX partagés par le mixer et les chaînes d'effets.
 *
 * Rôle du module:
 * - Déclarer les instances d'état FX (EQ, saturation, granular).
 * - Exposer un accès indexé à des slots FX persistants.
 *
 * Architecture:
 * - Appelé par: brick6_app_init.c, mixer.c, fx_chain.c.
 * - Appelle: aucun module externe (hors types FX).
 *
 * Contraintes temps réel:
 * - IRQ: oui (lecture de slots depuis le DSP).
 * - Hard realtime: oui.
 * - malloc: interdit (pool 100% statique).
 *
 * Notes:
 * - La taille du pool est fixe (FX_POOL_SIZE).
 */

#include "fx_pool.h"
#include "fx_dj_eq3_cmsis.h"
#include "fx_saturation.h"
#include "fx_granular.h"

#define FX_POOL_SIZE 3

/** Table des slots FX exposée au moteur. */
static fx_slot_t g_slots[FX_POOL_SIZE];

/** États DSP persistants associés aux slots. */
static fx_dj_eq3_t g_eq;
static fx_saturation_t g_sat;
static uint8_t g_gran;

/**
 * @brief Initialise le pool de slots FX avec le mapping par défaut.
 *
 * Rôle:
 * - Associe chaque slot à un type FX et à son état mémoire persistant.
 *
 * Contexte d'appel:
 * - Init application (main loop), avant démarrage audio.
 *
 * Contraintes:
 * - Pas d'allocation, pas de blocage.
 */
void fx_pool_init(void)
{
    g_slots[0].active = 1;
    g_slots[0].type = FX_EQ3;
    g_slots[0].state = &g_eq;

    g_slots[1].active = 1;
    g_slots[1].type = FX_SAT;
    g_slots[1].state = &g_sat;

    g_slots[2].active = 1;
    g_slots[2].type = FX_GRANULAR;
    g_slots[2].state = &g_gran;
}

/**
 * @brief Retourne un pointeur sur un slot FX du pool.
 *
 * @param index Index de slot demandé.
 *
 * @return Pointeur sur le slot si valide, sinon NULL.
 *
 * Rôle:
 * - Fournir un accès sûr aux slots pour le routing mixer/fx_chain.
 *
 * Contexte d'appel:
 * - Init, tasklet ou IRQ audio (lecture).
 */
fx_slot_t* fx_pool_get_slot(uint32_t index)
{
    if (index >= FX_POOL_SIZE) return 0;
    return &g_slots[index];
}
