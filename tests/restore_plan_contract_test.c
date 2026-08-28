#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "Storage/restore_control_workspace.h"

int main(void)
{
    restore_control_workspace_t plan = {0};
    assert(RESTORE_PLAN_MAX_TRACK_ITEMS == 5120U);
    assert(RESTORE_PLAN_MAX_GLOBAL_ITEMS == 128U);
    assert(RESTORE_PLAN_MAX_ITEMS == 5248U);
    assert(sizeof(restore_control_workspace_t) == 42336U);
    assert(offsetof(restore_control_workspace_t, items) == 352U);
    assert((sizeof(restore_control_workspace_t) % RESTORE_PLAN_CACHE_LINE_BYTES) == 0U);

    plan.header.magic = RESTORE_PLAN_MAGIC;
    plan.header.abi_version = RESTORE_PLAN_ABI_VERSION;
    plan.header.header_bytes = (uint16_t)sizeof(plan.header);
    plan.header.plan_bytes = (uint32_t)sizeof(plan);
    plan.header.item_count = (uint16_t)RESTORE_PLAN_MAX_ITEMS;
    plan.header.program_count = (uint8_t)PERSIST_CONTROL_ENTITY_COUNT;
    plan.items[RESTORE_PLAN_MAX_ITEMS - 1U].param_id = (uint16_t)(PARAM_COUNT - 1U);
    plan.items[RESTORE_PLAN_MAX_ITEMS - 1U].entity = RESTORE_PLAN_ENTITY_NONE;
    plan.items[RESTORE_PLAN_MAX_ITEMS - 1U].phase = RESTORE_PLAN_PHASE_GLOBAL;

    assert(plan.header.plan_bytes <= RESTORE_PLAN_SDRAM_BUDGET_BYTES);
    assert(plan.items[RESTORE_PLAN_MAX_ITEMS - 1U].param_id < PARAM_COUNT);
    return 0;
}
