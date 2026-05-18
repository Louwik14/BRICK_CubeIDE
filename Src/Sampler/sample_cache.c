#include "Sampler/sample_cache.h"

#include <ctype.h>
#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "Storage/wav_audio_codec.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_manager.h"
#include "ff.h"

#define SAMPLE_CACHE_MAX_VOICES (16U)
#define SAMPLE_CACHE_IO_BYTES (4096U)
#define SAMPLE_CACHE_STREAM_START_PAGES (2U)
#define SAMPLE_CACHE_STREAM_TAIL_PAGES (1U)
#define SAMPLE_CACHE_STREAM_REVERSE_PAGES SAMPLE_CACHE_STREAM_TAIL_PAGES
#define SAMPLE_CACHE_STREAM_STATIC_PAGES (SAMPLE_CACHE_STREAM_START_PAGES \
                                          + SAMPLE_CACHE_STREAM_REVERSE_PAGES)
#define SAMPLE_CACHE_FULL_MAX_BYTES (SAMPLE_CACHE_STREAM_STATIC_PAGES * SAMPLE_PAGE_BYTES)
#define SAMPLE_CACHE_FULL_MAX_FRAMES (SAMPLE_CACHE_STREAM_STATIC_PAGES * SAMPLE_PAGE_FRAMES)

SDRAM_SAMPLES static sample_cache_desc_t g_sample_cache[SAMPLE_CACHE_HOT_SAMPLE_CAPACITY];
static AUDIO_HOT sample_cache_voice_t g_sample_cache_voice[SAMPLE_CACHE_MAX_VOICES];
static AUDIO_WARM uint8_t g_sample_cache_io_storage[SAMPLE_CACHE_IO_BYTES + 1U];
static CTRL_STATE FRESULT g_sample_cache_last_fresult[SAMPLE_CACHE_HOT_SAMPLE_CAPACITY];

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_CACHE_HOT_SAMPLE_CAPACITY <= SAMPLE_PAGE_CACHE_ID_CAPACITY,
               "hot sample cache ids must fit in the page-cache id space");
_Static_assert(SAMPLE_POOL_PROJECT_CAPACITY >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY,
               "project sample capacity must cover the hot cache capacity");
_Static_assert(SAMPLE_CACHE_FULL_MAX_BYTES
                   == (SAMPLE_CACHE_FULL_MAX_FRAMES * SAMPLE_PAGE_BYTES_PER_FRAME),
               "FULL frame threshold must match decoded stereo float bytes");
#endif

#if BRICK6_SAMPLER_DIAG_ENABLE
static CTRL_STATE sample_cache_diag_snapshot_t g_sample_cache_diag;
#define SAMPLE_CACHE_DIAG_INC(field) (++g_sample_cache_diag.field)
#define SAMPLE_CACHE_DIAG_ADD(field, value) (g_sample_cache_diag.field += (uint32_t)(value))
#define SAMPLE_CACHE_DIAG_ADD64(field, value) (g_sample_cache_diag.field += (uint64_t)(value))
#else
#define SAMPLE_CACHE_DIAG_INC(field) ((void)0)
#define SAMPLE_CACHE_DIAG_ADD(field, value) ((void)0)
#define SAMPLE_CACHE_DIAG_ADD64(field, value) ((void)0)
#endif

static uint8_t sample_cache_open_source(uint16_t sample_id, FIL *fp);
static uint8_t sample_cache_try_prepare_full_via_page_cache(uint16_t sample_id,
                                                            sample_cache_desc_t *desc,
                                                            FIL *fp);
static uint8_t sample_cache_prepare_partial_via_page_cache(uint16_t sample_id,
                                                           sample_cache_desc_t *desc);
static void sample_cache_cursor_reset(sample_stream_cursor_t *cursor);
static void sample_cache_cursor_release(sample_stream_cursor_t *cursor);
static void sample_cache_cursor_release_current_page(sample_stream_cursor_t *cursor);
static void sample_cache_cursor_clear_lookahead(sample_stream_cursor_t *cursor);
static void sample_cache_voice_reset(sample_cache_voice_t *voice);
static void sample_cache_voice_release(sample_cache_voice_t *voice);
static void sample_cache_voice_bind(sample_cache_voice_t *voice,
                                    uint16_t sample_id,
                                    uint32_t frame_index);
static void sample_cache_voice_seek(sample_cache_voice_t *voice, uint32_t frame_pos);
void sample_cache_diag_reset(void);
void sample_cache_diag_get_snapshot(sample_cache_diag_snapshot_t *out_snapshot);
static uint8_t sample_cache_voice_uses_page_cache_path(const sample_cache_desc_t *desc,
                                                       uint16_t sample_id);
static uint8_t sample_cache_voice_cursor_has_valid_span(const sample_cache_voice_t *voice,
                                                        uint32_t frame_index);
static uint8_t sample_cache_voice_cursor_request_lookahead(sample_stream_cursor_t *cursor,
                                                           const sample_cache_desc_t *desc,
                                                           uint32_t current_page_index);
static uint8_t sample_cache_voice_cursor_resolve_page(sample_cache_voice_t *voice,
                                                      const sample_cache_desc_t *desc,
                                                      uint32_t frame_index);
static uint8_t sample_cache_voice_cursor_prepare(sample_cache_voice_t *voice,
                                                 const sample_cache_desc_t *desc,
                                                 uint32_t frame_index);
static void sample_cache_invalidate_voices_for_sample(uint16_t sample_id);
#if BRICK6_SAMPLER_DIAG_ENABLE
static uint32_t sample_cache_diag_count_active_voices(void);
#endif
static void sample_cache_queue_active_stream_pages(void);
static uint32_t sample_cache_stream_last_page_index(const sample_cache_desc_t *desc);
static uint8_t sample_cache_stream_boundary_pages_ready(uint16_t sample_id,
                                                        const sample_cache_desc_t *desc);

static uint8_t *sample_cache_io_buffer(void)
{
    /*
     * Keep FatFs on the diskio scratch-buffer path. The preview path works
     * through a non-direct destination; this avoids the direct multi-block DMA
     * path for sample import while still decoding into the SDRAM cache after IO.
     */
    return &g_sample_cache_io_storage[1U];
}

static void sample_cache_clear_desc(sample_cache_desc_t *desc)
{
    if (desc == 0)
    {
        return;
    }

    memset(desc, 0, sizeof(*desc));
    desc->state = SAMPLE_CACHE_EMPTY;
    desc->mode = SAMPLE_CACHE_MODE_FULL;
}

static void sample_cache_cursor_reset(sample_stream_cursor_t *cursor)
{
    if (cursor == 0)
    {
        return;
    }

    cursor->sample_id = UINT16_MAX;
    cursor->frame_pos = 0U;
    cursor->current_page.page_index = UINT32_MAX;
    cursor->current_page.slot_index = UINT32_MAX;
    cursor->current_page.generation = 0U;
    cursor->current_page.valid = 0U;
    cursor->current_page.acquired = 0U;
    cursor->current_span.base = 0;
    cursor->current_span.start_frame = 0U;
    cursor->current_span.frame_count = 0U;
    cursor->current_span.offset_frames = 0U;
    cursor->current_span.valid = 0U;
    cursor->lookahead_page.page_index = UINT32_MAX;
    cursor->lookahead_page.slot_index = UINT32_MAX;
    cursor->lookahead_page.generation = 0U;
    cursor->lookahead_page.valid = 0U;
    cursor->lookahead_page.acquired = 0U;
    cursor->valid = 0U;
    cursor->lookahead_requested = 0U;
}

static void sample_cache_cursor_release(sample_stream_cursor_t *cursor)
{
    if (cursor == 0)
    {
        return;
    }

    sample_cache_cursor_release_current_page(cursor);
    sample_cache_cursor_clear_lookahead(cursor);
    sample_cache_cursor_reset(cursor);
}

