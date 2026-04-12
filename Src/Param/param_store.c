/**
 * @file param_store.c
 * @brief Stockage double-buffer des paramètres de contrôle (staging/active).
 */

#include "param_store.h"

#include <string.h>

#include "audio_float.h"
#include "param_registry.h"
#include "stm32h7xx_hal.h"

typedef struct {
    float staging[PARAM_COUNT];
    float active[PARAM_COUNT];
    volatile uint32_t last_commit_block;
    volatile uint32_t commit_count;
    volatile uint8_t dirty;
} param_store_t;

static param_store_t g_ps;

/**
 * @brief Point d'entrée param_store_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_store_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void param_store_init(void)
{
    memset(&g_ps, 0, sizeof(g_ps));

    param_registry_init();

    for (uint32_t i = 0U; i < (uint32_t)PARAM_COUNT; i++)
    {
        const float v = param_registry[i].default_value;
        g_ps.staging[i] = v;
        g_ps.active[i] = v;
    }

    g_ps.last_commit_block = g_audio_block_counter;
}

/**
 * @brief Point d'entrée param_store_set_staging.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_store_set_staging.
 *
 * @param id Paramètre d'entrée de l'API.
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void param_store_set_staging(param_id_t id, float v)
{
    if (id >= PARAM_COUNT)
        return;

    g_ps.staging[id] = v;
    g_ps.dirty = 1U;
}

/**
 * @brief Point d'entrée param_store_set_active.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_store_set_active.
 *
 * @param id Paramètre d'entrée de l'API.
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void param_store_set_active(param_id_t id, float v)
{
    if (id >= PARAM_COUNT)
        return;

    g_ps.staging[id] = v;
    g_ps.active[id] = v;
}

/**
 * @brief Point d'entrée param_store_commit_if_block_advanced.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_store_commit_if_block_advanced.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
bool param_store_commit_if_block_advanced(void)
{
    if (g_ps.dirty == 0U)
        return false;

    uint32_t b = g_audio_block_counter;

    if ((uint32_t)(b - g_ps.last_commit_block) == 0U)
        return false;

    for (uint32_t i = 0U; i < (uint32_t)PARAM_COUNT; i++)
    {
        if (g_ps.active[i] == g_ps.staging[i])
            continue;

        param_set((param_id_t)i, g_ps.staging[i]);
        g_ps.active[i] = param_get((param_id_t)i);
    }

    __DMB();

    g_ps.last_commit_block = b;
    g_ps.commit_count++;
    g_ps.dirty = 0U;

    return true;
}

/**
 * @brief Point d'entrée param_store_get_active.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_store_get_active.
 *
 * @param id Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
float param_store_get_active(param_id_t id)
{
    if (id >= PARAM_COUNT)
        return 0.0f;

    return g_ps.active[id];
}

/**
 * @brief Point d'entrée param_store_get_commit_count.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_store_get_commit_count.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint32_t param_store_get_commit_count(void)
{
    return g_ps.commit_count;
}

/**
 * @brief Point d'entrée param_store_get_last_commit_block.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_store_get_last_commit_block.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint32_t param_store_get_last_commit_block(void)
{
    return g_ps.last_commit_block;
}
