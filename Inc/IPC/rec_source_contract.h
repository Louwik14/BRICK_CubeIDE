#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"

typedef struct
{
    volatile uint8_t ready;
    uint8_t reserved[3];
    sample_audio_key_t key;
    uint32_t frame_count;
    uint32_t sample_rate;
    uint32_t registration_epoch;
} rec_source_projection_t;

typedef rec_source_projection_t rec_source_snapshot_t;

extern rec_source_projection_t g_rec_source_projection;
