#pragma once

#include <stdint.h>

/*
 * Final bounded streaming limits. These contracts are shared by the need
 * registry, scheduler and physical I/O boundary.
 */
#define SAMPLE_STREAM_TARGET_MAX_VOICES          (8U)
#define SAMPLE_STREAM_TARGET_MOBILE_NEEDS_PER_VOICE (6U)
#define SAMPLE_STREAM_TARGET_PRELOAD_NEEDS_PER_VOICE (6U)
#define SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE     \
    (SAMPLE_STREAM_TARGET_MOBILE_NEEDS_PER_VOICE \
     + SAMPLE_STREAM_TARGET_PRELOAD_NEEDS_PER_VOICE)
#define SAMPLE_STREAM_TARGET_MAX_NEEDS           \
    (SAMPLE_STREAM_TARGET_MAX_VOICES * SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE)
#define SAMPLE_STREAM_TARGET_MAX_IO_IN_FLIGHT    (1U)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_STREAM_TARGET_MAX_VOICES <= UINT8_MAX,
               "stream voice index must fit in uint8_t");
_Static_assert(SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE <= UINT8_MAX,
               "stream need count must fit in uint8_t");
_Static_assert(SAMPLE_STREAM_TARGET_MAX_NEEDS <= UINT16_MAX,
               "stream need count must fit in uint16_t");
_Static_assert(SAMPLE_STREAM_TARGET_MAX_IO_IN_FLIGHT == 1U,
               "streaming keeps one monocore I/O operation in flight");
#endif
