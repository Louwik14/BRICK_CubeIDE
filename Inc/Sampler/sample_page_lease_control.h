#pragma once

#include "Sampler/sample_page_lease.h"

typedef struct
{
    sample_page_lease_t lease[SAMPLE_STREAM_ACTIVE_READER_CAPACITY];
    uint8_t slot[SAMPLE_STREAM_ACTIVE_READER_CAPACITY];
    uint8_t count;
} sample_page_lease_active_snapshot_t;

uint8_t sample_page_lease_control_read(uint8_t slot,
                                       sample_page_lease_t *out);
uint8_t sample_page_lease_control_protects(sample_audio_key_t key,
                                           uint32_t registration_epoch,
                                           uint32_t page_index);
uint8_t sample_page_lease_control_references_key(sample_audio_key_t key);
uint8_t sample_page_lease_control_all_released(void);
void sample_page_lease_control_active_slots(uint32_t out_mask[2]);
uint8_t sample_page_lease_control_snapshot_active(
    sample_page_lease_active_snapshot_t *out_snapshot);
uint8_t sample_page_lease_control_snapshot_protects(
    const sample_page_lease_active_snapshot_t *snapshot,
    sample_audio_key_t key,
    uint32_t registration_epoch,
    uint32_t page_index);
