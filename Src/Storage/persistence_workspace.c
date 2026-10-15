#include "Storage/persistence_workspace.h"

#include "Platform/memory_layout.h"
#include "Storage/persistence_debug.h"

typedef union
{
    persistence_project_save_workspace_t project_save;
    persistence_project_restore_workspace_t project_restore;
    persistence_pattern_io_workspace_t pattern_io;
    persistence_groove_build_workspace_t groove_build;
    persistence_patch_dx7_boot_workspace_t patch_dx7_boot;
    persistence_wavetable_import_workspace_t wavetable_import;
} persistence_workspace_storage_t;

#define PERSISTENCE_WORKSPACE_ALIGN_BYTES (32U)
#define PERSISTENCE_WORKSPACE_ALIGN_UP(_bytes) \
    (((_bytes) + PERSISTENCE_WORKSPACE_ALIGN_BYTES - 1U) \
        & ~(PERSISTENCE_WORKSPACE_ALIGN_BYTES - 1U))

_Static_assert(sizeof(persistence_workspace_storage_t) == 581696U,
               "Persistence workspace size changed");
_Static_assert(PERSISTENCE_WORKSPACE_ALIGN_UP(
                   sizeof(persistence_workspace_storage_t)) == 581696U,
               "Aligned persistence workspace size changed");

STORAGE_STATE_SDRAM ALIGN32 static persistence_workspace_storage_t g_persistence_workspace;
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

persistence_patch_dx7_boot_workspace_t *persistence_workspace_acquire_patch_dx7_boot(void)
{
    if (g_persistence_workspace_owner != PERSISTENCE_WORKSPACE_FREE) return 0;
    g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_PATCH_DX7_BOOT;
    persist_debug_workspace_owner(g_persistence_workspace_owner);
    return &g_persistence_workspace.patch_dx7_boot;
}

persistence_wavetable_import_workspace_t *persistence_workspace_acquire_wavetable_import(void)
{
    if (g_persistence_workspace_owner != PERSISTENCE_WORKSPACE_FREE) return 0;
    g_persistence_workspace_owner = PERSISTENCE_WORKSPACE_WAVETABLE_IMPORT;
    persist_debug_workspace_owner(g_persistence_workspace_owner);
    return &g_persistence_workspace.wavetable_import;
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
