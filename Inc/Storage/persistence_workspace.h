#ifndef PERSISTENCE_WORKSPACE_H
#define PERSISTENCE_WORKSPACE_H

#include "Storage/persistent_control_codec.h"

typedef enum
{
    PERSISTENCE_WORKSPACE_FREE = 0,
    PERSISTENCE_WORKSPACE_PROJECT,
    PERSISTENCE_WORKSPACE_PATTERN_SAVE
} persistence_workspace_owner_t;

persist_codec_project_workspace_t *persistence_workspace_acquire_project(void);
persist_control_pattern_t *persistence_workspace_acquire_pattern_save(void);
void persistence_workspace_release(persistence_workspace_owner_t owner);
persistence_workspace_owner_t persistence_workspace_owner(void);

#endif /* PERSISTENCE_WORKSPACE_H */