static void sample_cache_cursor_release_current_page(sample_stream_cursor_t *cursor)
{
    if (cursor == 0)
    {
        return;
    }

    if ((cursor->current_page.acquired != 0U) && (cursor->sample_id < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
        && (cursor->current_page.valid != 0U))
    {
        SAMPLE_CACHE_DIAG_INC(page_release_calls);
        sample_page_ref_t page_ref;
        page_ref.page_index = cursor->current_page.page_index;
        page_ref.page_generation = cursor->current_page.generation;
        page_ref.slot_index = cursor->current_page.slot_index;
        sample_page_cache_release_page_ref(cursor->sample_id, &page_ref);
    }

    cursor->current_page.page_index = UINT32_MAX;
    cursor->current_page.slot_index = UINT32_MAX;
    cursor->current_page.generation = 0U;
    cursor->current_page.valid = 0U;
    cursor->current_page.acquired = 0U;
    cursor->current_span.base = 0;
    cursor->current_span.start_frame = 0U;
    cursor->current_span.frame_count = 0U;
    cursor->current_span.offset_frames = 0U;
    cursor->current_span.valid = 0U;
}

static void sample_cache_cursor_clear_lookahead(sample_stream_cursor_t *cursor)
{
    if (cursor == 0)
    {
        return;
    }

    cursor->lookahead_page.page_index = UINT32_MAX;
    cursor->lookahead_page.slot_index = UINT32_MAX;
    cursor->lookahead_page.generation = 0U;
    cursor->lookahead_page.valid = 0U;
    cursor->lookahead_page.acquired = 0U;
    cursor->lookahead_requested = 0U;
}

static void sample_cache_voice_reset(sample_cache_voice_t *voice)
{
    if (voice == 0)
    {
        return;
    }

    voice->active = 0U;
    voice->sample_id = UINT16_MAX;
    voice->frame_pos = 0U;
    voice->direction = 1;
    voice->stop_on_underrun = 1U;
    sample_cache_cursor_reset(&voice->cursor);
}

static void sample_cache_voice_release(sample_cache_voice_t *voice)
{
    if (voice == 0)
    {
        return;
    }

    sample_cache_cursor_release(&voice->cursor);
    voice->active = 0U;
    voice->sample_id = UINT16_MAX;
    voice->frame_pos = 0U;
    voice->direction = 1;
}

static void sample_cache_voice_bind(sample_cache_voice_t *voice,
                                    uint16_t sample_id,
                                    uint32_t frame_index)
{
    if (voice == 0)
    {
        return;
    }

    sample_cache_cursor_release(&voice->cursor);
    voice->sample_id = sample_id;
    voice->frame_pos = frame_index;
    voice->direction = 1;
    voice->active = 1U;
    voice->stop_on_underrun = 1U;
    voice->cursor.sample_id = sample_id;
    voice->cursor.frame_pos = frame_index;
    voice->cursor.valid = 1U;
}

static void sample_cache_voice_seek(sample_cache_voice_t *voice, uint32_t frame_pos)
{
    if (voice == 0)
    {
        return;
    }

    sample_cache_cursor_release(&voice->cursor);
    voice->frame_pos = frame_pos;
    voice->cursor.sample_id = voice->sample_id;
    voice->cursor.frame_pos = frame_pos;
    voice->cursor.valid = (voice->active != 0U) ? 1U : 0U;
}

static uint8_t sample_cache_voice_cursor_has_valid_span(const sample_cache_voice_t *voice,
                                                        uint32_t frame_index)
{
    if (voice == 0)
    {
        return 0U;
    }

    const sample_stream_cursor_t *const cursor = &voice->cursor;
    if ((cursor->valid == 0U) || (cursor->current_page.valid == 0U)
        || (cursor->current_page.acquired == 0U) || (cursor->current_span.valid == 0U)
        || (cursor->current_span.base == 0))
    {
        return 0U;
    }

    if (cursor->sample_id != voice->sample_id)
    {
        return 0U;
    }

    if (cursor->current_page.page_index != (frame_index / SAMPLE_PAGE_FRAMES))
    {
        return 0U;
    }

    if ((frame_index < cursor->current_span.start_frame)
        || (frame_index >= (cursor->current_span.start_frame + cursor->current_span.frame_count)))
    {
        return 0U;
    }

    if (cursor->current_span.offset_frames != (frame_index - cursor->current_span.start_frame))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t sample_cache_voice_cursor_request_lookahead(sample_stream_cursor_t *cursor,
                                                           const sample_cache_desc_t *desc,
                                                           uint32_t current_page_index)
{
    if ((cursor == 0) || (desc == 0))
    {
        return 0U;
    }

    if (!((desc->mode == SAMPLE_CACHE_MODE_STREAM) && (desc->fully_cached == 0U)))
    {
        sample_cache_cursor_clear_lookahead(cursor);
        return 1U;
    }

    const uint32_t next_page_index = current_page_index + 1U;
    const uint32_t next_page_start_frame = next_page_index * SAMPLE_PAGE_FRAMES;
    if (next_page_start_frame >= desc->total_frames)
    {
        sample_cache_cursor_clear_lookahead(cursor);
        return 1U;
    }

    if ((cursor->lookahead_requested != 0U) && (cursor->lookahead_page.valid != 0U)
        && (cursor->lookahead_page.page_index == next_page_index))
    {
        return 1U;
    }

    sample_page_ref_t lookahead_ref;
    SAMPLE_CACHE_DIAG_INC(lookahead_requests);
    if (sample_page_cache_request_page_ref(cursor->sample_id, next_page_index, &lookahead_ref) == 0U)
    {
        sample_cache_cursor_clear_lookahead(cursor);
        return 0U;
    }

    cursor->lookahead_page.page_index = lookahead_ref.page_index;
    cursor->lookahead_page.slot_index = lookahead_ref.slot_index;
    cursor->lookahead_page.generation = lookahead_ref.page_generation;
    cursor->lookahead_page.valid = 1U;
    cursor->lookahead_page.acquired = 0U;
    cursor->lookahead_requested = 1U;
    return 1U;
}

static uint8_t sample_cache_voice_cursor_resolve_page(sample_cache_voice_t *voice,
                                                      const sample_cache_desc_t *desc,
                                                      uint32_t frame_index)
{
    if ((voice == 0) || (desc == 0))
    {
        return 0U;
    }

    sample_stream_cursor_t *const cursor = &voice->cursor;
    const uint32_t page_index = frame_index / SAMPLE_PAGE_FRAMES;

    sample_page_span_t page_span;
    if ((cursor->lookahead_requested != 0U) && (cursor->lookahead_page.valid != 0U)
        && (cursor->lookahead_page.page_index == page_index)
        && (cursor->lookahead_page.slot_index < SAMPLE_PAGE_MAX_COUNT))
    {
        sample_page_ref_t lookahead_ref;
        lookahead_ref.page_index = cursor->lookahead_page.page_index;
        lookahead_ref.page_generation = cursor->lookahead_page.generation;
        lookahead_ref.slot_index = cursor->lookahead_page.slot_index;
        SAMPLE_CACHE_DIAG_INC(page_resolve_acquire_calls);
        if (sample_page_cache_try_acquire_page_ref(voice->sample_id, &lookahead_ref, &page_span) == 0U)
        {
            return 0U;
        }
    }
    else
    {
        SAMPLE_CACHE_DIAG_INC(page_resolve_acquire_calls);
        if (sample_page_cache_try_acquire_page(voice->sample_id, page_index, &page_span) == 0U)
        {
            return 0U;
        }
    }

    SAMPLE_CACHE_DIAG_INC(page_cache_lookup_calls);
    const sample_page_desc_t *const page_desc = sample_page_cache_get_page_desc(page_span.slot_index);
    if ((page_desc == 0) || (page_desc->data == 0)
        || (page_desc->sample_id != voice->sample_id)
        || (page_desc->page_index != page_span.page_index)
        || (page_desc->generation != page_span.page_generation)
        || (page_desc->state != SAMPLE_PAGE_READY))
    {
        sample_page_ref_t page_ref;
        page_ref.page_index = page_span.page_index;
        page_ref.page_generation = page_span.page_generation;
        page_ref.slot_index = page_span.slot_index;
        sample_page_cache_release_page_ref(voice->sample_id, &page_ref);
        return 0U;
    }

    cursor->sample_id = voice->sample_id;
    cursor->frame_pos = frame_index;
    cursor->current_page.page_index = page_span.page_index;
    cursor->current_page.slot_index = page_span.slot_index;
    cursor->current_page.generation = page_span.page_generation;
    cursor->current_page.valid = 1U;
    cursor->current_page.acquired = 1U;
    cursor->current_span.base = page_desc->data;
    cursor->current_span.start_frame = page_span.start_frame;
    cursor->current_span.frame_count = page_span.frame_count;
    cursor->current_span.offset_frames = frame_index - page_span.start_frame;
    cursor->current_span.valid = 1U;
    cursor->valid = 1U;

    if ((cursor->lookahead_requested != 0U) && (cursor->lookahead_page.valid != 0U)
        && (cursor->lookahead_page.page_index == page_index))
    {
        sample_cache_cursor_clear_lookahead(cursor);
    }

    (void)sample_cache_voice_cursor_request_lookahead(cursor, desc, page_index);
    return 1U;
}

static void sample_cache_invalidate_voices_for_sample(uint16_t sample_id)
{
    for (uint32_t i = 0U; i < SAMPLE_CACHE_MAX_VOICES; ++i)
    {
        sample_cache_voice_t *const voice = &g_sample_cache_voice[i];
        if (voice->sample_id != sample_id)
        {
            continue;
        }

        sample_cache_voice_release(voice);
    }
}

static uint8_t sample_cache_voice_uses_page_cache_path(const sample_cache_desc_t *desc,
                                                       uint16_t sample_id)
{
    if ((desc == 0) || (sample_id >= SAMPLE_PAGE_CACHE_MAX_SAMPLES))
    {
        return 0U;
    }

    return (((desc->fully_cached != 0U) && (desc->mode == SAMPLE_CACHE_MODE_FULL))
            || ((desc->fully_cached == 0U) && (desc->mode == SAMPLE_CACHE_MODE_STREAM)))
               ? 1U
               : 0U;
}

static uint8_t sample_cache_voice_cursor_prepare(sample_cache_voice_t *voice,
                                                 const sample_cache_desc_t *desc,
                                                 uint32_t frame_index)
{
    if ((voice == 0) || (desc == 0))
    {
        return 0U;
    }

    sample_stream_cursor_t *const cursor = &voice->cursor;
    cursor->sample_id = voice->sample_id;
    cursor->frame_pos = frame_index;

    if (sample_cache_voice_cursor_has_valid_span(voice, frame_index) != 0U)
    {
        SAMPLE_CACHE_DIAG_INC(cursor_span_hits);
        return 1U;
    }

    sample_cache_cursor_release_current_page(cursor);
    cursor->sample_id = voice->sample_id;
    cursor->frame_pos = frame_index;
    cursor->valid = 1U;
    return sample_cache_voice_cursor_resolve_page(voice, desc, frame_index);
}

static void sample_cache_release_slot(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
    {
        return;
    }

    sample_cache_invalidate_voices_for_sample(sample_id);
    sample_stream_manager_release_sample(sample_id);
    sample_page_cache_clear_sample(sample_id);
}

static uint8_t sample_cache_try_prepare_full_via_page_cache(uint16_t sample_id,
                                                            sample_cache_desc_t *desc,
                                                            FIL *fp)
{
    if ((desc == 0) || (fp == 0) || (desc->total_frames == 0U)
        || (desc->mode != SAMPLE_CACHE_MODE_FULL))
    {
        return 0U;
    }

    const sample_page_load_result_t page_result =
        sample_page_cache_load_full_sample(sample_id,
                                           fp,
                                           &desc->info,
                                           desc->total_frames,
                                           desc->data_offset,
                                           sample_cache_io_buffer(),
                                           SAMPLE_CACHE_IO_BYTES);
    if (page_result != SAMPLE_PAGE_LOAD_OK)
    {
        switch (page_result)
        {
            case SAMPLE_PAGE_LOAD_NO_SPACE:
                desc->last_error = 8U;
                g_sample_cache_last_fresult[sample_id] = FR_NOT_ENOUGH_CORE;
                break;

            case SAMPLE_PAGE_LOAD_SEEK_FAILED:
                desc->last_error = 9U;
                break;

            case SAMPLE_PAGE_LOAD_READ_FAILED:
            case SAMPLE_PAGE_LOAD_DECODE_FAILED:
                desc->last_error = 12U;
                break;

            case SAMPLE_PAGE_LOAD_INVALID_ARG:
            case SAMPLE_PAGE_LOAD_UNSUPPORTED_SAMPLE:
            default:
                desc->last_error = 12U;
                g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
                break;
        }
        return 0U;
    }

    uint32_t cached_frames = 0U;
    const float *const full_base = sample_page_cache_get_full_sample_base(sample_id, &cached_frames);
    if ((full_base == 0) || (cached_frames < desc->total_frames))
    {
        sample_page_cache_clear_sample(sample_id);
        desc->last_error = 12U;
        g_sample_cache_last_fresult[sample_id] = FR_INT_ERR;
        return 0U;
    }

    desc->cache = (float *)full_base;
    desc->cache_capacity_frames =
        ((desc->total_frames + SAMPLE_PAGE_FRAMES - 1U) / SAMPLE_PAGE_FRAMES) * SAMPLE_PAGE_FRAMES;
    desc->cache_window_start_frame = 0U;
    desc->cache_valid_frames = desc->total_frames;
    desc->source_read_frame = desc->total_frames;
    desc->fully_cached = 1U;
    desc->stream_active = 0U;
    desc->state = SAMPLE_CACHE_READY_FULL;
    desc->last_error = 0U;
    return 1U;
}

static uint8_t sample_cache_prepare_partial_via_page_cache(uint16_t sample_id,
                                                           sample_cache_desc_t *desc)
{
    if ((desc == 0) || (desc->mode != SAMPLE_CACHE_MODE_STREAM))
    {
        return 0U;
    }

    if (sample_page_cache_register_stream_sample(sample_id,
                                                 desc->path,
                                                 &desc->info,
                                                 desc->total_frames,
                                                 desc->data_offset) == 0U)
    {
        desc->last_error = 12U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
        return 0U;
    }

    if (sample_stream_manager_request_range(sample_id, 0U, SAMPLE_CACHE_STREAM_START_PAGES) == 0U)
    {
        desc->last_error = 8U;
        g_sample_cache_last_fresult[sample_id] = FR_NOT_ENOUGH_CORE;
        return 0U;
    }
    SAMPLE_CACHE_DIAG_INC(prepare_stream_async);
    SAMPLE_CACHE_DIAG_INC(prepare_stream_initial_queued);
    if (sample_page_cache_pin_page(sample_id, 0U) == 0U)
    {
        desc->last_error = 8U;
        g_sample_cache_last_fresult[sample_id] = FR_NOT_ENOUGH_CORE;
        return 0U;
    }
    if (sample_page_cache_get_page_state(sample_id, 0U) != SAMPLE_PAGE_READY)
    {
        SAMPLE_CACHE_DIAG_INC(prepare_stream_page0_not_ready);
    }

    /*
     * Long STREAM warm set stays capped at three 16 KiB pages:
     * page0 for immediate start, page1 for initial forward lookahead, and
     * the final page for reverse/pingpong entry near the tail.
     */
    const uint32_t last_page = sample_cache_stream_last_page_index(desc);
    if (last_page != 0U)
    {
        const uint32_t reverse_span = (last_page + 1U < SAMPLE_CACHE_STREAM_REVERSE_PAGES)
                                          ? (last_page + 1U)
                                          : SAMPLE_CACHE_STREAM_REVERSE_PAGES;
        const uint32_t reverse_first_page = (last_page + 1U) - reverse_span;
        for (uint32_t page_index = reverse_first_page; page_index <= last_page; ++page_index)
        {
            if (sample_stream_manager_request_page(sample_id, page_index) == 0U)
            {
                desc->last_error = 8U;
                g_sample_cache_last_fresult[sample_id] = FR_NOT_ENOUGH_CORE;
                return 0U;
            }
            if (sample_page_cache_pin_page(sample_id, page_index) == 0U)
            {
                desc->last_error = 8U;
                g_sample_cache_last_fresult[sample_id] = FR_NOT_ENOUGH_CORE;
                return 0U;
            }
        }
    }

    desc->cache = 0;
    desc->cache_window_start_frame = 0U;
    desc->cache_valid_frames = SAMPLE_PAGE_FRAMES * SAMPLE_CACHE_STREAM_START_PAGES;
    if (desc->cache_valid_frames > desc->total_frames)
    {
        desc->cache_valid_frames = desc->total_frames;
    }
    desc->source_read_frame = desc->cache_valid_frames;
    desc->fully_cached = 0U;
    desc->stream_active = 0U;
    desc->state = SAMPLE_CACHE_READY_PARTIAL;
    desc->last_error = 0U;
    return 1U;
}

static uint32_t sample_cache_trim_path_copy(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t start = 0U;
    uint32_t end;

    if ((dst == 0) || (dst_size == 0U) || (src == 0))
    {
        return 0U;
    }

    end = (uint32_t)strlen(src);
    while ((start < end) && (isspace((unsigned char)src[start]) != 0))
    {
        start++;
    }
    while ((end > start) && (isspace((unsigned char)src[end - 1U]) != 0))
    {
        end--;
    }

    const uint32_t len = end - start;
    if ((len == 0U) || (len >= dst_size))
    {
        return 0U;
    }

    memcpy(dst, &src[start], len);
    dst[len] = '\0';
    return len;
}

uint8_t sample_cache_wav_format_supported(const wav_info_t *info)
{
    if (info == 0)
    {
        return 0U;
    }

    return (((info->audio_format == 1U) || (info->audio_format == 65534U))
            && ((info->channels == 1U) || (info->channels == 2U))
            && ((info->bits_per_sample == 16U)
                || (info->bits_per_sample == 24U)
                || (info->bits_per_sample == 32U))
            && (info->sample_rate == 48000U)
            && (info->block_align != 0U)) ? 1U : 0U;
}

static uint8_t sample_cache_open_source(uint16_t sample_id, FIL *fp)
{
    if ((sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY) || (fp == 0))
    {
        return 0U;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    FRESULT fr = f_open(fp, desc->path, FA_READ);
    if (fr != FR_OK)
    {
        g_sample_cache_last_fresult[sample_id] = fr;
        return 0U;
    }

    const FSIZE_t offset = (FSIZE_t)desc->data_offset
                         + ((FSIZE_t)desc->source_read_frame * (FSIZE_t)desc->info.block_align);
    fr = f_lseek(fp, offset);
    if (fr != FR_OK)
    {
        g_sample_cache_last_fresult[sample_id] = fr;
        (void)f_close(fp);
        return 0U;
    }

    return 1U;
}

static uint8_t sample_cache_frame_available(const sample_cache_desc_t *desc, uint32_t frame_index)
{
    if ((desc == 0) || (desc->cache == 0) || (desc->cache_valid_frames == 0U)
        || (frame_index >= desc->total_frames))
    {
        return 0U;
    }

    if (frame_index < desc->cache_window_start_frame)
    {
        return 0U;
    }

    const uint32_t relative = frame_index - desc->cache_window_start_frame;
    return ((relative < desc->cache_valid_frames) && (relative < desc->cache_capacity_frames)) ? 1U : 0U;
}

static uint32_t sample_cache_frame_offset(const sample_cache_desc_t *desc, uint32_t frame_index)
{
    return (desc->cache_capacity_frames == 0U) ? 0U : (frame_index % desc->cache_capacity_frames);
}

static sample_cache_block_status_t sample_cache_inspect_voice_block(uint8_t voice_id,
                                                                    uint32_t max_frames,
                                                                    sample_cache_block_t *out_block)
{
    if (out_block == 0)
    {
        return SAMPLE_CACHE_BLOCK_NOT_READY;
    }

    out_block->l = 0;
    out_block->r = 0;
    out_block->frames = 0U;
    out_block->frame_stride = 0U;
    out_block->is_mono = 0U;
    out_block->status = SAMPLE_CACHE_BLOCK_NOT_READY;

    if ((voice_id >= SAMPLE_CACHE_MAX_VOICES) || (max_frames == 0U))
    {
        return SAMPLE_CACHE_BLOCK_NOT_READY;
    }

    const sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY))
    {
        return SAMPLE_CACHE_BLOCK_NOT_READY;
    }

    const sample_cache_desc_t *const desc = &g_sample_cache[voice->sample_id];
    if (((desc->mode == SAMPLE_CACHE_MODE_FULL) && ((desc->cache == 0) || (desc->cache_valid_frames == 0U)))
        || ((desc->mode == SAMPLE_CACHE_MODE_STREAM) && (desc->cache_valid_frames == 0U))
        || ((desc->state != SAMPLE_CACHE_READY_FULL)
            && (desc->state != SAMPLE_CACHE_READY_PARTIAL)
            && (desc->state != SAMPLE_CACHE_PLAYING)))
    {
        return SAMPLE_CACHE_BLOCK_NOT_READY;
    }

    if (voice->frame_pos >= desc->total_frames)
    {
        out_block->status = SAMPLE_CACHE_BLOCK_DONE;
        return SAMPLE_CACHE_BLOCK_DONE;
    }

    if (sample_cache_voice_uses_page_cache_path(desc, voice->sample_id) != 0U)
    {
        sample_cache_voice_t *const voice_mut = &g_sample_cache_voice[voice_id];
        if (sample_cache_voice_cursor_prepare(voice_mut, desc, voice->frame_pos) == 0U)
        {
            out_block->status = (desc->mode == SAMPLE_CACHE_MODE_STREAM)
                                    ? SAMPLE_CACHE_BLOCK_UNDERRUN
                                    : SAMPLE_CACHE_BLOCK_NOT_READY;
            return out_block->status;
        }

        sample_stream_cursor_t *const cursor = &voice_mut->cursor;
        uint32_t frames = cursor->current_span.frame_count - cursor->current_span.offset_frames;
        const uint32_t sample_remaining = desc->total_frames - voice->frame_pos;
        if (frames > sample_remaining)
        {
            frames = sample_remaining;
        }
        if (frames > max_frames)
        {
            frames = max_frames;
        }
        if (frames == 0U)
        {
            out_block->status = (desc->mode == SAMPLE_CACHE_MODE_STREAM)
                                    ? SAMPLE_CACHE_BLOCK_UNDERRUN
                                    : SAMPLE_CACHE_BLOCK_NOT_READY;
            return out_block->status;
        }

        out_block->l = &cursor->current_span.base[cursor->current_span.offset_frames * 2U];
        out_block->r = (desc->info.channels == 1U)
                           ? 0
                           : (&cursor->current_span.base[(cursor->current_span.offset_frames * 2U) + 1U]);
        out_block->frames = frames;
        out_block->frame_stride = 2U;
        out_block->is_mono = (desc->info.channels == 1U) ? 1U : 0U;
        out_block->status = SAMPLE_CACHE_BLOCK_OK;
        return SAMPLE_CACHE_BLOCK_OK;
    }

    if (sample_cache_frame_available(desc, voice->frame_pos) == 0U)
    {
        out_block->status = SAMPLE_CACHE_BLOCK_UNDERRUN;
        return SAMPLE_CACHE_BLOCK_UNDERRUN;
    }

    const uint32_t cache_index = sample_cache_frame_offset(desc, voice->frame_pos);
    uint32_t frames = desc->total_frames - voice->frame_pos;
    const uint32_t window_frames =
        (desc->cache_window_start_frame + desc->cache_valid_frames) - voice->frame_pos;
    const uint32_t wrap_frames = desc->cache_capacity_frames - cache_index;

    if (frames > max_frames)
    {
        frames = max_frames;
    }
    if (frames > window_frames)
    {
        frames = window_frames;
    }
    if (frames > wrap_frames)
    {
        frames = wrap_frames;
    }

    if (frames == 0U)
    {
        out_block->status = SAMPLE_CACHE_BLOCK_UNDERRUN;
        return SAMPLE_CACHE_BLOCK_UNDERRUN;
    }

    out_block->l = &desc->cache[cache_index * 2U];
    out_block->r = (desc->info.channels == 1U) ? 0 : (&desc->cache[cache_index * 2U + 1U]);
    out_block->frames = frames;
    out_block->frame_stride = 2U;
    out_block->is_mono = (desc->info.channels == 1U) ? 1U : 0U;
    out_block->status = SAMPLE_CACHE_BLOCK_OK;
    return SAMPLE_CACHE_BLOCK_OK;
}

static uint8_t sample_cache_start_frame_available(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
    {
        return 0U;
    }

    if ((g_sample_cache[sample_id].mode == SAMPLE_CACHE_MODE_STREAM)
        && (g_sample_cache[sample_id].fully_cached == 0U))
    {
        return (sample_page_cache_get_page_state(sample_id, 0U) == SAMPLE_PAGE_READY) ? 1U : 0U;
    }

    return sample_cache_frame_available(&g_sample_cache[sample_id], 0U);
}

static uint32_t sample_cache_stream_last_page_index(const sample_cache_desc_t *desc)
{
    if ((desc == 0) || (desc->total_frames == 0U))
    {
        return 0U;
    }

    return (desc->total_frames - 1U) / SAMPLE_PAGE_FRAMES;
}

static uint8_t sample_cache_stream_boundary_pages_ready(uint16_t sample_id,
                                                        const sample_cache_desc_t *desc)
{
    if ((sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY) || (desc == 0))
    {
        return 0U;
    }

    if ((desc->mode != SAMPLE_CACHE_MODE_STREAM) || (desc->fully_cached != 0U))
    {
        return sample_cache_start_frame_available(sample_id);
    }

    if (sample_page_cache_get_page_state(sample_id, 0U) != SAMPLE_PAGE_READY)
    {
        return 0U;
    }

    const uint32_t last_page = sample_cache_stream_last_page_index(desc);
    if (last_page == 0U)
    {
        return 1U;
    }

    return (sample_page_cache_get_page_state(sample_id, last_page) == SAMPLE_PAGE_READY) ? 1U : 0U;
}

#if BRICK6_SAMPLER_DIAG_ENABLE
static uint32_t sample_cache_diag_count_active_voices(void)
{
    uint32_t active = 0U;
    for (uint32_t i = 0U; i < SAMPLE_CACHE_MAX_VOICES; ++i)
    {
        if ((g_sample_cache_voice[i].active != 0U)
            && (g_sample_cache_voice[i].sample_id < SAMPLE_CACHE_HOT_SAMPLE_CAPACITY))
        {
            active++;
        }
    }

    return active;
}
#endif

static uint8_t sample_cache_any_voice_active(uint16_t sample_id)
{
    for (uint32_t i = 0U; i < SAMPLE_CACHE_MAX_VOICES; ++i)
    {
        if ((g_sample_cache_voice[i].active != 0U)
            && (g_sample_cache_voice[i].sample_id == sample_id))
        {
            return 1U;
        }
    }

    return 0U;
}

static void sample_cache_queue_active_stream_pages(void)
{
    for (uint32_t i = 0U; i < SAMPLE_CACHE_MAX_VOICES; ++i)
    {
        const sample_cache_voice_t *const voice = &g_sample_cache_voice[i];
        if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY))
        {
            continue;
        }

        const sample_cache_desc_t *const desc = &g_sample_cache[voice->sample_id];
        if ((desc->mode != SAMPLE_CACHE_MODE_STREAM) || (desc->fully_cached != 0U))
        {
            continue;
        }

        const sample_stream_active_desc_t stream_desc = {
            .key = sample_audio_key_classic(voice->sample_id),
            .current_frame = voice->frame_pos,
            .end_frame = desc->total_frames,
            .direction = voice->direction,
            .lookahead_pages = (voice->direction < 0)
                                   ? SAMPLE_CACHE_STREAM_REVERSE_PAGES
                                   : SAMPLE_CACHE_STREAM_START_PAGES,
            .request_current_page = 0U,
            .state = 0,
        };
        (void)sample_stream_manager_queue_active_pages(&stream_desc);
    }
}

