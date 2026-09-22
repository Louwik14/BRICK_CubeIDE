#pragma once

#include <stdint.h>

#include "Sampler/multi_sample_config.h"
#include "Sampler/sample_classic_config.h"
#include "Sampler/sample_audio_format.h"
#include "Sampler/brick6_sampler_multi_contract.h"

/*
 * Product page-cache sizing.
 *
 * Classic Stream, Multi and RAM share the slot-pool product budget; voice
 * windows and margin pages stay in their reserved ranges.
 */

#define SAMPLE_PAGE_CACHE_TARGET_BUDGET_BYTES \
    (1504U * 16U * 1024U)
#define SAMPLE_PAGE_CACHE_TARGET_PAGE_COUNT \
    (SAMPLE_PAGE_CACHE_TARGET_BUDGET_BYTES / SAMPLE_AUDIO_FORMAT_PAGE_BYTES)
#define SAMPLE_PAGE_BYTES                     SAMPLE_AUDIO_FORMAT_PAGE_BYTES
#define SAMPLE_PAGE_FRAMES                    SAMPLE_AUDIO_FORMAT_STEREO_FRAMES_PER_PAGE
#define SAMPLE_PAGE_CHANNELS                  (2U)
#define SAMPLE_PAGE_SAMPLE_BYTES              SAMPLE_AUDIO_FORMAT_FLOAT_BYTES
#define SAMPLE_PAGE_FRAME_STRIDE_FLOATS       SAMPLE_AUDIO_FORMAT_STEREO_STRIDE_FLOATS
#define SAMPLE_PAGE_BYTES_PER_FRAME           (SAMPLE_PAGE_FRAME_STRIDE_FLOATS * SAMPLE_PAGE_SAMPLE_BYTES)
#define SAMPLE_PAGE_MAX_COUNT                 (SAMPLE_PAGE_CACHE_TARGET_BUDGET_BYTES / SAMPLE_PAGE_BYTES)
#define SAMPLE_PAGE_CACHE_PATH_MAX            SAMPLE_CLASSIC_PATH_MAX
#define SAMPLE_PREP_MIN_READY_FRAMES          SAMPLE_AUDIO_FORMAT_MIN_READY_FRAMES
#define SAMPLE_PREP_MULTI_TARGET_BUDGET_BYTES (20U * 1024U * 1024U)
#define SAMPLE_PAGE_MIN_READY_FRAMES          SAMPLE_PREP_MIN_READY_FRAMES
#define SAMPLE_PAGE_MIN_READY_PAGES \
    SAMPLE_AUDIO_FORMAT_STEREO_PRESOCLE_PAGES

#define SAMPLE_PAGE_CLASSIC_FORWARD_WINDOW_PAGES SAMPLE_PAGE_MIN_READY_PAGES
#define SAMPLE_PAGE_CLASSIC_REVERSE_WINDOW_PAGES SAMPLE_PAGE_MIN_READY_PAGES
#if BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST
#define SAMPLE_PAGE_MULTI_WINDOW_PAGES \
    ((SAMPLE_AUDIO_FORMAT_MULTI_MOBILE_FRAMES \
      + SAMPLE_AUDIO_FORMAT_STEREO_FRAMES_PER_PAGE - 1U) \
     / SAMPLE_AUDIO_FORMAT_STEREO_FRAMES_PER_PAGE)
#define SAMPLE_PAGE_VOICE_LOOP_CACHE_MAX_PAGES \
    ((SAMPLE_AUDIO_FORMAT_VOICE_LOOP_CACHE_FRAMES \
      + SAMPLE_AUDIO_FORMAT_STEREO_FRAMES_PER_PAGE - 1U) \
     / SAMPLE_AUDIO_FORMAT_STEREO_FRAMES_PER_PAGE)
#else
#define SAMPLE_PAGE_MULTI_WINDOW_PAGES \
    ((SAMPLE_AUDIO_FORMAT_MULTI_MOBILE_FRAMES \
      + SAMPLE_AUDIO_FORMAT_MONO_FRAMES_PER_PAGE - 1U) \
     / SAMPLE_AUDIO_FORMAT_MONO_FRAMES_PER_PAGE)
#define SAMPLE_PAGE_VOICE_LOOP_CACHE_MAX_PAGES \
    ((SAMPLE_AUDIO_FORMAT_VOICE_LOOP_CACHE_FRAMES \
      + SAMPLE_AUDIO_FORMAT_MONO_FRAMES_PER_PAGE - 1U) \
     / SAMPLE_AUDIO_FORMAT_MONO_FRAMES_PER_PAGE)
