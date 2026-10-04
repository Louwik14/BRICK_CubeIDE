#ifndef PATTERN_LIVE_RAM_H
#define PATTERN_LIVE_RAM_H

#include <stdint.h>
#include "Storage/persistent_control_codec.h"

void pattern_live_init(void);
uint8_t pattern_live_build_default(persist_control_pattern_t *out,
                                   uint32_t groove_seed);
void pattern_load_service(uint32_t byte_budget);
uint8_t pattern_load_is_pending(void);
void pattern_live_cancel_recall(void);
void pattern_live_on_transport_stopped(void);
void pattern_live_service(void);
uint8_t pattern_live_capture_to_slot(uint8_t bank, uint8_t pattern);
uint8_t pattern_live_request_slot(uint8_t bank, uint8_t pattern);
uint8_t pattern_live_get_active(uint8_t *out_bank, uint8_t *out_pattern);
uint8_t pattern_live_get_pending(uint8_t *out_valid, uint8_t *out_bank, uint8_t *out_pattern);
void pattern_live_publish_active(uint8_t active_bank, uint8_t active_pattern);

#endif