static void sample_cache_finish_playback(sample_cache_desc_t *desc,
                                         sample_cache_voice_t *voice,
                                         sample_cache_state_t state)
{
    if ((desc == 0) || (voice == 0))
    {
        return;
    }

    const uint16_t sample_id = voice->sample_id;
    sample_cache_voice_release(voice);
    desc->stream_active = sample_cache_any_voice_active(sample_id);

    if ((desc->mode == SAMPLE_CACHE_MODE_STREAM) && (desc->stream_active != 0U))
    {
        desc->state = SAMPLE_CACHE_PLAYING;
        return;
    }

    if (desc->fully_cached != 0U)
    {
        desc->state = SAMPLE_CACHE_READY_FULL;
    }
    else if (desc->mode == SAMPLE_CACHE_MODE_STREAM)
    {
        desc->state = (state == SAMPLE_CACHE_UNDERRUN) ? SAMPLE_CACHE_UNDERRUN : SAMPLE_CACHE_NEEDS_REPREPARE;
        desc->reprepare_start_frame = 0U;
    }
    else
    {
        desc->state = SAMPLE_CACHE_DONE;
    }
}

static uint16_t sample_cache_pick_refill_candidate(void)
{
    for (uint16_t sample_id = 0U; sample_id < SAMPLE_CACHE_HOT_SAMPLE_CAPACITY; ++sample_id)
    {
        const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
        if (desc->mode != SAMPLE_CACHE_MODE_STREAM)
        {
            continue;
        }

        if ((desc->state == SAMPLE_CACHE_NEEDS_REPREPARE)
            || (desc->state == SAMPLE_CACHE_DONE)
            || (desc->state == SAMPLE_CACHE_UNDERRUN))
        {
            return sample_id;
        }
    }

    return SAMPLE_CACHE_HOT_SAMPLE_CAPACITY;
}

