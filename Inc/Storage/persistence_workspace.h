#ifndef PERSISTENCE_WORKSPACE_H
#define PERSISTENCE_WORKSPACE_H

#include "Storage/persistent_control_codec.h"

typedef enum
{
    PERSISTENCE_WORKSPACE_FREE = 0,
    PERSISTENCE_WORKSPACE_PROJECT_SAVE,
    PERSISTENCE_WORKSPACE_PROJECT_RESTORE,
    PERSISTENCE_WORKSPACE_PATTERN_IO,
    PERSISTENCE_WORKSPACE_GROOVE_BUILD
} persistence_workspace_owner_t;

#define PERSISTENCE_PATTERN_ENCODED_MAX_BYTES \
    PERSIST_CODEC_PATTERN_DOCUMENT_MAX_BYTES
#define PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY (512U)
#define PERSISTENCE_PROJECT_RESTORE_ASSET_CAPACITY (152U)

_Static_assert(PERSISTENCE_PROJECT_RESTORE_ASSET_CAPACITY == 152U,
               "Project Restore product asset capacity changed");
_Static_assert(sizeof(persist_control_asset_ref_t) == 168U,
               "Persistent asset descriptor size changed");

typedef struct
{
    persist_control_pattern_t working_pattern;
    persist_control_asset_ref_t assets[PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY];
    persist_control_macros_t macros;
    persist_control_pattern_record_t record_scratch;
} persistence_project_save_workspace_t;

_Static_assert(sizeof(persistence_project_save_workspace_t) == 1015980U,
               "Project Save workspace size changed");

typedef struct
{
    persist_control_pattern_t working_pattern;
    persist_control_asset_ref_t assets[PERSISTENCE_PROJECT_RESTORE_ASSET_CAPACITY];
    persist_control_macros_t macros;
    persist_codec_project_metadata_t metadata;
    uint16_t asset_count;
    uint8_t working_valid;
    uint8_t macros_valid;
    uint8_t pattern_bank_started;
    uint8_t pattern_bank_staged;
    uint8_t active_pattern_seen;
    persist_codec_project_workspace_t codec_scratch;
} persistence_project_restore_workspace_t;

_Static_assert(sizeof(persistence_project_restore_workspace_t) == 955548U,
               "Project Restore workspace size changed");

_Static_assert(sizeof(persist_control_pattern_t)
                   >= PERSISTENCE_PATTERN_ENCODED_MAX_BYTES,
               "Project Save pattern scratch is too small for Pattern files");

typedef struct
{
    persist_control_pattern_t pattern;
    uint8_t encoded[PERSISTENCE_PATTERN_ENCODED_MAX_BYTES];
} persistence_pattern_io_workspace_t;

_Static_assert(sizeof(persistence_pattern_io_workspace_t) == 579304U,
               "Pattern IO workspace size changed");

typedef struct
{
    seq_groove_compiled_t track[SEQ_TIMING_TRACK_COUNT];
} persistence_groove_build_workspace_t;

persistence_project_save_workspace_t *persistence_workspace_acquire_project_save(void);
persistence_project_restore_workspace_t *persistence_workspace_acquire_project_restore(void);
persistence_pattern_io_workspace_t *persistence_workspace_acquire_pattern_io(void);
persistence_groove_build_workspace_t *persistence_workspace_acquire_groove_build(void);
void persistence_workspace_release(persistence_workspace_owner_t owner);
persistence_workspace_owner_t persistence_workspace_owner(void);

#endif /* PERSISTENCE_WORKSPACE_H */
