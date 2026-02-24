#include "control_router.h"
#include "param_store.h"

void control_router_set_param(control_param_id_t id, float v)
{
    param_store_set_staging((param_id_t)id, v);
    (void)param_store_commit_if_block_advanced();
}
