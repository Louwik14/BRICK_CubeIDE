#include "Storage/persistence_workspace.h"

#include "Storage/memory_layout.h"

typedef union
{
    persist_codec_project_workspace_t project;
    persist_control_pattern_t pattern_save;
} persistence_workspace_storage_t;

STORAGE_STATE_SDRAM static persistence_workspace_storage_t g_persistence_workspace;
static persistence_workspace_owner_t g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_FREE;

persist_codec_project_workspace_t *persistence_workspace_acquire_project(void)
{
    if (g_persistence_workspace_owner != PERSISTENCE_WORKSPACE_FREE)
    {
        return 0;
    }

    g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_PROJECT;
    return &g_persistence_workspace.project;
}

persist_control_pattern_t *persistence_workspace_acquire_pattern_save(void)
{
    if (g_persistence_workspace_owner != PERSISTENCE_WORKSPACE_FREE)
    {
        return 0;
    }

    g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_PATTERN_SAVE;
    return &g_persistence_workspace.pattern_save;
}

void persistence_workspace_release(persistence_workspace_owner_t owner)
{
    if ((owner != PERSISTENCE_WORKSPACE_FREE)
        && (g_persistence_workspace_owner == owner))
    {
        g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_FREE;
    }
}

persistence_workspace_owner_t persistence_workspace_owner(void)
{
    return g_persistence_workspace_owner;
}
