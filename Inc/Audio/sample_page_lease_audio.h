#pragma once

#include "Sampler/sample_page_lease.h"

void sample_page_lease_audio_init(void);
uint8_t sample_page_lease_audio_publish(
    uint8_t slot,
    sample_audio_key_t key,
    uint32_t registration_epoch,
    const sample_reader_window_role_t roles[SAMPLE_PAGE_LEASE_PAGE_COUNT],
    uint8_t valid_mask);
void sample_page_lease_audio_clear(uint8_t slot);
