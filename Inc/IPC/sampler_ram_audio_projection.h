#pragma once

#include <stdint.h>

#include "IPC/shared_memory_ref.h"
#include "Sampler/sample_page_cache_config.h"

typedef enum
{
    SAMPLER_RAM_FORMAT_NONE = 0,
    SAMPLER_RAM_FORMAT_FLOAT32_MONO,
    SAMPLER_RAM_FORMAT_FLOAT32_STEREO_INTERLEAVED
} sampler_ram_format_t;

#define SAMPLER_RAM_AUDIO_INVALID_SLOT UINT16_MAX
#define SAMPLER_RAM_AUDIO_MAX_SLOTS SAMPLE_PAGE_PRODUCT_MAX_LONG_SAMPLE_SLOTS

static inline uint16_t sampler_ram_audio_format_channels(
    sampler_ram_format_t format)
{
    return (format == SAMPLER_RAM_FORMAT_FLOAT32_MONO) ? 1U
        : (format == SAMPLER_RAM_FORMAT_FLOAT32_STEREO_INTERLEAVED) ? 2U : 0U;
}

static inline uint16_t sampler_ram_audio_format_bytes_per_frame(
    sampler_ram_format_t format)
{
    return (uint16_t)(sampler_ram_audio_format_channels(format)
                      * sizeof(float));
}

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