static uint8_t sample_cache_reprepare_window(uint16_t sample_id, uint32_t byte_budget)
{
    if ((sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY) || (byte_budget < g_sample_cache[sample_id].info.block_align))
    {
        return 0U;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->mode != SAMPLE_CACHE_MODE_STREAM)
        || ((desc->state != SAMPLE_CACHE_NEEDS_REPREPARE)
            && (desc->state != SAMPLE_CACHE_DONE)
            && (desc->state != SAMPLE_CACHE_UNDERRUN)))
    {
        return 0U;
    }

    if (sample_cache_any_voice_active(sample_id) != 0U)
    {
        return 0U;
    }

    uint32_t start_frame = desc->reprepare_start_frame;
    if (start_frame >= desc->total_frames)
    {
        start_frame = 0U;
    }

    desc->state = SAMPLE_CACHE_PREFILLING;
    desc->cache_window_start_frame = start_frame;
    desc->cache_valid_frames = 0U;
    desc->fully_cached = 0U;
    desc->stream_active = 0U;
    desc->last_error = 0U;

    if (sample_page_cache_register_stream_sample(sample_id,
                                                 desc->path,
                                                 &desc->info,
                                                 desc->total_frames,
                                                 desc->data_offset) == 0U)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 12U;
        return 0U;
    }

    if (sample_stream_manager_request_range(sample_id, start_frame, SAMPLE_CACHE_STREAM_START_PAGES) == 0U)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 8U;
        return 0U;
    }
    if (sample_page_cache_pin_page(sample_id, start_frame / SAMPLE_PAGE_FRAMES) == 0U)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 8U;
        return 0U;
    }

    sample_stream_manager_service(byte_budget);
    if (sample_page_cache_get_page_state(sample_id, start_frame / SAMPLE_PAGE_FRAMES) != SAMPLE_PAGE_READY)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 12U;
        return 0U;
    }

    desc->cache_valid_frames = SAMPLE_PAGE_FRAMES * SAMPLE_CACHE_STREAM_START_PAGES;
    if ((start_frame + desc->cache_valid_frames) > desc->total_frames)
    {
        desc->cache_valid_frames = desc->total_frames - start_frame;
    }
    desc->source_read_frame = start_frame + desc->cache_valid_frames;
    desc->state = SAMPLE_CACHE_READY_PARTIAL;
    return 1U;
}

