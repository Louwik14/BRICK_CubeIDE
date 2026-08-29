#include "Storage/persistence_workspace.h"

#include "Platform/memory_layout.h"

typedef union
{
    persist_codec_project_workspace_t project;
    persistence_project_save_workspace_t project_save;
    persistence_project_restore_workspace_t project_restore;
    persistence_pattern_io_workspace_t pattern_io;
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

persistence_project_save_workspace_t *persistence_workspace_acquire_project_save(void)
{
    if (g_persistence_workspace_owner != PERSISTENCE_WORKSPACE_FREE)
    {
        return 0;
    }

    g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_PROJECT_SAVE;
    return &g_persistence_workspace.project_save;
}

persistence_project_restore_workspace_t *persistence_workspace_acquire_project_restore(void)
{
    if (g_persistence_workspace_owner != PERSISTENCE_WORKSPACE_FREE) return 0;
    g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_PROJECT_RESTORE;
    return &g_persistence_workspace.project_restore;
}

persistence_pattern_io_workspace_t *persistence_workspace_acquire_pattern_io(void)
{
    if (g_persistence_workspace_owner != PERSISTENCE_WORKSPACE_FREE)
    {
        return 0;
    }

    g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_PATTERN_IO;
    return &g_persistence_workspace.pattern_io;
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
