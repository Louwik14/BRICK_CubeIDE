#pragma once

#include "Sampler/sample_page_lease.h"

uint8_t sample_page_lease_control_read(uint8_t slot,
                                       sample_page_lease_t *out);
uint8_t sample_page_lease_control_protects(sample_audio_key_t key,
                                           uint32_t registration_epoch,
                                           uint32_t page_index);
