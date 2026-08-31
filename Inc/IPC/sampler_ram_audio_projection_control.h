#pragma once

#include <stdint.h>

#include "Sampler/sampler_ram_pool.h"

void sampler_ram_audio_projection_init(void);
uint8_t sampler_ram_audio_projection_publish(uint16_t ram_slot,
                                             const sampler_ram_slot_t *slot);
void sampler_ram_audio_projection_withdraw(uint16_t ram_slot,
                                           uint32_t generation);
