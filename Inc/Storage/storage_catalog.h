#ifndef STORAGE_CATALOG_H
#define STORAGE_CATALOG_H

#include <stdint.h>

#include "Storage/wav_loader.h"

#define STORAGE_CATALOG_CAPACITY 256U
#define STORAGE_CATALOG_MULTI_INDEX_MAX 256U

typedef enum
{
    STORAGE_CATALOG_WAVETABLE = 0,
    STORAGE_CATALOG_MULTI
} storage_catalog_kind_t;

typedef struct
{
    char path[WAV_LOADER_CATALOG_PATH_MAX];
    char name[WAV_LOADER_CATALOG_NAME_MAX];
    char index_path[WAV_LOADER_CATALOG_PATH_MAX];
    uint16_t wav_count;
    uint16_t sample_count;
    uint16_t zone_count;
    uint16_t slot_cost;
    uint8_t is_dir;
    uint8_t multi_type;
    uint8_t prepared;
} storage_catalog_entry_t;

typedef struct
{
    const storage_catalog_entry_t *entries;
    uint16_t count;
    uint32_t sequence;
} storage_catalog_snapshot_t;

uint8_t storage_catalog_request(storage_catalog_kind_t kind, const char *path);
void storage_catalog_service(void);
uint8_t storage_catalog_snapshot_begin(storage_catalog_kind_t kind,
                                        const char *path,
                                        storage_catalog_snapshot_t *snapshot);
uint8_t storage_catalog_snapshot_end(const storage_catalog_snapshot_t *snapshot);

#endif /* STORAGE_CATALOG_H */
