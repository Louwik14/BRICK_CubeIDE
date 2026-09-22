#ifndef PATTERN_LIVE_RAM_H
#define PATTERN_LIVE_RAM_H

#include <stdint.h>
#include "Storage/persistent_control_codec.h"

typedef enum
{
    PATTERN_RECORD_LEASE_FREE = 0,
    PATTERN_RECORD_LEASE_PATTERN_LOAD,
    PATTERN_RECORD_LEASE_PATTERN_QUEUE_READY,
    PATTERN_RECORD_LEASE_PROJECT_RESTORE,
    PATTERN_RECORD_LEASE_PROJECT_SAVE
} pattern_record_lease_owner_t;

persist_control_pattern_record_t *pattern_record_lease_acquire(
    pattern_record_lease_owner_t owner);
uint8_t pattern_record_lease_transfer(pattern_record_lease_owner_t current_owner,
                                      pattern_record_lease_owner_t next_owner);
uint8_t pattern_record_lease_release(pattern_record_lease_owner_t owner);
pattern_record_lease_owner_t pattern_record_lease_owner(void);
persist_codec_project_workspace_t *pattern_record_lease_codec_workspace(
    pattern_record_lease_owner_t owner);

void pattern_live_init(void);
uint8_t pattern_live_build_default(persist_control_pattern_t *out,
                                   uint32_t groove_seed);
uint8_t pattern_load_request(uint8_t bank, uint8_t pattern);
void pattern_load_service(uint32_t byte_budget);
uint8_t pattern_load_is_pending(void);
uint8_t pattern_load_is_ready(uint8_t *out_bank, uint8_t *out_pattern);
uint8_t pattern_load_take_ready(uint8_t *out_bank, uint8_t *out_pattern, persist_control_pattern_t *out_snapshot);
void pattern_load_cancel(void);
void pattern_live_cancel_recall(void);
void pattern_live_on_transport_stopped(void);
void pattern_live_service(void);
uint8_t pattern_live_capture_to_slot(uint8_t bank, uint8_t pattern);
uint8_t pattern_live_queue_slot(uint8_t bank, uint8_t pattern, uint8_t boundary_track);
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

#endif
