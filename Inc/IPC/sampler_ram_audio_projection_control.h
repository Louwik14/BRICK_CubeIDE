#pragma once

#include <stdint.h>

#include "Sampler/sampler_ram_pool.h"

void sampler_ram_audio_projection_init(void);
uint8_t sampler_ram_audio_projection_build(uint16_t ram_slot,
                                           const sampler_ram_slot_t *slot,
                                           sampler_ram_audio_descriptor_t *out);
void sampler_ram_audio_projection_install_prepared(
    const sampler_ram_audio_descriptor_t *descriptor);
void sampler_ram_audio_projection_withdraw(uint16_t ram_slot,
                                           uint32_t generation);
