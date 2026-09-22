#include "Storage/persistence_workspace.h"

#include "Platform/memory_layout.h"
#include "Storage/persistence_debug.h"

typedef union
{
    persistence_project_save_workspace_t project_save;
    persistence_project_restore_workspace_t project_restore;
    persistence_pattern_io_workspace_t pattern_io;
    persistence_groove_build_workspace_t groove_build;
} persistence_workspace_storage_t;

#define PERSISTENCE_WORKSPACE_ALIGN_BYTES (32U)
#define PERSISTENCE_WORKSPACE_ALIGN_UP(_bytes) \
    (((_bytes) + PERSISTENCE_WORKSPACE_ALIGN_BYTES - 1U) \
        & ~(PERSISTENCE_WORKSPACE_ALIGN_BYTES - 1U))

_Static_assert(sizeof(persistence_workspace_storage_t) == 1015984U,
               "Persistence workspace union size changed");
_Static_assert(PERSISTENCE_WORKSPACE_ALIGN_UP(
                   sizeof(persistence_workspace_storage_t)) == 1016000U,
               "Persistence workspace aligned allocation changed");

STORAGE_STATE_SDRAM static persistence_workspace_storage_t g_persistence_workspace;
static persistence_workspace_owner_t g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_FREE;

persistence_project_save_workspace_t *persistence_workspace_acquire_project_save(void)
{
    if (g_persistence_workspace_owner != PERSISTENCE_WORKSPACE_FREE)
    {
        return 0;
    }

    g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_PROJECT_SAVE;
    persist_debug_workspace_owner(g_persistence_workspace_owner);
    return &g_persistence_workspace.project_save;
}

persistence_project_restore_workspace_t *persistence_workspace_acquire_project_restore(void)
{
    if (g_persistence_workspace_owner != PERSISTENCE_WORKSPACE_FREE) return 0;
    g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_PROJECT_RESTORE;
    persist_debug_workspace_owner(g_persistence_workspace_owner);
    return &g_persistence_workspace.project_restore;
}

persistence_pattern_io_workspace_t *persistence_workspace_acquire_pattern_io(void)
{
    if (g_persistence_workspace_owner != PERSISTENCE_WORKSPACE_FREE)
    {
        return 0;
    }

    g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_PATTERN_IO;
    persist_debug_workspace_owner(g_persistence_workspace_owner);
    return &g_persistence_workspace.pattern_io;
}

persistence_groove_build_workspace_t *persistence_workspace_acquire_groove_build(void)
{
    if (g_persistence_workspace_owner != PERSISTENCE_WORKSPACE_FREE) return 0;
    g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_GROOVE_BUILD;
    persist_debug_workspace_owner(g_persistence_workspace_owner);
    return &g_persistence_workspace.groove_build;
}

void persistence_workspace_release(persistence_workspace_owner_t owner)
{
    if ((owner != PERSISTENCE_WORKSPACE_FREE)
        && (g_persistence_workspace_owner == owner))
    {
        g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_FREE;
        persist_debug_workspace_owner(g_persistence_workspace_owner);
    }
}

persistence_workspace_owner_t persistence_workspace_owner(void)
{
    return g_persistence_workspace_owner;
}
