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
