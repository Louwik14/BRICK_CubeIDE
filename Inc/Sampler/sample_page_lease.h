#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_stream_limits.h"
#include "Track/entity_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_PAGE_LEASE_CLASSIC_READER_BASE (2U)
#define SAMPLE_PAGE_LEASE_CLASSIC_COUNT \
    (SAMPLE_PAGE_LEASE_CLASSIC_READER_BASE + BRICK_ENTITY_CAPACITY)
#define SAMPLE_PAGE_LEASE_MULTI_COUNT   SAMPLE_STREAM_TARGET_MAX_VOICES
#define SAMPLE_PAGE_LEASE_REC_COUNT     (SAMPLE_PAGE_LEASE_CLASSIC_COUNT + 1U)
#define SAMPLE_PAGE_LEASE_REC_OVERDUB_READER SAMPLE_PAGE_LEASE_CLASSIC_COUNT
#define SAMPLE_PAGE_LEASE_CLASSIC_BASE (0U)
#define SAMPLE_PAGE_LEASE_MULTI_BASE \
    (SAMPLE_PAGE_LEASE_CLASSIC_BASE + SAMPLE_PAGE_LEASE_CLASSIC_COUNT)
#define SAMPLE_PAGE_LEASE_REC_BASE \
    (SAMPLE_PAGE_LEASE_MULTI_BASE + SAMPLE_PAGE_LEASE_MULTI_COUNT)
#define SAMPLE_PAGE_LEASE_SLOT_COUNT \
    (SAMPLE_PAGE_LEASE_REC_BASE + SAMPLE_PAGE_LEASE_REC_COUNT)

#if (SAMPLE_PAGE_LEASE_SLOT_COUNT >= UINT8_MAX)
#error "Lease slot indices must fit in uint8_t without using UINT8_MAX"
#endif
#if (SAMPLE_PAGE_LEASE_MULTI_COUNT != SAMPLE_STREAM_TARGET_MAX_VOICES)
#error "Each physical Multi voice requires its own lease slot"
#endif

typedef enum
{
    SAMPLE_PAGE_LEASE_CURRENT = 0,
    SAMPLE_PAGE_LEASE_NEXT,
    SAMPLE_PAGE_LEASE_LOOP_START,
    SAMPLE_PAGE_LEASE_LOOP_START_NEXT,
    SAMPLE_PAGE_LEASE_PAGE_COUNT
} sample_page_lease_page_role_t;

#define SAMPLE_PAGE_LEASE_VALID(role) ((uint8_t)(1U << (uint8_t)(role)))

typedef struct
{
    sample_audio_key_t key;
    uint32_t page_index;
    uint32_t page_generation;
    uint16_t slot_index;
    uint8_t resolved;
    uint8_t reserved;
} sample_reader_window_role_t;

typedef struct
{
    volatile uint32_t seq;
    sample_audio_key_t key;
    uint32_t registration_epoch;
    sample_reader_window_role_t roles[SAMPLE_PAGE_LEASE_PAGE_COUNT];
    uint8_t valid_mask;
    uint8_t reserved[3];
} sample_page_lease_t;

extern sample_page_lease_t g_sample_page_leases[SAMPLE_PAGE_LEASE_SLOT_COUNT];
extern volatile uint32_t g_sample_page_lease_active_mask[2];

static inline uint8_t sample_page_lease_classic_slot(uint8_t reader)
{
    if (reader >= SAMPLE_PAGE_LEASE_CLASSIC_COUNT) return UINT8_MAX;
    return (uint8_t)(SAMPLE_PAGE_LEASE_CLASSIC_BASE + reader);
}

static inline uint8_t sample_page_lease_multi_slot(uint8_t reader)
{
    if (reader >= SAMPLE_PAGE_LEASE_MULTI_COUNT) return UINT8_MAX;
    return (uint8_t)(SAMPLE_PAGE_LEASE_MULTI_BASE + reader);
}

static inline uint8_t sample_page_lease_rec_slot(uint8_t reader)
{
    if (reader >= SAMPLE_PAGE_LEASE_REC_COUNT) return UINT8_MAX;
    return (uint8_t)(SAMPLE_PAGE_LEASE_REC_BASE + reader);
}

#ifdef __cplusplus
}
#endif