void sample_cache_init(void)
{
    sample_page_cache_reset();
    sample_stream_manager_init();
    sample_cache_diag_reset();

    for (uint32_t i = 0U; i < SAMPLE_CACHE_HOT_SAMPLE_CAPACITY; ++i)
    {
        sample_cache_clear_desc(&g_sample_cache[i]);
        g_sample_cache_last_fresult[i] = FR_OK;
    }

    memset(g_sample_cache_voice, 0, sizeof(g_sample_cache_voice));
    for (uint32_t i = 0U; i < SAMPLE_CACHE_MAX_VOICES; ++i)
    {
        g_sample_cache_voice[i].voice_id = (uint8_t)i;
        sample_cache_voice_reset(&g_sample_cache_voice[i]);
    }
}

void sample_cache_diag_reset(void)
{
#if BRICK6_SAMPLER_DIAG_ENABLE
    memset(&g_sample_cache_diag, 0, sizeof(g_sample_cache_diag));
#endif
}

void sample_cache_diag_get_snapshot(sample_cache_diag_snapshot_t *out_snapshot)
{
    if (out_snapshot == 0)
    {
        return;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
#if BRICK6_SAMPLER_DIAG_ENABLE
    *out_snapshot = g_sample_cache_diag;
    out_snapshot->active_voices = sample_cache_diag_count_active_voices();
    if (out_snapshot->active_voices > out_snapshot->max_active_voices)
    {
        out_snapshot->max_active_voices = out_snapshot->active_voices;
    }
    if (out_snapshot->spans_returned != 0U)
    {
        out_snapshot->average_span_frames =
            (uint32_t)(out_snapshot->span_frames_total / out_snapshot->spans_returned);
    }
#endif
}

void sample_cache_clear(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
    {
        return;
    }

    sample_cache_release_slot(sample_id);
    sample_cache_clear_desc(&g_sample_cache[sample_id]);
    g_sample_cache_last_fresult[sample_id] = FR_OK;
}

uint8_t sample_cache_prepare(uint16_t sample_id, const char *path)
{
    if (sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
    {
        return 0U;
    }

    sample_cache_release_slot(sample_id);
    sample_cache_clear_desc(&g_sample_cache[sample_id]);
    g_sample_cache_last_fresult[sample_id] = FR_OK;

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    desc->sample_id = sample_id;
    desc->state = SAMPLE_CACHE_PREPARING;

    if (sample_cache_trim_path_copy(desc->path, sizeof(desc->path), path) == 0U)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 1U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_NAME;
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 2U;
        g_sample_cache_last_fresult[sample_id] = FR_TIMEOUT;
        return 0U;
    }

    uint8_t ok = 0U;
    uint8_t fp_open = 0U;
    FIL fp;
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        desc->last_error = 3U;
        g_sample_cache_last_fresult[sample_id] = FR_DISK_ERR;
        goto done;
    }

    if (sample_cache_open_source(sample_id, &fp) == 0U)
    {
        desc->last_error = 4U;
        goto done;
    }
    fp_open = 1U;

    if (wav_parser_parse_info(&fp, &desc->info) == 0U)
    {
        desc->last_error = 5U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_OBJECT;
        goto done;
    }

    if (sample_cache_wav_format_supported(&desc->info) == 0U)
    {
        desc->last_error = 6U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
        goto done;
    }

    desc->total_frames = desc->info.data_size / desc->info.block_align;
    desc->data_offset = desc->info.data_offset;
    desc->cache_capacity_frames = SAMPLE_CACHE_FULL_MAX_FRAMES;
    if (desc->total_frames == 0U)
    {
        desc->last_error = 7U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
        goto done;
    }

    desc->mode = (desc->total_frames <= desc->cache_capacity_frames)
                     ? SAMPLE_CACHE_MODE_FULL
                     : SAMPLE_CACHE_MODE_STREAM;
    desc->source_read_frame = 0U;

    if (desc->mode == SAMPLE_CACHE_MODE_FULL)
    {
        desc->cache = 0;
        desc->cache_window_start_frame = 0U;
        desc->state = SAMPLE_CACHE_PREFILLING;
        if (sample_cache_try_prepare_full_via_page_cache(sample_id, desc, &fp) == 0U)
        {
            goto done;
        }

        ok = 1U;
        goto done;
    }

    desc->cache = 0;
    desc->cache_window_start_frame = 0U;
    desc->state = SAMPLE_CACHE_PREFILLING;
    if (sample_cache_prepare_partial_via_page_cache(sample_id, desc) == 0U)
    {
        goto done;
    }

    ok = 1U;

done:
    if (fp_open != 0U)
    {
        (void)f_close(&fp);
    }
    if (ok == 0U)
    {
        sample_cache_release_slot(sample_id);
        desc->state = SAMPLE_CACHE_ERROR;
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
    return ok;
}

void sample_cache_service(uint32_t byte_budget)
{
    if (byte_budget == 0U)
    {
        return;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        return;
    }

    sample_cache_queue_active_stream_pages();
    sample_stream_manager_service(byte_budget);
    if (sample_stream_manager_has_pending_sd_work() != 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        return;
    }

    uint32_t remaining = byte_budget;
    while (remaining != 0U)
    {
        const uint16_t sample_id = sample_cache_pick_refill_candidate();
        if (sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
        {
            break;
        }

        if ((g_sample_cache[sample_id].state == SAMPLE_CACHE_NEEDS_REPREPARE)
            || (g_sample_cache[sample_id].state == SAMPLE_CACHE_DONE)
            || (g_sample_cache[sample_id].state == SAMPLE_CACHE_UNDERRUN))
        {
            if (sample_cache_reprepare_window(sample_id, remaining) == 0U)
            {
                break;
            }

            const uint32_t loaded_frames = g_sample_cache[sample_id].cache_valid_frames;
            const uint32_t consumed = loaded_frames * g_sample_cache[sample_id].info.block_align;
            if ((loaded_frames == 0U) || (consumed >= remaining))
            {
                break;
            }
            remaining -= consumed;
            continue;
        }

        break;
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
}

uint8_t sample_cache_has_pending_sd_work(void)
{
    sample_cache_queue_active_stream_pages();
    if (sample_stream_manager_has_pending_sd_work() != 0U)
    {
        return 1U;
    }

    return (sample_cache_pick_refill_candidate() < SAMPLE_CACHE_HOT_SAMPLE_CAPACITY) ? 1U : 0U;
}

uint8_t sample_cache_is_ready(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
    {
        return 0U;
    }

    const sample_cache_state_t state = g_sample_cache[sample_id].state;
    if (state == SAMPLE_CACHE_READY_FULL)
    {
        return 1U;
    }

    if ((state == SAMPLE_CACHE_READY_PARTIAL) || (state == SAMPLE_CACHE_PLAYING))
    {
        return 1U;
    }

    return 0U;
}

sample_cache_state_t sample_cache_get_state(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
    {
        return SAMPLE_CACHE_ERROR;
    }

    return g_sample_cache[sample_id].state;
}

uint8_t sample_cache_get_last_error(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
    {
        return 1U;
    }

    return g_sample_cache[sample_id].last_error;
}

uint8_t sample_cache_get_last_fresult(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
    {
        return (uint8_t)FR_INVALID_PARAMETER;
    }

    return (uint8_t)g_sample_cache_last_fresult[sample_id];
}

uint8_t sample_cache_start_voice_at(uint16_t sample_id, uint8_t voice_id, uint32_t frame_index)
{
    if ((sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY) || (voice_id >= SAMPLE_CACHE_MAX_VOICES))
    {
        return 0U;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->state != SAMPLE_CACHE_READY_FULL)
        && (desc->state != SAMPLE_CACHE_READY_PARTIAL)
        && (desc->state != SAMPLE_CACHE_PLAYING))
    {
        return 0U;
    }

    if (((desc->mode == SAMPLE_CACHE_MODE_STREAM) && (desc->fully_cached == 0U)
         && (sample_page_cache_get_page_state(sample_id, frame_index / SAMPLE_PAGE_FRAMES) != SAMPLE_PAGE_READY))
        || ((desc->mode != SAMPLE_CACHE_MODE_STREAM) && (sample_cache_frame_available(desc, frame_index) == 0U)))
    {
        return 0U;
    }

    sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    sample_cache_voice_bind(voice, sample_id, frame_index);
    voice->voice_id = voice_id;
    desc->stream_active = (desc->mode == SAMPLE_CACHE_MODE_STREAM) ? 1U : 0U;
    desc->state = SAMPLE_CACHE_PLAYING;
    return 1U;
}

uint8_t sample_cache_start_voice(uint16_t sample_id, uint8_t voice_id)
{
    return sample_cache_start_voice_at(sample_id, voice_id, 0U);
}

uint8_t sample_cache_begin_read_block(uint8_t voice_id,
                                      uint32_t max_frames,
                                      sample_cache_block_t *out_block)
{
    if (out_block == 0)
    {
        return 0U;
    }

    SAMPLE_CACHE_DIAG_INC(begin_read_block_calls);
    (void)sample_cache_inspect_voice_block(voice_id, max_frames, out_block);
    if ((out_block->status == SAMPLE_CACHE_BLOCK_OK) && (out_block->frames != 0U))
    {
        SAMPLE_CACHE_DIAG_INC(spans_returned);
        SAMPLE_CACHE_DIAG_ADD64(span_frames_total, out_block->frames);
    }
#if BRICK6_SAMPLER_DIAG_ENABLE
    {
        const uint32_t active_voices = sample_cache_diag_count_active_voices();
        if (active_voices > g_sample_cache_diag.max_active_voices)
        {
            g_sample_cache_diag.max_active_voices = active_voices;
        }
    }
#endif
    return (voice_id < SAMPLE_CACHE_MAX_VOICES) ? 1U : 0U;
}

void sample_cache_commit_read_block(uint8_t voice_id, uint32_t consumed_frames)
{
    if (voice_id >= SAMPLE_CACHE_MAX_VOICES)
    {
        return;
    }

    sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY))
    {
        return;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[voice->sample_id];
    if (((desc->mode == SAMPLE_CACHE_MODE_FULL) && (desc->cache == 0))
        || ((desc->state != SAMPLE_CACHE_READY_FULL)
            && (desc->state != SAMPLE_CACHE_READY_PARTIAL)
            && (desc->state != SAMPLE_CACHE_PLAYING)))
    {
        sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_UNDERRUN);
        return;
    }

    if (consumed_frames != 0U)
    {
        if (sample_cache_voice_uses_page_cache_path(desc, voice->sample_id) != 0U)
        {
            sample_stream_cursor_t *const cursor = &voice->cursor;
            if (sample_cache_voice_cursor_has_valid_span(voice, voice->frame_pos) == 0U)
            {
                sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_UNDERRUN);
                return;
            }

            uint32_t available_frames = cursor->current_span.frame_count - cursor->current_span.offset_frames;
            const uint32_t sample_remaining = desc->total_frames - voice->frame_pos;
            if (available_frames > sample_remaining)
            {
                available_frames = sample_remaining;
            }
            if (consumed_frames > available_frames)
            {
                consumed_frames = available_frames;
            }

            voice->frame_pos += consumed_frames;
            cursor->frame_pos = voice->frame_pos;
            cursor->current_span.offset_frames += consumed_frames;

            if (voice->frame_pos >= desc->total_frames)
            {
                sample_cache_cursor_release_current_page(cursor);
            }
            else if (cursor->current_span.offset_frames >= cursor->current_span.frame_count)
            {
                SAMPLE_CACHE_DIAG_INC(page_transitions);
                sample_cache_cursor_release_current_page(cursor);
                cursor->frame_pos = voice->frame_pos;
                if (sample_cache_voice_cursor_resolve_page(voice, desc, voice->frame_pos) == 0U)
                {
                    sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_UNDERRUN);
                    return;
                }
            }
        }
        else
        {
            SAMPLE_CACHE_DIAG_INC(slow_path_fallbacks);
            sample_cache_block_t block;
            const sample_cache_block_status_t status =
                sample_cache_inspect_voice_block(voice_id, consumed_frames, &block);
            if (status != SAMPLE_CACHE_BLOCK_OK)
            {
                sample_cache_finish_playback(desc,
                                             voice,
                                             (status == SAMPLE_CACHE_BLOCK_DONE)
                                                 ? SAMPLE_CACHE_DONE
                                                 : SAMPLE_CACHE_UNDERRUN);
                return;
            }

            if (consumed_frames > block.frames)
            {
                consumed_frames = block.frames;
            }
            voice->frame_pos += consumed_frames;
        }
    }

    if (voice->frame_pos >= desc->total_frames)
    {
        sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_DONE);
        return;
    }

    if ((sample_cache_voice_uses_page_cache_path(desc, voice->sample_id) != 0U)
        && (desc->mode == SAMPLE_CACHE_MODE_STREAM) && (desc->fully_cached == 0U))
    {
        if ((voice->cursor.current_page.acquired == 0U)
            && (sample_cache_voice_cursor_prepare(voice, desc, voice->frame_pos) == 0U))
        {
            sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_UNDERRUN);
            return;
        }
    }
    else if (sample_cache_frame_available(desc, voice->frame_pos) == 0U)
    {
        sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_UNDERRUN);
        return;
    }

    desc->state = SAMPLE_CACHE_PLAYING;
}

