#ifndef KIT_SD_BANK_H
#define KIT_SD_BANK_H

#include <stdint.h>

#include "Storage/kit_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KIT_SD_FILE_MAGIC   0x544B3642UL /* B6KT */
#define KIT_SD_FILE_VERSION 3U

typedef enum
{
    KIT_SD_BANK_ERR_NONE = 0,
    KIT_SD_BANK_ERR_INVALID_SLOT,
    KIT_SD_BANK_ERR_INVALID_ARG,
    KIT_SD_BANK_ERR_GATE_BUSY,
    KIT_SD_BANK_ERR_MOUNT_FAIL,
    KIT_SD_BANK_ERR_PATH_FAIL,
    KIT_SD_BANK_ERR_OPEN_FAIL,
    KIT_SD_BANK_ERR_READ_FAIL,
    KIT_SD_BANK_ERR_WRITE_FAIL,
    KIT_SD_BANK_ERR_SYNC_FAIL,
    KIT_SD_BANK_ERR_INVALID_HEADER,
    KIT_SD_BANK_ERR_CHECKSUM_FAIL
} kit_sd_bank_error_t;

typedef enum
{
    KIT_SD_SLOT_EMPTY = 0,
    KIT_SD_SLOT_VALID,
    KIT_SD_SLOT_INVALID
} kit_sd_slot_state_t;

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t payload_size;
    uint8_t track_count;
    uint8_t reserved[3];
    char name[KIT_V1_NAME_MAX];
    kit_v1_track_summary_t summary[KIT_V1_TRACK_MAX];
    uint32_t checksum;
} kit_sd_slot_header_t;

void kit_sd_bank_init(void);
uint8_t kit_sd_bank_load_slot(uint16_t slot, KitSaveV1 *out_kit);
uint8_t kit_sd_bank_store_slot(uint16_t slot, const KitSaveV1 *kit);
uint8_t kit_sd_bank_slot_has_data(uint16_t slot);
kit_sd_slot_state_t kit_sd_bank_get_slot_state(uint16_t slot);
uint16_t kit_sd_bank_find_first_empty_slot(void);
uint8_t kit_sd_bank_get_slot_metadata(uint16_t slot, kit_v1_metadata_t *out_meta);
uint8_t kit_sd_bank_delete_slot(uint16_t slot);
uint8_t kit_sd_bank_rename_slot(uint16_t slot, const char *name);
kit_sd_bank_error_t kit_sd_bank_get_last_error(void);
const char *kit_sd_bank_error_to_string(kit_sd_bank_error_t err);

#ifdef __cplusplus
}
#endif

#endif /* KIT_SD_BANK_H */
