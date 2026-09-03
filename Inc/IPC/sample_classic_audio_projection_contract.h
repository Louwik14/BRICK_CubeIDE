#pragma once

#include <stddef.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_classic_config.h"

typedef struct
{
    volatile uint32_t seq;
    sample_audio_key_t key;
    uint32_t total_frames;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t sample_rate;
    uint32_t registration_epoch;
    uint8_t format;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t block_align;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint8_t ready;
    uint8_t reserved[3];
} sample_classic_audio_source_t;

_Static_assert(sizeof(sample_classic_audio_source_t) == 48U,
               "Classic source ABI changed");
_Static_assert(offsetof(sample_classic_audio_source_t, format) == 28U,
               "Classic source format offset changed");
_Static_assert(offsetof(sample_classic_audio_source_t, ready) == 44U,
               "Classic source ready offset changed");

extern sample_classic_audio_source_t
    g_sample_classic_audio_source[SAMPLE_CLASSIC_CAPACITY];