#endif
#define SAMPLE_PAGE_CLASSIC_FORWARD_LOOKAHEAD_PAGES \
    (SAMPLE_PAGE_CLASSIC_FORWARD_WINDOW_PAGES - 1U)
#define SAMPLE_PAGE_CLASSIC_REVERSE_LOOKAHEAD_PAGES \
    (SAMPLE_PAGE_CLASSIC_REVERSE_WINDOW_PAGES - 1U)
#define SAMPLE_PAGE_MULTI_LOOKAHEAD_PAGES \
    (SAMPLE_PAGE_MULTI_WINDOW_PAGES - 1U)

/*
 * Page-cache contract sizing. The Stream backend now covers the active global
 * sample capacity; other domains use explicit id ranges.
 */
#define SAMPLE_PAGE_CACHE_CLASSIC_ID_BASE     (0U)
#define SAMPLE_PAGE_CACHE_CLASSIC_ID_CAPACITY (SAMPLE_CLASSIC_CAPACITY)
#define SAMPLE_PAGE_CACHE_MULTI_ID_BASE       (SAMPLE_PAGE_CACHE_CLASSIC_ID_BASE \
                                               + SAMPLE_PAGE_CACHE_CLASSIC_ID_CAPACITY)
#define SAMPLE_PAGE_CACHE_MULTI_ID_CAPACITY   (MULTI_SAMPLE_MAX_SAMPLES)
#define SAMPLE_PAGE_CACHE_REC_ID_BASE         (SAMPLE_PAGE_CACHE_MULTI_ID_BASE \
                                               + SAMPLE_PAGE_CACHE_MULTI_ID_CAPACITY)
/* Recorder keeps CURRENT, UNDO, RETIRED and BUILDING source slots. */
#define SAMPLE_PAGE_CACHE_REC_ID_CAPACITY     (4U)
#define SAMPLE_PAGE_CACHE_ID_CAPACITY         (SAMPLE_PAGE_CACHE_REC_ID_BASE \
                                               + SAMPLE_PAGE_CACHE_REC_ID_CAPACITY)
#define SAMPLE_PAGE_CACHE_MAX_SAMPLES         (SAMPLE_PAGE_CACHE_ID_CAPACITY)
/* Multi page-window reserve; Stream admits at most 8 active voices. */
#define SAMPLE_PAGE_CACHE_MAX_VOICES          BRICK6_SAMPLER_MULTI_MAX_VOICES
/* Product page-cache budget: keep this margin outside Multi slot presocle pages. */
#define SAMPLE_PAGE_PRODUCT_MARGIN_BUDGET_BYTES (128U * 16U * 1024U)
#define SAMPLE_PAGE_PRODUCT_MARGIN_PAGES \
    (SAMPLE_PAGE_PRODUCT_MARGIN_BUDGET_BYTES / SAMPLE_PAGE_BYTES)
#if SAMPLE_AUDIO_FORMAT_VOICE_LOOP_CACHE_FRAMES > 0U
#define SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES \
    (SAMPLE_PAGE_CACHE_MAX_VOICES \
     * (SAMPLE_PAGE_MULTI_WINDOW_PAGES \
        + SAMPLE_PAGE_VOICE_LOOP_CACHE_MAX_PAGES))
#else
#define SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES \
    (SAMPLE_PAGE_CACHE_MAX_VOICES * SAMPLE_PAGE_MULTI_WINDOW_PAGES * 2U)
#endif
#define SAMPLE_PAGE_PRODUCT_SLOT_POOL_PAGES \
    (SAMPLE_PAGE_MAX_COUNT - SAMPLE_PAGE_PRODUCT_MARGIN_PAGES \
     - SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES)
#define SAMPLE_PAGE_PRODUCT_MAX_LONG_SAMPLE_SLOTS_RAW \
    (SAMPLE_PAGE_PRODUCT_SLOT_POOL_PAGES / SAMPLE_PAGE_MIN_READY_PAGES)
#define SAMPLE_PAGE_PRODUCT_MAX_LONG_SAMPLE_SLOTS \
    ((SAMPLE_PAGE_PRODUCT_MAX_LONG_SAMPLE_SLOTS_RAW < SAMPLE_CLASSIC_CAPACITY) \
         ? SAMPLE_PAGE_PRODUCT_MAX_LONG_SAMPLE_SLOTS_RAW \
         : SAMPLE_CLASSIC_CAPACITY)

