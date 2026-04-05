#ifndef PROJECT_SD_BANK_H
#define PROJECT_SD_BANK_H

#include <stdint.h>

#include "Storage/project_v1.h"

void project_sd_bank_init(void);
void project_sd_bank_refresh_slots(void);
uint8_t project_sd_bank_list_slots(uint8_t *out_slots, uint8_t max_slots);
uint8_t project_sd_bank_slot_has_data(uint8_t project_slot);
uint8_t project_sd_bank_load_slot(uint8_t project_slot, ProjectSaveV1 *out_project, uint32_t *out_save_counter);
uint8_t project_sd_bank_store_slot(uint8_t project_slot, const ProjectSaveV1 *project, uint32_t save_counter);
uint8_t project_sd_bank_delete_slot(uint8_t project_slot);

#endif
