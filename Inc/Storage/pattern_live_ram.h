#ifndef PATTERN_LIVE_RAM_H
#define PATTERN_LIVE_RAM_H

#include <stdint.h>
#include "Storage/persistent_control_model.h"

void pattern_live_init(void);
uint8_t pattern_live_get_control_boot(persist_control_pattern_t *out);
persist_control_pattern_record_t *pattern_live_project_record_workspace(void);
uint8_t pattern_load_request(uint8_t bank, uint8_t pattern);
void pattern_load_service(uint32_t byte_budget);
uint8_t pattern_load_is_pending(void);
uint8_t pattern_load_is_ready(uint8_t *out_bank, uint8_t *out_pattern);
uint8_t pattern_load_take_ready(uint8_t *out_bank, uint8_t *out_pattern, persist_control_pattern_t *out_snapshot);
void pattern_load_cancel(void);
void pattern_live_service(void);
uint8_t pattern_live_capture_to_slot(uint8_t bank, uint8_t pattern);
uint8_t pattern_live_queue_slot(uint8_t bank, uint8_t pattern);
uint8_t pattern_live_get_active(uint8_t *out_bank, uint8_t *out_pattern);
uint8_t pattern_live_get_queued(uint8_t *out_valid, uint8_t *out_bank, uint8_t *out_pattern);
void pattern_live_set_active_state(uint8_t active_bank,
                                   uint8_t active_pattern,
                                   uint8_t queued_valid,
                                   uint8_t queued_bank,
                                   uint8_t queued_pattern);
uint8_t pattern_live_apply_boot_snapshot(uint8_t resume_transport);

#endif
