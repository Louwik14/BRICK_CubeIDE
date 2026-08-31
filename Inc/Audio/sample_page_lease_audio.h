#pragma once

#include "Sampler/sample_page_lease.h"

void sample_page_lease_audio_init(void);
uint8_t sample_page_lease_audio_publish(
    uint8_t slot,
    sample_audio_key_t key,
    uint32_t registration_epoch,
    const sample_page_lease_range_t ranges[2]);
void sample_page_lease_audio_clear(uint8_t slot);