#define SAMPLE_PAGE_SLOT_POOL_START          (0U)
#define SAMPLE_PAGE_SLOT_POOL_COUNT          SAMPLE_PAGE_PRODUCT_SLOT_POOL_PAGES
#define SAMPLE_PREP_MULTI_TARGET_BUDGET_PAGES \
    (SAMPLE_PREP_MULTI_TARGET_BUDGET_BYTES / SAMPLE_PAGE_BYTES)
#define SAMPLE_PREP_MULTI_BUDGET_PAGES \
    ((SAMPLE_PREP_MULTI_TARGET_BUDGET_PAGES < SAMPLE_PAGE_SLOT_POOL_COUNT) \
         ? SAMPLE_PREP_MULTI_TARGET_BUDGET_PAGES \
         : SAMPLE_PAGE_SLOT_POOL_COUNT)
#define SAMPLE_PREP_MULTI_BUDGET_BYTES \
    (SAMPLE_PREP_MULTI_BUDGET_PAGES * SAMPLE_PAGE_BYTES)
#define SAMPLE_PREP_MULTI_START_SLOT_PAGES SAMPLE_AUDIO_FORMAT_MULTI_START_SLOT_PAGES
#define SAMPLE_PREP_MULTI_START_SLOT_BUDGET \
    (SAMPLE_PREP_MULTI_BUDGET_PAGES / SAMPLE_PREP_MULTI_START_SLOT_PAGES)
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

#if (SAMPLE_PAGE_FRAMES * SAMPLE_PAGE_BYTES_PER_FRAME != SAMPLE_PAGE_BYTES)
#error "stereo page aliases must describe one physical page"
#endif

#if (SAMPLE_AUDIO_FORMAT_MONO_FRAMES_PER_PAGE * SAMPLE_AUDIO_FORMAT_FLOAT_BYTES \
     != SAMPLE_AUDIO_FORMAT_PAGE_BYTES)
#error "mono page geometry must fit one physical page"
#endif

#if (SAMPLE_AUDIO_FORMAT_STEREO_FRAMES_PER_PAGE * SAMPLE_AUDIO_FORMAT_STEREO_STRIDE_FLOATS \
     * SAMPLE_AUDIO_FORMAT_FLOAT_BYTES != SAMPLE_AUDIO_FORMAT_PAGE_BYTES)
#error "stereo page geometry must fit one physical page"
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

#if BRICK6_STREAM_PRODUCT_MULTI_CHANNEL_COST
#if (SAMPLE_PAGE_BYTES != (64U * 1024U))
#error "Streamer product contract requires 64 KiB pages"
#endif
#if (SAMPLE_PAGE_FRAMES != 8192U)
#error "Streamer product contract requires 8192 stereo frames per page"
#endif
#if (SAMPLE_PAGE_MAX_COUNT != 376U)
#error "Streamer product cache budget must expose 376 physical pages"
#endif
#if (SAMPLE_PAGE_MULTI_WINDOW_PAGES != 3U)
#error "Stereo Multi mobile window must cover 24576 frames"
#endif
#if (SAMPLE_PAGE_VOICE_LOOP_CACHE_MAX_PAGES != 2U)
#error "Stereo Multi loop cache must cover 16384 frames"
#endif
#if (SAMPLE_PAGE_PRODUCT_VOICE_RESERVE_PAGES != 40U)
#error "Multi runtime reserve must cover 8 x (3 mobile + 2 loop) pages"
#endif
#if (SAMPLE_PREP_MULTI_BUDGET_PAGES != 304U)
#error "Multi START budget must expose 304 physical pages"
#endif
#if (SAMPLE_PREP_MULTI_START_SLOT_BUDGET != 304U)
#error "Multi START budget must expose 304 mono-equivalent slots"
#endif
#endif

#if (SAMPLE_CLASSIC_CAPACITY > SAMPLE_PAGE_CACHE_ID_CAPACITY)
#error "sample cache hot capacity must fit in page-cache id capacity"
#endif

#if ((SAMPLE_PAGE_CACHE_MULTI_ID_BASE + SAMPLE_PAGE_CACHE_MULTI_ID_CAPACITY) \
     > SAMPLE_PAGE_CACHE_ID_CAPACITY)
#error "multi sample ids must fit in page-cache id capacity"
#endif
