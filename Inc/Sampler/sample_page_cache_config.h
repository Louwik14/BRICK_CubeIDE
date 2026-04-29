#pragma once

#include <stdint.h>

/*
 * Phase 1 page-cache sizing only.
 *
 * The current product path still uses sample_cache and its READY_FULL /
 * READY_PARTIAL ring logic. This config file only defines the target layout for
 * the future paged cache introduced in parallel.
 */

#define SAMPLE_PAGE_CACHE_TARGET_BUDGET_BYTES (8U * 1024U * 1024U)
#define SAMPLE_PAGE_FRAMES                    (1024U)
#define SAMPLE_PAGE_CHANNELS                  (2U)
#define SAMPLE_PAGE_SAMPLE_BYTES              (4U)
#define SAMPLE_PAGE_FRAME_STRIDE_FLOATS       (SAMPLE_PAGE_CHANNELS)
#define SAMPLE_PAGE_BYTES_PER_FRAME           (SAMPLE_PAGE_CHANNELS * SAMPLE_PAGE_SAMPLE_BYTES)
#define SAMPLE_PAGE_BYTES                     (SAMPLE_PAGE_FRAMES * SAMPLE_PAGE_BYTES_PER_FRAME)
#define SAMPLE_PAGE_MAX_COUNT                 (SAMPLE_PAGE_CACHE_TARGET_BUDGET_BYTES / SAMPLE_PAGE_BYTES)

/*
 * Page-cache contract sizing. This is intentionally independent from the
 * current sample_pool product path, which still exposes 64 slots today.
 */
#define SAMPLE_PAGE_CACHE_MAX_SAMPLES         (128U)
#define SAMPLE_PAGE_CACHE_MAX_VOICES          (16U)

#if (SAMPLE_PAGE_BYTES == 0U)
#error "SAMPLE_PAGE_BYTES must be non-zero"
#endif

#if (SAMPLE_PAGE_MAX_COUNT == 0U)
#error "SAMPLE_PAGE_MAX_COUNT must be non-zero"
#endif
