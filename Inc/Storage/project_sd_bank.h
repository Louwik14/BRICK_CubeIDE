#ifndef PROJECT_SD_BANK_H
#define PROJECT_SD_BANK_H

#include <stdint.h>

#include "Storage/project_v1.h"

typedef enum
{
    PROJECT_SD_BANK_ERR_NONE = 0,
    PROJECT_SD_BANK_ERR_INVALID_SLOT,
    PROJECT_SD_BANK_ERR_INVALID_ARG,
    PROJECT_SD_BANK_ERR_GATE_BUSY,
    PROJECT_SD_BANK_ERR_MOUNT_FAIL,
    PROJECT_SD_BANK_ERR_PATH_FAIL,
    PROJECT_SD_BANK_ERR_OPEN_FAIL,
    PROJECT_SD_BANK_ERR_READ_FAIL,
    PROJECT_SD_BANK_ERR_WRITE_FAIL,
    PROJECT_SD_BANK_ERR_SYNC_FAIL,
    PROJECT_SD_BANK_ERR_SEEK_FAIL,
    PROJECT_SD_BANK_ERR_INVALID_HEADER,
    PROJECT_SD_BANK_ERR_INVALID_SIZE,
    PROJECT_SD_BANK_ERR_CHECKSUM_FAIL,
    PROJECT_SD_BANK_ERR_PATTERN_READ_FAIL,
    PROJECT_SD_BANK_ERR_PATTERN_STORE_FAIL,
    PROJECT_SD_BANK_ERR_PATTERN_DELETE_FAIL,
    PROJECT_SD_BANK_ERR_UNLINK_FAIL
} project_sd_bank_error_t;

void project_sd_bank_init(void);
void project_sd_bank_refresh_slots(void);
uint8_t project_sd_bank_list_slots(uint8_t *out_slots, uint8_t max_slots);
uint8_t project_sd_bank_slot_has_data(uint8_t project_slot);
uint8_t project_sd_bank_load_slot(uint8_t project_slot, ProjectSaveV1 *out_project, uint32_t *out_save_counter);
uint8_t project_sd_bank_commit_slot_patterns(uint8_t project_slot);
uint8_t project_sd_bank_store_slot(uint8_t project_slot, const ProjectSaveV1 *project, uint32_t save_counter);
uint8_t project_sd_bank_delete_slot(uint8_t project_slot);
uint8_t project_sd_bank_is_slot_equivalent_to_live(uint8_t project_slot);
project_sd_bank_error_t project_sd_bank_get_last_error(void);
const char *project_sd_bank_error_to_string(project_sd_bank_error_t err);

#endif
