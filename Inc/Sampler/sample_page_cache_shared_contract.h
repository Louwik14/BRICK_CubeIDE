#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_page_cache_config.h"

#define SAMPLE_PAGE_SLOT_FLOAT_CAPACITY (SAMPLE_PAGE_BYTES / sizeof(float))
#define SAMPLE_PAGE_INDEX_SIZE (SAMPLE_PAGE_MAX_COUNT * 2U)

/* Shared data-plane layout. STORAGE is the sole writer; AUDIO only validates
 * and resolves READY descriptors after publishing its lease. */
typedef struct
{
    sample_audio_key_t key;
    uint16_t sample_id;
    uint8_t format;
    uint16_t stride_floats;
    uint32_t page_index;
    uint32_t start_frame;
    uint32_t frame_count;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint32_t data_offset;
    volatile uint8_t state;
    uint8_t static_resident;
    uint8_t reserved[3];
    uint32_t generation;
    uint8_t load_cancel_requested;
    uint8_t lifecycle_reserved[3];
    uint32_t last_touch;
} sample_page_shared_descriptor_t;

typedef struct
{
    uint8_t used;
    sample_audio_key_t key;
    uint16_t slot_index;
    uint32_t page_index;
} sample_page_shared_index_entry_t;

_Static_assert(sizeof(sample_page_shared_descriptor_t) == 56U,
               "Page descriptor ABI changed");
_Static_assert(offsetof(sample_page_shared_descriptor_t, key) == 0U,
               "Page descriptor key offset changed");
_Static_assert(offsetof(sample_page_shared_descriptor_t, format) == 6U,
               "Page descriptor format offset changed");
_Static_assert(offsetof(sample_page_shared_descriptor_t, state) == 36U,
               "Page descriptor state offset changed");
_Static_assert(offsetof(sample_page_shared_descriptor_t, last_touch) == 52U,
               "Page descriptor last-touch offset changed");
_Static_assert(sizeof(sample_page_shared_index_entry_t) == 12U,
               "Page index ABI changed");
_Static_assert(offsetof(sample_page_shared_index_entry_t, key) == 2U,
               "Page index key offset changed");
_Static_assert(offsetof(sample_page_shared_index_entry_t, slot_index) == 6U,
               "Page index slot offset changed");
_Static_assert(offsetof(sample_page_shared_index_entry_t, page_index) == 8U,
               "Page index page offset changed");

extern sample_page_shared_descriptor_t
    g_sample_page_shared_descriptor[SAMPLE_PAGE_MAX_COUNT];
extern float g_sample_page_shared_data
    [SAMPLE_PAGE_MAX_COUNT][SAMPLE_PAGE_SLOT_FLOAT_CAPACITY];
extern volatile uint16_t
    g_sample_page_shared_last_slot[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
extern sample_page_shared_index_entry_t
    g_sample_page_shared_index[SAMPLE_PAGE_INDEX_SIZE];

static inline uint16_t sample_page_cache_key_slot(sample_audio_key_t key)
{
    switch (key.domain)
    {
        case SAMPLE_AUDIO_DOMAIN_CLASSIC:
            return (key.object_id < SAMPLE_PAGE_CACHE_CLASSIC_ID_CAPACITY)
                ? (uint16_t)(SAMPLE_PAGE_CACHE_CLASSIC_ID_BASE + key.object_id)
                : UINT16_MAX;
        case SAMPLE_AUDIO_DOMAIN_LOOPER:
            return (key.object_id < SAMPLE_PAGE_CACHE_LOOPER_ID_CAPACITY)
                ? (uint16_t)(SAMPLE_PAGE_CACHE_LOOPER_ID_BASE + key.object_id)
                : UINT16_MAX;
        case SAMPLE_AUDIO_DOMAIN_MULTI:
            return (key.object_id < SAMPLE_PAGE_CACHE_MULTI_ID_CAPACITY)
                ? (uint16_t)(SAMPLE_PAGE_CACHE_MULTI_ID_BASE + key.object_id)
                : UINT16_MAX;
        default:
            return UINT16_MAX;
    }
}

static inline uint32_t sample_page_cache_hash_key(sample_audio_key_t key,
                                                  uint32_t page_index)
{
    return sample_audio_key_page_hash(key, page_index, SAMPLE_PAGE_INDEX_SIZE);
}
