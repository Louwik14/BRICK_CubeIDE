#ifndef PERSISTENCE_WORKSPACE_H
#define PERSISTENCE_WORKSPACE_H

#include "Storage/persistent_control_codec.h"

typedef enum
{
    PERSISTENCE_WORKSPACE_FREE = 0,
    PERSISTENCE_WORKSPACE_PROJECT_SAVE,
    PERSISTENCE_WORKSPACE_PROJECT_RESTORE,
    PERSISTENCE_WORKSPACE_PATTERN_IO
} persistence_workspace_owner_t;

#define PERSISTENCE_PATTERN_ENCODED_MAX_BYTES (256U * 1024U)
#define PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY (512U)
#define PERSISTENCE_ASSET_RESULT_PENDING 0U
#define PERSISTENCE_ASSET_RESULT_READY 1U
#define PERSISTENCE_ASSET_RESULT_FAILED 2U

typedef struct
{
    persist_control_pattern_t working_pattern;
    persist_control_pattern_record_t pattern_record;
    persist_control_asset_ref_t assets[PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY];
    persist_control_macros_t macros;
} persistence_project_save_workspace_t;

typedef struct
{
    persist_codec_project_workspace_t codec;
    persist_control_pattern_t working_pattern;
    persist_control_asset_ref_t assets[PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY];
    uint16_t asset_runtime[PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY];
    uint8_t asset_result[PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY];
    persist_control_macros_t macros;
    persist_codec_project_metadata_t metadata;
    uint16_t asset_count;
    uint8_t working_valid;
    uint8_t macros_valid;
    uint8_t pattern_bank_started;
    uint8_t pattern_bank_staged;
    uint8_t active_pattern_seen;
} persistence_project_restore_workspace_t;

_Static_assert(sizeof(persist_control_pattern_t)
                   >= PERSISTENCE_PATTERN_ENCODED_MAX_BYTES,
               "Project Save pattern scratch is too small for Pattern files");

typedef struct
{
    persist_control_pattern_t pattern;
    uint8_t encoded[PERSISTENCE_PATTERN_ENCODED_MAX_BYTES];
} persistence_pattern_io_workspace_t;

persistence_project_save_workspace_t *persistence_workspace_acquire_project_save(void);
persistence_project_restore_workspace_t *persistence_workspace_acquire_project_restore(void);
persistence_project_restore_workspace_t *persistence_workspace_project_restore_view(void);
persistence_pattern_io_workspace_t *persistence_workspace_acquire_pattern_io(void);
void persistence_workspace_release(persistence_workspace_owner_t owner);
persistence_workspace_owner_t persistence_workspace_owner(void);

#endif /* PERSISTENCE_WORKSPACE_H */
