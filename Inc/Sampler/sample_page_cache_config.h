#pragma once

#include <stdint.h>

#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_pool.h"

/*
 * Phase 1 page-cache sizing only.
 *
 * The current product path still uses sample_cache and its READY_FULL /
 * READY_PARTIAL ring logic. This config file only defines the target layout for
 * the future paged cache introduced in parallel.
 */

#define SAMPLE_PAGE_CACHE_TARGET_BUDGET_BYTES (19U * 1024U * 1024U)
#define SAMPLE_PAGE_FRAMES                    (2048U)
#define SAMPLE_PAGE_CHANNELS                  (2U)
#define SAMPLE_PAGE_SAMPLE_BYTES              (4U)
#define SAMPLE_PAGE_FRAME_STRIDE_FLOATS       (SAMPLE_PAGE_CHANNELS)
#define SAMPLE_PAGE_BYTES_PER_FRAME           (SAMPLE_PAGE_CHANNELS * SAMPLE_PAGE_SAMPLE_BYTES)
#define SAMPLE_PAGE_BYTES                     (SAMPLE_PAGE_FRAMES * SAMPLE_PAGE_BYTES_PER_FRAME)
#define SAMPLE_PAGE_MAX_COUNT                 (SAMPLE_PAGE_CACHE_TARGET_BUDGET_BYTES / SAMPLE_PAGE_BYTES)
#define SAMPLE_PAGE_CACHE_PATH_MAX            SAMPLE_POOL_PATH_MAX
#define SAMPLE_PREP_MIN_READY_FRAMES          (8192U)
#define SAMPLE_PREP_MULTI_BUDGET_BYTES        (8U * 1024U * 1024U)
#define SAMPLE_PREP_MULTI_BUDGET_PAGES \
    (SAMPLE_PREP_MULTI_BUDGET_BYTES / SAMPLE_PAGE_BYTES)
#define SAMPLE_PAGE_MIN_READY_FRAMES          SAMPLE_PREP_MIN_READY_FRAMES
#define SAMPLE_PAGE_MIN_READY_PAGES \
    ((SAMPLE_PREP_MIN_READY_FRAMES + SAMPLE_PAGE_FRAMES - 1U) / SAMPLE_PAGE_FRAMES)

typedef enum
{
    SAMPLE_PREP_PROFILE_CLASSIC = 0,
    SAMPLE_PREP_PROFILE_MULTI
} sample_prep_profile_t;

#define SAMPLE_PAGE_CLASSIC_FORWARD_WINDOW_PAGES SAMPLE_PAGE_MIN_READY_PAGES
#define SAMPLE_PAGE_CLASSIC_REVERSE_WINDOW_PAGES SAMPLE_PAGE_MIN_READY_PAGES
#define SAMPLE_PAGE_MULTI_WINDOW_PAGES           (4U)
#define SAMPLE_PAGE_CLASSIC_FORWARD_LOOKAHEAD_PAGES \
    (SAMPLE_PAGE_CLASSIC_FORWARD_WINDOW_PAGES - 1U)
#define SAMPLE_PAGE_CLASSIC_REVERSE_LOOKAHEAD_PAGES \
    (SAMPLE_PAGE_CLASSIC_REVERSE_WINDOW_PAGES - 1U)
#define SAMPLE_PAGE_MULTI_LOOKAHEAD_PAGES \
    (SAMPLE_PAGE_MULTI_WINDOW_PAGES - 1U)

/*
 * Page-cache contract sizing. This is intentionally independent from the
 * current sample_pool product path, which still exposes 64 slots today.
 */
#define SAMPLE_PAGE_CACHE_CLASSIC_ID_BASE     (0U)
#define SAMPLE_PAGE_CACHE_CLASSIC_ID_CAPACITY (SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
#define SAMPLE_PAGE_CACHE_LOOPER_ID_BASE      (SAMPLE_PAGE_CACHE_CLASSIC_ID_BASE \
                                               + SAMPLE_PAGE_CACHE_CLASSIC_ID_CAPACITY)
#define SAMPLE_PAGE_CACHE_LOOPER_ID_CAPACITY  (64U)
#define SAMPLE_PAGE_CACHE_MULTI_ID_BASE       (SAMPLE_PAGE_CACHE_LOOPER_ID_BASE \
                                               + SAMPLE_PAGE_CACHE_LOOPER_ID_CAPACITY)
#define SAMPLE_PAGE_CACHE_MULTI_ID_CAPACITY   (MULTI_SAMPLE_MAX_SAMPLES)
#define SAMPLE_PAGE_CACHE_ID_CAPACITY         (SAMPLE_PAGE_CACHE_MULTI_ID_BASE \
                                               + SAMPLE_PAGE_CACHE_MULTI_ID_CAPACITY)
