#ifndef PATTERN_LOAD_STORAGE_H
#define PATTERN_LOAD_STORAGE_H

#include <stdint.h>

#include "Storage/persistent_control_model.h"

/* Storage-owned pattern load request and one-shot completion contract. */
uint8_t pattern_storage_request(uint8_t bank, uint8_t pattern);
uint8_t pattern_storage_save_busy(void);
uint8_t pattern_storage_request_save(uint8_t bank, uint8_t pattern);
void pattern_storage_service(uint32_t byte_budget);
uint8_t pattern_storage_is_pending(void);
uint8_t pattern_storage_load_available(uint8_t *out_bank,
                                       uint8_t *out_pattern);
uint8_t pattern_storage_take_load(uint8_t *out_bank,
                                  uint8_t *out_pattern,
                                  persist_control_pattern_t *out_pattern_data);
void pattern_storage_cancel(void);

#endif /* PATTERN_LOAD_STORAGE_H */
