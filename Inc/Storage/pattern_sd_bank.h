#ifndef PATTERN_SD_BANK_H
#define PATTERN_SD_BANK_H

#include <stdint.h>

#include "Storage/pattern_live_ram.h"

void pattern_sd_bank_init(const PatternSaveV1 *boot_pattern);
uint8_t pattern_sd_bank_load_slot(uint8_t bank, uint8_t pattern, PatternSaveV1 *out_pattern);
uint8_t pattern_sd_bank_store_slot(uint8_t bank, uint8_t pattern, const PatternSaveV1 *pattern_data);
uint8_t pattern_sd_bank_store_slot_nosync(uint8_t bank, uint8_t pattern, const PatternSaveV1 *pattern_data);
uint8_t pattern_sd_bank_delete_slot(uint8_t bank, uint8_t pattern);
uint8_t pattern_sd_bank_slot_has_data(uint8_t bank, uint8_t pattern);
uint8_t pattern_sd_bank_get_slot_checksum(uint8_t bank,
                                          uint8_t pattern,
                                          uint8_t *out_has_data,
                                          uint32_t *out_checksum);

#endif
