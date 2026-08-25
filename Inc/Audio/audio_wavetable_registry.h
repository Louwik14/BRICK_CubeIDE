#pragma once

#include <stdint.h>

#include "Audio/audio_shared_memory.h"
#include "Sampler/wavetable_prepared_format.h"

typedef struct
{
    uint32_t max_phase_increment;
    uint32_t cycle_sample_count;
    uint32_t sample_count;
    uint16_t cycle_magnitude;
    uint16_t flags;
    audio_shared_memory_ref_t data;
} audio_wavetable_band_t;

typedef struct
{
    uint32_t generation;
    uint32_t frame_count;
    uint32_t frame_sample_count;
    uint16_t wavetable_slot;
    uint16_t global_slot;
    uint16_t band_count;
    uint16_t duplicate_sample_count;
    audio_shared_memory_ref_t base_data;
    audio_wavetable_band_t bands[WAVETABLE_MIPMAP_MAX_BANDS];
} audio_wavetable_descriptor_t;

void audio_wavetable_registry_init(void);
/* Local H743 implementation of the future CONTROL->AUDIO install transport. */
uint8_t audio_wavetable_registry_transport_install(
    const audio_wavetable_descriptor_t *descriptor);
uint8_t audio_wavetable_registry_resolve(uint16_t wavetable_slot,
                                         uint32_t generation,
                                         audio_wavetable_descriptor_t *out);
uint8_t audio_wavetable_registry_resolve_global(uint16_t global_slot,
                                                audio_wavetable_descriptor_t *out);
void audio_wavetable_registry_remove(uint16_t wavetable_slot,
                                     uint32_t generation);
