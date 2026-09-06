#pragma once

#include <stdint.h>

/*
 * Final bounded streaming limits shared by leases, scheduler and physical
 * I/O boundary.
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
#define SAMPLE_STREAM_TARGET_MAX_IO_IN_FLIGHT    (1U)
#define SAMPLE_STREAM_IO_MAX_READERS              SAMPLE_STREAM_TARGET_MAX_VOICES

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_STREAM_TARGET_MAX_VOICES <= UINT8_MAX,
               "stream voice index must fit in uint8_t");
_Static_assert(SAMPLE_STREAM_TARGET_MAX_IO_IN_FLIGHT == 1U,
               "streaming keeps one bounded I/O operation in flight");
#endif
