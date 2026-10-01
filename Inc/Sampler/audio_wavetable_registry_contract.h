#pragma once

#include "Sampler/audio_wave_table_projection.h"

typedef struct
{
    volatile uint8_t ready;
    uint8_t reserved[3];
    audio_wavetable_descriptor_t descriptor;
} audio_wavetable_registry_slot_t;

extern audio_wavetable_registry_slot_t
    g_audio_wavetable_registry[WAVETABLE_POOL_MAX_SLOTS];
