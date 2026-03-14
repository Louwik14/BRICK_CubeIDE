/**
 * @file control_router.c
 * @brief Routage des paramètres de contrôle via registry centralisée.
 */

#include "control_router.h"

#include "param_registry.h"
#include "param_store.h"

void control_router_set_param(control_param_id_t id, float v)
{
    param_set((param_id_t)id, v);
    param_store_set_staging((param_id_t)id, param_get((param_id_t)id));
    (void)param_store_commit_if_block_advanced();
}
