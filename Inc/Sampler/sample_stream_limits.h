#pragma once

#include <stdint.h>

/*
 * Final bounded streaming limits. These contracts are shared by the need
 * registry, scheduler and physical I/O boundary.
 */
#define SAMPLE_STREAM_TARGET_MAX_VOICES          (8U)
#ifndef BRICK6_STREAM_PRODUCT_MULTI_MOBILE_PAGES
#define BRICK6_STREAM_PRODUCT_MULTI_MOBILE_PAGES (3U)
#endif
#ifndef BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST
#define BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST (1U)
#endif
#if BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST
#define SAMPLE_STREAM_TARGET_MOBILE_MAX_PAGES_PER_VOICE \
    (2U * BRICK6_STREAM_PRODUCT_MULTI_MOBILE_PAGES)
#else
#define SAMPLE_STREAM_TARGET_MOBILE_MAX_PAGES_PER_VOICE \
    BRICK6_STREAM_PRODUCT_MULTI_MOBILE_PAGES
#endif
#define SAMPLE_STREAM_TARGET_MOBILE_NEEDS_PER_VOICE \
    SAMPLE_STREAM_TARGET_MOBILE_MAX_PAGES_PER_VOICE
#define SAMPLE_STREAM_TARGET_PRELOAD_NEEDS_PER_VOICE (6U)
#define SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE     \
    (SAMPLE_STREAM_TARGET_MOBILE_NEEDS_PER_VOICE \
     + SAMPLE_STREAM_TARGET_PRELOAD_NEEDS_PER_VOICE)
#define SAMPLE_STREAM_TARGET_MAX_NEEDS           \
    (SAMPLE_STREAM_TARGET_MAX_VOICES * SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE)
#define SAMPLE_STREAM_TARGET_MAX_IO_IN_FLIGHT    (1U)
#define SAMPLE_STREAM_IO_MAX_READERS              SAMPLE_STREAM_TARGET_MAX_VOICES

/*
 * Calibration seam: number of complete voice passes allowed per service
 * round. Pages are always issued one voice at a time, so a value of 2 gives
 * V1..V8, then V1..V8 again; it never gives two consecutive pages to V1.
 */
#ifndef SAMPLE_STREAM_PAGES_PER_VOICE_PER_ROUND
#define SAMPLE_STREAM_PAGES_PER_VOICE_PER_ROUND  (1U)
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_STREAM_TARGET_MAX_VOICES <= UINT8_MAX,
               "stream voice index must fit in uint8_t");
_Static_assert(SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE <= UINT8_MAX,
               "stream need count must fit in uint8_t");
_Static_assert(SAMPLE_STREAM_TARGET_MAX_NEEDS <= UINT16_MAX,
               "stream need count must fit in uint16_t");
_Static_assert(SAMPLE_STREAM_TARGET_MAX_IO_IN_FLIGHT == 1U,
               "streaming keeps one monocore I/O operation in flight");
_Static_assert(SAMPLE_STREAM_PAGES_PER_VOICE_PER_ROUND > 0U,
               "stream round must distribute at least one page per voice");
#endif
