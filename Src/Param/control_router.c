/**
 * @file control_router.c
 * @brief Routage des paramètres de contrôle via registry centralisée.
 */

#include "control_router.h"

#include "param_registry.h"
#include "param_store.h"

/**
 * @brief Point d'entrée control_router_set_param.
 *
 * Rôle:
 * - Exécuter le traitement associé à control_router_set_param.
 *
 * @param id Paramètre d'entrée de l'API.
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void control_router_set_param(control_param_id_t id, float v)
{
    param_set((param_id_t)id, v);
    param_store_set_staging((param_id_t)id, param_get((param_id_t)id));
    (void)param_store_commit_if_block_advanced();
}
