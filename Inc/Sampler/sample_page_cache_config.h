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

#define SAMPLE_PAGE_CACHE_TARGET_BUDGET_BYTES (16U * 1024U * 1024U)
#define SAMPLE_PAGE_FRAMES                    (512U)
#define SAMPLE_PAGE_CHANNELS                  (2U)
#define SAMPLE_PAGE_SAMPLE_BYTES              (4U)
#define SAMPLE_PAGE_FRAME_STRIDE_FLOATS       (SAMPLE_PAGE_CHANNELS)
#define SAMPLE_PAGE_BYTES_PER_FRAME           (SAMPLE_PAGE_CHANNELS * SAMPLE_PAGE_SAMPLE_BYTES)
#define SAMPLE_PAGE_BYTES                     (SAMPLE_PAGE_FRAMES * SAMPLE_PAGE_BYTES_PER_FRAME)
#define SAMPLE_PAGE_MAX_COUNT                 (SAMPLE_PAGE_CACHE_TARGET_BUDGET_BYTES / SAMPLE_PAGE_BYTES)
#define SAMPLE_PAGE_CACHE_PATH_MAX            SAMPLE_POOL_PATH_MAX

#define SAMPLE_PAGE_CLASSIC_FORWARD_WINDOW_PAGES (12U)
#define SAMPLE_PAGE_CLASSIC_REVERSE_WINDOW_PAGES (8U)
#define SAMPLE_PAGE_MULTI_WINDOW_PAGES           (28U)
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

#if (SAMPLE_PAGE_BYTES == 0U)
#error "SAMPLE_PAGE_BYTES must be non-zero"
#endif

#if (SAMPLE_PAGE_MAX_COUNT == 0U)
#error "SAMPLE_PAGE_MAX_COUNT must be non-zero"
#endif

#if (SAMPLE_CACHE_HOT_SAMPLE_CAPACITY > SAMPLE_PAGE_CACHE_ID_CAPACITY)
#error "sample cache hot capacity must fit in page-cache id capacity"
#endif

#if ((SAMPLE_PAGE_CACHE_MULTI_ID_BASE + SAMPLE_PAGE_CACHE_MULTI_ID_CAPACITY) \
     > SAMPLE_PAGE_CACHE_ID_CAPACITY)
#error "multi sample ids must fit in page-cache id capacity"
#endif
