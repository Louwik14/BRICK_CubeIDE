#ifndef PATCH_SD_BANK_H
#define PATCH_SD_BANK_H

#include <stdint.h>

#include "Storage/patch_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PATCH_SD_FILE_MAGIC   0x54503642UL /* B6PT */
#define PATCH_SD_FILE_VERSION 6U /* Wave tone layout; older payloads are rejected. */

typedef enum
{
    PATCH_SD_BANK_ERR_NONE = 0,
    PATCH_SD_BANK_ERR_INVALID_SLOT,
    PATCH_SD_BANK_ERR_INVALID_ARG,
    PATCH_SD_BANK_ERR_GATE_BUSY,
    PATCH_SD_BANK_ERR_MOUNT_FAIL,
    PATCH_SD_BANK_ERR_PATH_FAIL,
    PATCH_SD_BANK_ERR_OPEN_FAIL,
    PATCH_SD_BANK_ERR_READ_FAIL,
    PATCH_SD_BANK_ERR_WRITE_FAIL,
    PATCH_SD_BANK_ERR_SYNC_FAIL,
    PATCH_SD_BANK_ERR_INVALID_HEADER,
    PATCH_SD_BANK_ERR_CHECKSUM_FAIL
} patch_sd_bank_error_t;

typedef enum
{
    PATCH_SD_SLOT_EMPTY = 0,
    PATCH_SD_SLOT_VALID,
    PATCH_SD_SLOT_INVALID
} patch_sd_slot_state_t;

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t payload_size;
    uint8_t family;
    uint8_t type;
    uint8_t source_track;
    uint8_t summary_family;
    uint8_t summary_type;
    uint8_t reserved[3];
    char name[PATCH_V1_NAME_MAX];
    uint32_t checksum;
} patch_sd_slot_header_t;

void patch_sd_bank_init(void);
uint8_t patch_sd_bank_load_slot(uint16_t slot, PatchSaveV1 *out_patch);
uint8_t patch_sd_bank_store_slot(uint16_t slot, const PatchSaveV1 *patch);
uint8_t patch_sd_bank_slot_has_data(uint16_t slot);
patch_sd_slot_state_t patch_sd_bank_get_slot_state(uint16_t slot);
uint16_t patch_sd_bank_find_first_empty_slot(void);
uint8_t patch_sd_bank_get_slot_metadata(uint16_t slot, patch_v1_metadata_t *out_meta);
uint8_t patch_sd_bank_delete_slot(uint16_t slot);
uint8_t patch_sd_bank_rename_slot(uint16_t slot, const char *name);
patch_sd_bank_error_t patch_sd_bank_get_last_error(void);
const char *patch_sd_bank_error_to_string(patch_sd_bank_error_t err);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_SD_BANK_H */