#define SAMPLE_PAGE_CACHE_MAX_SAMPLES         (SAMPLE_PAGE_CACHE_ID_CAPACITY)
#define SAMPLE_PAGE_CACHE_MAX_VOICES          (16U)

/* Product page-cache budget: keep this margin outside Multi slot presocle pages. */
#define SAMPLE_PAGE_PRODUCT_MARGIN_PAGES      (128U)
#define SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES \
    (SAMPLE_PAGE_CACHE_MAX_VOICES * SAMPLE_PAGE_MULTI_WINDOW_PAGES * 2U)
#define SAMPLE_PAGE_PRODUCT_SLOT_POOL_PAGES \
    (SAMPLE_PAGE_MAX_COUNT - SAMPLE_PAGE_PRODUCT_MARGIN_PAGES \
     - SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES)
#define SAMPLE_PAGE_PRODUCT_MAX_LONG_SAMPLE_SLOTS \
    (SAMPLE_PAGE_PRODUCT_SLOT_POOL_PAGES / SAMPLE_PAGE_MIN_READY_PAGES)

#define SAMPLE_PAGE_SLOT_POOL_START          (0U)
#define SAMPLE_PAGE_SLOT_POOL_COUNT          SAMPLE_PAGE_PRODUCT_SLOT_POOL_PAGES
#define SAMPLE_PAGE_VOICE_WINDOW_POOL_START  (SAMPLE_PAGE_SLOT_POOL_START \
                                              + SAMPLE_PAGE_SLOT_POOL_COUNT)
#define SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT  SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES
#define SAMPLE_PAGE_MARGIN_POOL_START        (SAMPLE_PAGE_VOICE_WINDOW_POOL_START \
                                              + SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT)
#define SAMPLE_PAGE_MARGIN_POOL_COUNT        SAMPLE_PAGE_PRODUCT_MARGIN_PAGES
#define SAMPLE_PAGE_POOL_RANGE_TOTAL         (SAMPLE_PAGE_SLOT_POOL_COUNT \
                                              + SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT \
                                              + SAMPLE_PAGE_MARGIN_POOL_COUNT)

static inline uint8_t sample_page_slot_is_slot_pool(uint32_t slot)
{
    return ((slot >= SAMPLE_PAGE_SLOT_POOL_START)
            && (slot < (SAMPLE_PAGE_SLOT_POOL_START + SAMPLE_PAGE_SLOT_POOL_COUNT)))
               ? 1U
               : 0U;
}

static inline uint8_t sample_page_slot_is_voice_window_pool(uint32_t slot)
{
    return ((slot >= SAMPLE_PAGE_VOICE_WINDOW_POOL_START)
            && (slot < (SAMPLE_PAGE_VOICE_WINDOW_POOL_START
                        + SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT)))
               ? 1U
               : 0U;
}

static inline uint8_t sample_page_slot_is_margin_pool(uint32_t slot)
{
    return ((slot >= SAMPLE_PAGE_MARGIN_POOL_START)
            && (slot < (SAMPLE_PAGE_MARGIN_POOL_START + SAMPLE_PAGE_MARGIN_POOL_COUNT)))
               ? 1U
               : 0U;
}

#if (SAMPLE_PAGE_BYTES == 0U)
#error "SAMPLE_PAGE_BYTES must be non-zero"
#endif

#if (SAMPLE_PAGE_MAX_COUNT == 0U)
#error "SAMPLE_PAGE_MAX_COUNT must be non-zero"
#endif

#if ((SAMPLE_PAGE_PRODUCT_MARGIN_PAGES + SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES) \
     >= SAMPLE_PAGE_MAX_COUNT)
#error "product page-cache margin and voice reserve must fit in page cache"
#endif

#if (SAMPLE_PAGE_POOL_RANGE_TOTAL != SAMPLE_PAGE_MAX_COUNT)
#error "sample page pool ranges must cover the full page pool"
#endif

#if (SAMPLE_PAGE_SLOT_POOL_COUNT != SAMPLE_PAGE_PRODUCT_SLOT_POOL_PAGES)
#error "sample slot pool range must match product slot pool pages"
#endif

#if (SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT != SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES)
#error "sample voice window pool range must match product voice reserve pages"
#endif

#if (SAMPLE_PAGE_MARGIN_POOL_COUNT != SAMPLE_PAGE_PRODUCT_MARGIN_PAGES)
#error "sample margin pool range must match product margin pages"
#endif

#if (SAMPLE_CACHE_HOT_SAMPLE_CAPACITY > SAMPLE_PAGE_CACHE_ID_CAPACITY)
#error "sample cache hot capacity must fit in page-cache id capacity"
#endif

#if ((SAMPLE_PAGE_CACHE_MULTI_ID_BASE + SAMPLE_PAGE_CACHE_MULTI_ID_CAPACITY) \
     > SAMPLE_PAGE_CACHE_ID_CAPACITY)
#error "multi sample ids must fit in page-cache id capacity"
#endif
