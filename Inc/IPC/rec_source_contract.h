#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"

typedef struct
{
    sample_audio_key_t key;
    uint32_t frame_count;
    uint32_t sample_rate;
    uint32_t registration_epoch;
    uint32_t publication_serial;
    uint8_t ready;
    uint8_t reserved[3];
} rec_source_snapshot_t;

typedef struct
{
    volatile uint32_t active_snapshot;
    rec_source_snapshot_t snapshots[2];
} rec_source_projection_t;

extern rec_source_projection_t g_rec_source_projection;
