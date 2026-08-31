#pragma once

#include "IPC/sampler_ram_audio_projection.h"
#include "Sampler/sample_page_cache_config.h"

#define SAMPLER_RAM_AUDIO_SLOT_COUNT SAMPLE_PAGE_PRODUCT_MAX_LONG_SAMPLE_SLOTS

typedef struct
{
    volatile uint32_t sequence;
    volatile uint8_t ready;
    uint8_t reserved[3];
    sampler_ram_audio_descriptor_t descriptor;
} sampler_ram_audio_slot_t;

extern sampler_ram_audio_slot_t
    g_sampler_ram_audio_slots[SAMPLER_RAM_AUDIO_SLOT_COUNT];
extern volatile uint16_t
    g_sampler_ram_audio_global_to_slot[SAMPLER_RAM_AUDIO_SLOT_COUNT];
