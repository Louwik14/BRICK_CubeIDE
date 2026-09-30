#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_format.h"

/*
 * Final bounded streaming limits shared by leases, scheduler and physical
 * I/O boundary.
 */
#define SAMPLE_STREAM_TARGET_MAX_VOICES          (8U)
#define SAMPLE_STREAM_RESERVED_OVERDUB_READERS   (1U)
#define SAMPLE_STREAM_ACTIVE_READER_CAPACITY \
    (SAMPLE_STREAM_TARGET_MAX_VOICES + SAMPLE_STREAM_RESERVED_OVERDUB_READERS)
#define SAMPLE_STREAM_TARGET_MOBILE_MAX_PAGES_PER_VOICE \
    SAMPLE_AUDIO_FORMAT_STREAM_WINDOW_PAGES
#define SAMPLE_STREAM_TARGET_MAX_IO_IN_FLIGHT    (1U)
#define SAMPLE_STREAM_IO_MAX_READERS SAMPLE_STREAM_ACTIVE_READER_CAPACITY

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
_Static_assert(SAMPLE_STREAM_TARGET_MAX_IO_IN_FLIGHT == 1U,
               "streaming keeps one monocore I/O operation in flight");
_Static_assert(SAMPLE_STREAM_PAGES_PER_VOICE_PER_ROUND > 0U,
               "stream round must distribute at least one page per voice");
#endif
