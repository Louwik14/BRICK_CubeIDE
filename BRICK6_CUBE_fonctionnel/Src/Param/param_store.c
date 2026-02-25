/**
 * @file param_store.c
 * @brief Stockage double-buffer des paramètres de contrôle (staging/active).
 *
 * Rôle du module:
 * - Recevoir des updates de paramètres côté contrôle (staging).
 * - Publier une vue active cohérente synchronisée sur l'avancement des blocs audio.
 *
 * Architecture:
 * - Appelé par: control_router.c, DSP audio (lecture active).
 * - Appelle: aucun module applicatif.
 *
 * Contraintes temps réel:
 * - IRQ: oui (lecture active en DSP, commit potentiellement hors IRQ).
 * - Hard realtime: oui.
 * - malloc: interdit.
 *
 * Notes:
 * - Le commit est déclenché uniquement si au moins un nouveau bloc audio est passé.
 */

#include "param_store.h"
#include "audio_float.h"
#include "stm32h7xx_hal.h"
#include <string.h>

typedef struct {
    float staging[PARAM_COUNT];
    float active[PARAM_COUNT];
    volatile uint32_t last_commit_block;
    volatile uint32_t commit_count;
    volatile uint8_t dirty;
} param_store_t;

/** État global du store paramètres. */
static param_store_t g_ps;

/**
 * @brief Initialise le store paramètres.
 *
 * Rôle:
 * - Remet à zéro staging/active et synchronise le block counter courant.
 *
 * Contexte d'appel:
 * - Init application (main loop).
 */
void param_store_init(void)
{
    memset(&g_ps, 0, sizeof(g_ps));
    g_ps.last_commit_block = g_audio_block_counter;
}

/**
 * @brief Écrit une valeur de paramètre en zone staging.
 *
 * @param id Identifiant de paramètre.
 * @param v Valeur flottante à stocker.
 *
 * Rôle:
 * - Préparer une mise à jour atomiquement publiable au prochain commit.
 *
 * Contexte d'appel:
 * - Tasklet/UI/contrôle.
 */
void param_store_set_staging(param_id_t id, float v)
{
    if (id >= PARAM_COUNT) return;
    g_ps.staging[id] = v;
    g_ps.dirty = 1U;
}

/**
 * @brief Commit les valeurs staging vers active si un nouveau bloc audio est passé.
 *
 * @return true si un commit a été effectué, false sinon.
 *
 * Rôle:
 * - Garantir une publication cohérente synchronisée au rythme audio.
 *
 * Contexte d'appel:
 * - Contrôle/tasklet (non critique audio).
 *
 * Contraintes:
 * - Pas de blocage, barrière mémoire DMB pour visibilité ordonnée.
 */
bool param_store_commit_if_block_advanced(void)
{
    if (g_ps.dirty == 0U) return false;

    uint32_t b = g_audio_block_counter;

    // overflow-safe compare
    if ((uint32_t)(b - g_ps.last_commit_block) == 0U)
        return false;

    memcpy(g_ps.active, g_ps.staging, sizeof(g_ps.active));

    __DMB();

    g_ps.last_commit_block = b;
    g_ps.commit_count++;
    g_ps.dirty = 0U;

    return true;
}

/**
 * @brief Lit la valeur active d'un paramètre.
 *
 * @param id Identifiant de paramètre.
 *
 * @return Valeur active, ou 0.0f si identifiant invalide.
 *
 * Contexte d'appel:
 * - DSP IRQ ou tasklet.
 */
float param_store_get_active(param_id_t id)
{
    if (id >= PARAM_COUNT) return 0.0f;
    return g_ps.active[id];
}

/**
 * @brief Retourne le nombre total de commits effectués.
 *
 * @return Compteur de commits.
 */
uint32_t param_store_get_commit_count(void)
{
    return g_ps.commit_count;
}

/**
 * @brief Retourne l'index de bloc audio du dernier commit.
 *
 * @return Numéro de bloc du dernier commit.
 */
uint32_t param_store_get_last_commit_block(void)
{
    return g_ps.last_commit_block;
}
