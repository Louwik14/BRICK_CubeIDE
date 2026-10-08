#pragma once

#include <stdint.h>

#include "Track/entity_types.h"

typedef struct
{
    uint32_t generation;
    uint32_t frame;
    uint32_t frame_count;
    uint32_t region_begin;
    uint32_t region_end;
    uint32_t loop_begin;
    uint8_t slice_count;
    uint8_t slice_index;
    uint16_t global_slot;
    uint8_t active;
} sample_playhead_snapshot_t;

typedef struct
{
    volatile uint32_t sequence;
    sample_playhead_snapshot_t snapshot;
} sample_playhead_slot_t;

extern sample_playhead_slot_t g_sample_playhead[BRICK_ENTITY_CAPACITY];
