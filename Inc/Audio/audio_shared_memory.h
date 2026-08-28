#pragma once

#include <stdint.h>

typedef enum
{
    AUDIO_SHARED_REGION_NONE = 0,
    AUDIO_SHARED_REGION_SAMPLE_PAGE_POOL = 1
} audio_shared_region_t;

typedef struct
{
    uint32_t offset;
    uint32_t length;
    uint16_t region;
    uint16_t reserved;
} audio_shared_memory_ref_t;

uint8_t audio_shared_memory_page_ref(uint16_t first_page_slot,
                                     uint32_t byte_offset,
                                     uint32_t length,
                                     audio_shared_memory_ref_t *out);
const void *audio_shared_memory_resolve(const audio_shared_memory_ref_t *ref);
/* AUDIO consumer resolution for cacheable bulk payload. */
const void *audio_shared_memory_consume(const audio_shared_memory_ref_t *ref);
