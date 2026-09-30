#pragma once

#include "Sampler/sample_page_lease.h"
#include "Sampler/sample_page_cache_config.h"

typedef struct
{
    uint16_t slot_index;
    uint16_t reserved;
    uint32_t page_generation;
} sample_page_resolved_protection_t;

typedef struct
{
    sample_audio_key_t key;
    uint32_t registration_epoch;
    uint32_t page_index;
} sample_page_logical_protection_t;

typedef struct
{
    uint32_t protected_slots[(SAMPLE_PAGE_MAX_COUNT + 31U) / 32U];
    sample_page_resolved_protection_t resolved[
        SAMPLE_STREAM_ACTIVE_READER_CAPACITY * SAMPLE_PAGE_LEASE_PAGE_COUNT];
    sample_page_logical_protection_t unresolved[
        SAMPLE_STREAM_ACTIVE_READER_CAPACITY * SAMPLE_PAGE_LEASE_PAGE_COUNT];
    uint32_t reader_seq[SAMPLE_STREAM_ACTIVE_READER_CAPACITY];
    uint8_t reader_slot[SAMPLE_STREAM_ACTIVE_READER_CAPACITY];
    uint8_t resolved_count;
    uint8_t unresolved_count;
    uint8_t reader_count;
} sample_page_lease_active_snapshot_t;

uint8_t sample_page_lease_control_read(uint8_t slot,
                                       sample_page_lease_t *out);
uint8_t sample_page_lease_control_protects(uint16_t slot_index,
                                           uint32_t page_generation,
                                           sample_audio_key_t key,
                                           uint32_t registration_epoch,
                                           uint32_t page_index);
uint8_t sample_page_lease_control_references_key(sample_audio_key_t key);
uint8_t sample_page_lease_control_all_released(void);
void sample_page_lease_control_active_slots(uint32_t out_mask[2]);
uint8_t sample_page_lease_control_snapshot_active(
    sample_page_lease_active_snapshot_t *out_snapshot);
uint8_t sample_page_lease_control_snapshot_protects(
    const sample_page_lease_active_snapshot_t *snapshot,
    uint16_t slot_index,
    uint32_t page_generation,
    sample_audio_key_t key,
    uint32_t registration_epoch,
    uint32_t page_index);
