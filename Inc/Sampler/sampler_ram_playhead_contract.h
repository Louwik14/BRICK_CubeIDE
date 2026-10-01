#pragma once

#include <stdint.h>

#include "Track/entity_types.h"

typedef struct
{
    uint32_t generation;
    uint32_t frame;
    uint32_t frame_count;
    uint16_t global_slot;
    uint8_t active;
    uint8_t reverse;
} sampler_ram_playhead_snapshot_t;

typedef struct
{
    volatile uint32_t sequence;
    sampler_ram_playhead_snapshot_t snapshot;
} sampler_ram_playhead_slot_t;

extern sampler_ram_playhead_slot_t
    g_sampler_ram_playhead[BRICK_ENTITY_CAPACITY];
