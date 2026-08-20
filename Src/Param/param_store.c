/**
 * @file param_store.c
 * @brief Stockage des paramètres actifs.
 */

#include "param_store.h"

#include <string.h>

#include "param_registry.h"

typedef struct {
    float active[PARAM_COUNT];
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
        g_ps.active[i] = v;
    }
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
    if ((id >= PARAM_COUNT) || (param_id_is_reserved(id) != 0U))
        return;

    g_ps.active[id] = v;
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