uint8_t sample_cache_try_acquire_span(uint16_t sample_id,
                                      uint32_t frame_index,
                                      uint32_t max_frames,
                                      sample_cache_span_t *out_span)
{
    if (out_span == 0)
    {
        return 0U;
    }

    memset(out_span, 0, sizeof(*out_span));
    if ((sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY) || (max_frames == 0U))
    {
        return 0U;
    }

    const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->state != SAMPLE_CACHE_READY_FULL)
        && (desc->state != SAMPLE_CACHE_READY_PARTIAL)
        && (desc->state != SAMPLE_CACHE_PLAYING))
    {
        return 0U;
    }

    if (frame_index >= desc->total_frames)
    {
        return 0U;
    }

    if ((((desc->fully_cached != 0U) && (desc->mode == SAMPLE_CACHE_MODE_FULL))
         || ((desc->fully_cached == 0U) && (desc->mode == SAMPLE_CACHE_MODE_STREAM)))
        && (sample_id < SAMPLE_PAGE_CACHE_MAX_SAMPLES))
    {
        sample_page_span_t page_span;
        SAMPLE_CACHE_DIAG_INC(page_cache_lookup_calls);
        if (sample_page_cache_try_acquire_page(sample_id,
                                               frame_index / SAMPLE_PAGE_FRAMES,
                                               &page_span) == 0U)
        {
            return 0U;
        }

        out_span->l = page_span.frames_interleaved;
        out_span->r = (desc->info.channels == 1U) ? 0 : (&page_span.frames_interleaved[1U]);
        out_span->frames = page_span.frame_count;
        out_span->frame_stride = 2U;
        out_span->start_frame = page_span.start_frame;
        out_span->backing_page_index = page_span.page_index;
        out_span->is_mono = (desc->info.channels == 1U) ? 1U : 0U;
        out_span->page_acquired = 1U;
        SAMPLE_CACHE_DIAG_INC(spans_returned);
        SAMPLE_CACHE_DIAG_ADD64(span_frames_total, out_span->frames);
        return 1U;
    }

    if ((desc->cache == 0) || (sample_cache_frame_available(desc, frame_index) == 0U))
    {
        return 0U;
    }

    const uint32_t cache_index = sample_cache_frame_offset(desc, frame_index);
    const uint32_t backward_available = frame_index - desc->cache_window_start_frame;
    const uint32_t backward_frames = (cache_index < backward_available) ? cache_index : backward_available;
    const uint32_t start_frame = frame_index - backward_frames;
    const uint32_t start_index = cache_index - backward_frames;
    uint32_t frames = desc->total_frames - start_frame;
    const uint32_t window_frames =
        (desc->cache_window_start_frame + desc->cache_valid_frames) - start_frame;
    const uint32_t wrap_frames = desc->cache_capacity_frames - start_index;
    if (frames > window_frames)
    {
        frames = window_frames;
    }
    if (frames > wrap_frames)
    {
        frames = wrap_frames;
    }
    if (frames == 0U)
    {
        return 0U;
    }

    out_span->l = &desc->cache[start_index * 2U];
    out_span->r = (desc->info.channels == 1U) ? 0 : (&desc->cache[(start_index * 2U) + 1U]);
    out_span->frames = frames;
    out_span->frame_stride = 2U;
    out_span->start_frame = start_frame;
    out_span->backing_page_index = 0U;
    out_span->is_mono = (desc->info.channels == 1U) ? 1U : 0U;
    out_span->page_acquired = 0U;
    SAMPLE_CACHE_DIAG_INC(spans_returned);
    SAMPLE_CACHE_DIAG_ADD64(span_frames_total, out_span->frames);
    return 1U;
}

