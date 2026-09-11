#pragma once

#include "IPC/audio_wave_table_projection.h"

typedef struct
{
    uint8_t ready;
    uint8_t reserved[3];
    audio_wavetable_descriptor_t descriptor;
} audio_wavetable_registry_snapshot_t;

typedef struct
{
    volatile uint32_t active_snapshot;
    audio_wavetable_registry_snapshot_t snapshots[2];
} audio_wavetable_registry_slot_t;

extern audio_wavetable_registry_slot_t
    g_audio_wavetable_registry[WAVETABLE_POOL_MAX_SLOTS];
