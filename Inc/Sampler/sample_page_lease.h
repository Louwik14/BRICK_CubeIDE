#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_stream_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_PAGE_LEASE_CLASSIC_COUNT (16U)
#define SAMPLE_PAGE_LEASE_MULTI_COUNT   SAMPLE_STREAM_TARGET_MAX_VOICES
#define SAMPLE_PAGE_LEASE_LOOPER_COUNT  (16U)
#define SAMPLE_PAGE_LEASE_LOOPER_AUX_COUNT SAMPLE_PAGE_LEASE_LOOPER_COUNT
#define SAMPLE_PAGE_LEASE_CLASSIC_BASE (0U)
#define SAMPLE_PAGE_LEASE_MULTI_BASE \
    (SAMPLE_PAGE_LEASE_CLASSIC_BASE + SAMPLE_PAGE_LEASE_CLASSIC_COUNT)
#define SAMPLE_PAGE_LEASE_LOOPER_BASE \
    (SAMPLE_PAGE_LEASE_MULTI_BASE + SAMPLE_PAGE_LEASE_MULTI_COUNT)
#define SAMPLE_PAGE_LEASE_LOOPER_AUX_BASE \
    (SAMPLE_PAGE_LEASE_LOOPER_BASE + SAMPLE_PAGE_LEASE_LOOPER_COUNT)
#define SAMPLE_PAGE_LEASE_SLOT_COUNT \
    (SAMPLE_PAGE_LEASE_LOOPER_AUX_BASE + SAMPLE_PAGE_LEASE_LOOPER_AUX_COUNT)

typedef struct
{
    uint32_t first_page;
    uint8_t page_count;
    uint8_t reserved[3];
} sample_page_lease_range_t;

typedef struct
{
    volatile uint32_t seq;
    sample_audio_key_t key;
    uint32_t registration_epoch;
    sample_page_lease_range_t ranges[2];
} sample_page_lease_t;

_Static_assert(sizeof(sample_page_lease_range_t) == 8U,
               "Sample page lease range ABI changed");
_Static_assert(sizeof(sample_page_lease_t) == 28U,
               "Sample page lease ABI changed");
_Static_assert(offsetof(sample_page_lease_t, key) == 4U,
               "Sample page lease key offset changed");
_Static_assert(offsetof(sample_page_lease_t, registration_epoch) == 8U,
               "Sample page lease epoch offset changed");
_Static_assert(offsetof(sample_page_lease_t, ranges) == 12U,
               "Sample page lease ranges offset changed");

extern sample_page_lease_t g_sample_page_leases[SAMPLE_PAGE_LEASE_SLOT_COUNT];

static inline uint8_t sample_page_lease_classic_slot(uint8_t reader)
{
    return (uint8_t)(SAMPLE_PAGE_LEASE_CLASSIC_BASE + reader);
}

static inline uint8_t sample_page_lease_multi_slot(uint8_t reader)
{
    return (uint8_t)(SAMPLE_PAGE_LEASE_MULTI_BASE + reader);
}

static inline uint8_t sample_page_lease_looper_slot(uint8_t reader)
{
    return (uint8_t)(SAMPLE_PAGE_LEASE_LOOPER_BASE + reader);
}

static inline uint8_t sample_page_lease_looper_aux_slot(uint8_t reader)
{
    return (uint8_t)(SAMPLE_PAGE_LEASE_LOOPER_AUX_BASE + reader);
}

#ifdef __cplusplus
}
#endif