void sample_cache_release_span(uint16_t sample_id, sample_cache_span_t *span)
{
    if (span == 0)
    {
        return;
    }

    if ((span->page_acquired != 0U) && (sample_id < SAMPLE_PAGE_CACHE_MAX_SAMPLES))
    {
        sample_page_cache_release_page(sample_id, span->backing_page_index);
    }

    memset(span, 0, sizeof(*span));
}

void sample_cache_set_voice_frame_pos(uint8_t voice_id, uint32_t frame_pos)
{
    if (voice_id >= SAMPLE_CACHE_MAX_VOICES)
    {
        return;
    }

    sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY))
    {
        return;
    }

    sample_cache_voice_seek(voice, frame_pos);
}

void sample_cache_update_voice_frame_pos(uint8_t voice_id, uint32_t frame_pos)
{
    if (voice_id >= SAMPLE_CACHE_MAX_VOICES)
    {
        return;
    }

    sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY))
    {
        return;
    }

    voice->frame_pos = frame_pos;

    sample_stream_cursor_t *const cursor = &voice->cursor;
    cursor->frame_pos = frame_pos;

    if ((cursor->valid == 0U) || (cursor->current_page.valid == 0U)
        || (cursor->current_page.acquired == 0U) || (cursor->current_span.valid == 0U)
        || (cursor->current_span.base == 0))
    {
        return;
    }

    if ((frame_pos >= cursor->current_span.start_frame)
        && (frame_pos < (cursor->current_span.start_frame + cursor->current_span.frame_count)))
    {
        cursor->current_span.offset_frames = frame_pos - cursor->current_span.start_frame;
    }
}

