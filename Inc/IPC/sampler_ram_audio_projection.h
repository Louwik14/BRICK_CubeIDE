#pragma once

#include <stdint.h>

#include "Sampler/sampler_ram_pool.h"
#include "Audio/audio_shared_memory.h"

typedef struct
{
    uint32_t generation;
    uint32_t frames;
    uint32_t sample_rate;
    uint32_t data_offset;
    audio_shared_memory_ref_t data;
    uint16_t global_slot;
    uint16_t ram_slot;
    uint16_t channels;
    uint16_t bytes_per_frame;
    sampler_ram_format_t format;
} sampler_ram_audio_descriptor_t;

_Static_assert(sizeof(sampler_ram_audio_descriptor_t) == 40U,
               "Sample RAM AUDIO descriptor ABI changed");

void sampler_ram_audio_projection_init(void);
/* H743 local transport seam. H747 replaces the implementation, not the
 * region+offset descriptor consumed by AUDIO. */
uint8_t sampler_ram_audio_projection_publish(uint16_t ram_slot,
                                             const sampler_ram_slot_t *slot);
void sampler_ram_audio_projection_withdraw(uint16_t ram_slot, uint32_t generation);
uint8_t sampler_ram_audio_projection_resolve(uint16_t global_slot,
                                             sampler_ram_audio_descriptor_t *out);
