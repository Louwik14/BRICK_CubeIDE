#ifndef PATTERN_LIVE_RAM_H
#define PATTERN_LIVE_RAM_H

#include <stdint.h>
#include "Storage/persistent_control_model.h"

typedef enum
{
    PATTERN_LIVE_OPERATION_STORE = 0U,
    PATTERN_LIVE_OPERATION_RECALL
} pattern_live_operation_t;

typedef struct
{
    uint8_t operation;
    uint8_t bank;
    uint8_t pattern;
    uint8_t success;
    uint8_t diagnostic;
} pattern_live_terminal_t;

void pattern_live_init(void);
void pattern_live_storage_init(void);
uint8_t pattern_live_get_control_boot(persist_control_pattern_t *out);
void pattern_live_service(void);
void pattern_live_control_process(void);
uint8_t pattern_live_capture_to_slot(uint8_t bank, uint8_t pattern);
uint8_t pattern_live_queue_slot(uint8_t bank, uint8_t pattern, uint8_t boundary_track);
void pattern_live_control_process_intent(uint8_t operation, uint8_t bank,
                                         uint8_t pattern, uint8_t boundary_track);
uint8_t pattern_live_operation_busy(void);
uint8_t pattern_live_take_terminal(pattern_live_terminal_t *out_terminal);
uint8_t pattern_live_terminal_available(void);
uint8_t pattern_live_get_active(uint8_t *out_bank, uint8_t *out_pattern);
uint8_t pattern_live_get_queued(uint8_t *out_valid, uint8_t *out_bank, uint8_t *out_pattern);
uint8_t pattern_live_get_queued_boundary(uint8_t *out_track,
                                         uint32_t *out_generation);
void pattern_live_set_active_state(uint8_t active_bank,
                                   uint8_t active_pattern,
                                   uint8_t queued_valid,
                                   uint8_t queued_bank,
                                   uint8_t queued_pattern,
                                   uint8_t boundary_track);
uint8_t pattern_live_apply_boot_snapshot(uint8_t resume_transport);

#endif