void sample_cache_set_voice_direction(uint8_t voice_id, int8_t direction)
{
    if (voice_id >= SAMPLE_CACHE_MAX_VOICES)
    {
        return;
    }

    sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY))
    {
        return;
    }

    voice->direction = (direction < 0) ? -1 : 1;
}

uint32_t sample_cache_read_voice(uint8_t voice_id, float *out_l, float *out_r, uint32_t frames)
{
    if ((voice_id >= SAMPLE_CACHE_MAX_VOICES) || (out_l == 0) || (out_r == 0) || (frames == 0U))
    {
        return 0U;
    }

    uint32_t produced = 0U;
    while (produced < frames)
    {
        sample_cache_block_t block;
        (void)sample_cache_begin_read_block(voice_id, frames - produced, &block);
        if ((block.status != SAMPLE_CACHE_BLOCK_OK) || (block.frames == 0U))
        {
            sample_cache_commit_read_block(voice_id, 0U);
            break;
        }

        const float *src_l = block.l;
        const float *src_r = (block.is_mono != 0U) ? block.l : block.r;
        for (uint32_t i = 0U; i < block.frames; ++i)
        {
            const float sample_l = src_l[i * block.frame_stride];
            const float sample_r = src_r[i * block.frame_stride];
            out_l[produced + i] += sample_l;
            out_r[produced + i] += sample_r;
        }

        sample_cache_commit_read_block(voice_id, block.frames);
        produced += block.frames;
    }

    return produced;
}

uint8_t sample_cache_read_voice_frame(uint8_t voice_id, uint32_t frame_index, float *out_l, float *out_r)
{
    if ((voice_id >= SAMPLE_CACHE_MAX_VOICES) || (out_l == 0) || (out_r == 0))
    {
        return 0U;
    }

    sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY))
    {
        return 0U;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[voice->sample_id];
    if (((desc->mode == SAMPLE_CACHE_MODE_FULL) && (desc->cache == 0))
        || (desc->cache_valid_frames == 0U)
        || ((desc->state != SAMPLE_CACHE_READY_FULL)
            && (desc->state != SAMPLE_CACHE_READY_PARTIAL)
            && (desc->state != SAMPLE_CACHE_PLAYING)))
    {
        voice->active = 0U;
        desc->state = SAMPLE_CACHE_UNDERRUN;
        return 0U;
    }

    if (frame_index >= desc->total_frames)
    {
        sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_DONE);
        return 0U;
    }

    if ((desc->mode == SAMPLE_CACHE_MODE_STREAM) && (desc->fully_cached == 0U))
    {
        sample_page_block_t page_block;
        SAMPLE_CACHE_DIAG_INC(page_cache_lookup_calls);
        (void)sample_page_cache_begin_read_block(voice->sample_id, frame_index, 1U, &page_block);
        if ((page_block.status != SAMPLE_PAGE_BLOCK_OK) || (page_block.frame_count == 0U))
        {
            sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_UNDERRUN);
            return 0U;
        }

        *out_l = page_block.frames_interleaved[0];
        *out_r = (desc->info.channels == 1U) ? page_block.frames_interleaved[0]
                                             : page_block.frames_interleaved[1U];
        sample_page_cache_commit_read_block(voice->sample_id, frame_index / SAMPLE_PAGE_FRAMES);
        voice->frame_pos = frame_index + 1U;
        return 1U;
    }

    if (sample_cache_frame_available(desc, frame_index) == 0U)
    {
        sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_UNDERRUN);
        return 0U;
    }

    const uint32_t cache_index = sample_cache_frame_offset(desc, frame_index);
    *out_l = desc->cache[cache_index * 2U];
    *out_r = desc->cache[cache_index * 2U + 1U];
    voice->frame_pos = frame_index + 1U;
    return 1U;
}

uint8_t sample_cache_peek_frame(uint16_t sample_id, uint32_t frame_index, float *out_l, float *out_r)
{
    SAMPLE_CACHE_DIAG_INC(peek_frame_calls);
    if ((sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY) || (out_l == 0) || (out_r == 0))
    {
        return 0U;
    }

    const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->state != SAMPLE_CACHE_READY_FULL)
        && (desc->state != SAMPLE_CACHE_READY_PARTIAL)
        && (desc->state != SAMPLE_CACHE_PLAYING))
    {
        return 0U;
    }

    if (frame_index >= desc->total_frames)
    {
        return 0U;
    }

    if ((desc->mode == SAMPLE_CACHE_MODE_STREAM) && (desc->fully_cached == 0U))
    {
        sample_page_block_t page_block;
        SAMPLE_CACHE_DIAG_INC(page_cache_lookup_calls);
        (void)sample_page_cache_begin_read_block(sample_id, frame_index, 1U, &page_block);
        if ((page_block.status != SAMPLE_PAGE_BLOCK_OK) || (page_block.frame_count == 0U))
        {
            return 0U;
        }

        *out_l = page_block.frames_interleaved[0];
        *out_r = (desc->info.channels == 1U) ? page_block.frames_interleaved[0]
                                             : page_block.frames_interleaved[1U];
        sample_page_cache_commit_read_block(sample_id, frame_index / SAMPLE_PAGE_FRAMES);
        return 1U;
    }

    if ((desc->cache == 0) || (sample_cache_frame_available(desc, frame_index) == 0U))
    {
        return 0U;
    }

    const uint32_t cache_index = sample_cache_frame_offset(desc, frame_index);
    *out_l = desc->cache[cache_index * 2U];
    *out_r = desc->cache[cache_index * 2U + 1U];
    return 1U;
}

const float *sample_cache_get_legacy_data(uint16_t sample_id, uint32_t *out_frames)
{
    if (out_frames != 0)
    {
        *out_frames = 0U;
    }

    if (sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
    {
        return 0;
    }

    const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->fully_cached == 0U) || (desc->cache == 0) || (desc->cache_valid_frames < desc->total_frames))
    {
        return 0;
    }

    if (out_frames != 0)
    {
        *out_frames = desc->total_frames;
    }
    return desc->cache;
}

void sample_cache_stop_voice(uint8_t voice_id)
{
    if (voice_id >= SAMPLE_CACHE_MAX_VOICES)
    {
        return;
    }

    sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    if ((voice->active != 0U) && (voice->sample_id < SAMPLE_CACHE_HOT_SAMPLE_CAPACITY))
    {
        sample_cache_desc_t *const desc = &g_sample_cache[voice->sample_id];
        const uint16_t sample_id = voice->sample_id;
        sample_cache_voice_release(voice);
        desc->stream_active = sample_cache_any_voice_active(sample_id);
        if ((desc->state == SAMPLE_CACHE_PLAYING) && (desc->stream_active == 0U))
        {
            if (desc->fully_cached != 0U)
            {
                desc->state = SAMPLE_CACHE_READY_FULL;
            }
            else
            {
                desc->state = (sample_cache_stream_boundary_pages_ready(sample_id, desc) != 0U)
                                  ? SAMPLE_CACHE_READY_PARTIAL
                                  : SAMPLE_CACHE_NEEDS_REPREPARE;
                desc->reprepare_start_frame = 0U;
            }
        }
        return;
    }

    sample_cache_voice_release(voice);
}
