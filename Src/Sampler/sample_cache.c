#include "Sampler/sample_cache.h"

#include <ctype.h>
#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "Storage/wav_audio_codec.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_stream_underrun_trace.h"
#include "Sampler/sample_stream_needs.h"
#include "Sampler/sample_stream_snapshot.h"
#include "Sampler/sample_voice_reader.h"
#include "ff.h"

#define SAMPLE_CACHE_MAX_VOICES (16U)
#define SAMPLE_CACHE_IO_BYTES (4096U)
#define SAMPLE_CACHE_STREAM_START_PAGES SAMPLE_PAGE_CLASSIC_FORWARD_WINDOW_PAGES
#define SAMPLE_CACHE_STREAM_TAIL_PAGES SAMPLE_PAGE_CLASSIC_REVERSE_WINDOW_PAGES
#define SAMPLE_CACHE_STREAM_FORWARD_LOOKAHEAD_PAGES SAMPLE_PAGE_CLASSIC_FORWARD_LOOKAHEAD_PAGES
#define SAMPLE_CACHE_STREAM_REVERSE_LOOKAHEAD_PAGES SAMPLE_PAGE_CLASSIC_REVERSE_LOOKAHEAD_PAGES
#define SAMPLE_CACHE_STREAM_STATIC_PAGES SAMPLE_CACHE_STREAM_START_PAGES
#define SAMPLE_CACHE_FULL_MAX_BYTES (SAMPLE_CACHE_STREAM_STATIC_PAGES * SAMPLE_PAGE_BYTES)

SDRAM_CLASSIC_POOL static sample_cache_desc_t g_sample_cache[SAMPLE_CACHE_HOT_SAMPLE_CAPACITY];
static AUDIO_HOT sample_cache_voice_t g_sample_cache_voice[SAMPLE_CACHE_MAX_VOICES];
static AUDIO_HOT sample_voice_reader_t g_sample_cache_readers[SAMPLE_CACHE_MAX_VOICES];
static AUDIO_WARM uint8_t g_sample_cache_io_storage[SAMPLE_CACHE_IO_BYTES + 1U];
static CTRL_STATE FRESULT g_sample_cache_last_fresult[SAMPLE_CACHE_HOT_SAMPLE_CAPACITY];
static CTRL_STATE uint32_t g_sample_cache_voice_generation_counter;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_CACHE_HOT_SAMPLE_CAPACITY <= SAMPLE_PAGE_CACHE_ID_CAPACITY,
               "hot sample cache ids must fit in the page-cache id space");
_Static_assert(SAMPLE_POOL_PROJECT_CAPACITY >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY,
               "project sample capacity must cover the hot cache capacity");
_Static_assert(SAMPLE_CACHE_FULL_MAX_BYTES == (SAMPLE_CACHE_STREAM_STATIC_PAGES * SAMPLE_PAGE_BYTES),
               "FULL byte threshold must match the fixed page budget");
#endif

static uint8_t sample_cache_open_source(uint16_t sample_id, FIL *fp);
static uint8_t sample_cache_try_prepare_full_via_page_cache(uint16_t sample_id,
                                                            sample_cache_desc_t *desc,
                                                            FIL *fp);
static uint8_t sample_cache_prepare_partial_via_page_cache(uint16_t sample_id,
                                                           sample_cache_desc_t *desc);
static uint8_t sample_cache_request_pin_page_span(uint16_t sample_id,
                                                  const sample_play_plan_page_span_t *span);
static void sample_cache_voice_reset(sample_cache_voice_t *voice);
static void sample_cache_voice_release(sample_cache_voice_t *voice);
static void sample_cache_voice_bind(sample_cache_voice_t *voice,
                                    uint16_t sample_id,
                                    uint32_t frame_index);
static uint8_t sample_cache_voice_bind_reader(sample_cache_voice_t *voice,
                                              const sample_cache_desc_t *desc);
static uint8_t sample_cache_voice_reserve_start_window(const sample_cache_voice_t *voice,
                                                       const sample_cache_desc_t *desc);
static void sample_cache_release_pending_stream_needs(void);
static void sample_cache_voice_seek(sample_cache_voice_t *voice, uint32_t frame_pos);
static uint8_t sample_cache_voice_uses_page_cache_path(const sample_cache_desc_t *desc,
                                                       uint16_t sample_id);
static uint8_t sample_cache_publish_stream_snapshot(const sample_cache_voice_t *voice,
                                                     const sample_cache_desc_t *desc,
                                                     sample_stream_snapshot_t *out_snapshot);
static uint8_t sample_cache_update_stream_needs(const sample_cache_voice_t *voice,
                                                const sample_stream_snapshot_t *snapshot);
static uint8_t sample_cache_build_voice_plan(const sample_cache_voice_t *voice,
                                             const sample_cache_desc_t *desc,
                                             sample_play_plan_t *out_plan);
static void sample_cache_invalidate_voices_for_sample(uint16_t sample_id);
static void sample_cache_update_active_stream_needs(void);
static uint32_t sample_cache_stream_last_page_index(const sample_cache_desc_t *desc);
static uint8_t sample_cache_stream_boundary_pages_ready(uint16_t sample_id,
                                                        const sample_cache_desc_t *desc);
static uint8_t sample_cache_stream_start_base_ready(uint16_t sample_id,
                                                    const sample_cache_desc_t *desc);
static uint8_t sample_cache_stream_window_ready(uint16_t sample_id,
                                                const sample_cache_desc_t *desc,
                                                uint32_t frame_index,
                                                int8_t direction);

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
    voice->stream_release_pending = 0U;
    if (voice->voice_id < SAMPLE_CACHE_MAX_VOICES)
    {
        sample_voice_reader_reset(&g_sample_cache_readers[voice->voice_id]);
    }
}

static void sample_cache_voice_release(sample_cache_voice_t *voice)
{
    if (voice == 0)
    {
        return;
    }

    if ((voice->active != 0U) || (voice->stream_release_pending != 0U))
    {
        (void)sample_stream_needs_registry_drop_generation(
            SAMPLE_STREAM_SNAPSHOT_CLASSIC, voice->voice_id, voice->generation);
    }
    sample_stream_snapshot_clear(SAMPLE_STREAM_SNAPSHOT_CLASSIC, voice->voice_id);
    sample_stream_needs_registry_drop(SAMPLE_STREAM_SNAPSHOT_CLASSIC, voice->voice_id);
    if (voice->voice_id < SAMPLE_CACHE_MAX_VOICES)
    {
        sample_voice_reader_stop(&g_sample_cache_readers[voice->voice_id]);
    }
    voice->active = 0U;
    voice->sample_id = UINT16_MAX;
    voice->frame_pos = 0U;
    voice->direction = 1;
    voice->stream_release_pending = 0U;
}

static void sample_cache_voice_bind(sample_cache_voice_t *voice,
                                    uint16_t sample_id,
                                    uint32_t frame_index)
{
    if (voice == 0)
    {
        return;
    }

    voice->sample_id = sample_id;
    voice->frame_pos = frame_index;
    voice->direction = 1;
    voice->active = 1U;
    voice->stop_on_underrun = 1U;
    voice->stream_release_pending = 0U;
    voice->generation = ++g_sample_cache_voice_generation_counter;
}

static uint8_t sample_cache_build_voice_plan(const sample_cache_voice_t *voice,
                                             const sample_cache_desc_t *desc,
                                             sample_play_plan_t *out_plan)
{
    if ((voice == 0) || (desc == 0) || (out_plan == 0)
        || (desc->total_frames == 0U))
    {
        return 0U;
    }

    sample_play_plan_init(out_plan);
    out_plan->key = sample_audio_key_classic(voice->sample_id);
    out_plan->sample_id = voice->sample_id;
    out_plan->format = desc->format;
    out_plan->stride_floats = desc->stride_floats;
    out_plan->frames_per_page = desc->frames_per_page;
    out_plan->registration_epoch = desc->registration_epoch;
    out_plan->start_frame = voice->frame_pos;
    out_plan->region_begin = 0U;
    out_plan->region_end = desc->total_frames;
    out_plan->loop_begin = 0U;
    out_plan->loop_end = desc->total_frames;
    out_plan->step_q16 = SAMPLE_STREAM_STEP_Q16_ONE;
    out_plan->direction = (voice->direction < 0) ? 1U : 0U;
    out_plan->loop_mode = SAMPLE_PLAY_LOOP_NONE;
    out_plan->stop_on_underrun = voice->stop_on_underrun;
    out_plan->kernel_type = (out_plan->direction != 0U)
                                ? SAMPLE_KERNEL_REV_1X
                                : SAMPLE_KERNEL_FWD_1X;
    return sample_play_plan_is_valid(out_plan);
}

static uint8_t sample_cache_voice_bind_reader(sample_cache_voice_t *voice,
                                              const sample_cache_desc_t *desc)
{
    if ((voice == 0) || (desc == 0) || (voice->voice_id >= SAMPLE_CACHE_MAX_VOICES))
    {
        return 0U;
    }

    sample_play_plan_t plan;
    if (sample_cache_build_voice_plan(voice, desc, &plan) == 0U)
    {
        return 0U;
    }

    return sample_voice_reader_bind_play_plan(&g_sample_cache_readers[voice->voice_id],
                                              &plan,
                                              UINT8_MAX);
}

static uint8_t sample_cache_voice_reserve_start_window(const sample_cache_voice_t *voice,
                                                       const sample_cache_desc_t *desc)
{
    if ((voice == 0) || (desc == 0) || (voice->active == 0U)
        || (voice->sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY))
    {
        return 0U;
    }

    if ((desc->mode != SAMPLE_CACHE_MODE_STREAM) || (desc->fully_cached != 0U))
    {
        return 1U;
    }

    sample_stream_snapshot_t snapshot;
    if (sample_cache_publish_stream_snapshot(voice, desc, &snapshot) == 0U)
    {
        return 0U;
    }

    return sample_cache_update_stream_needs(voice, &snapshot);
}

static uint8_t sample_cache_publish_stream_snapshot(const sample_cache_voice_t *voice,
                                                     const sample_cache_desc_t *desc,
                                                     sample_stream_snapshot_t *out_snapshot)
{
    if ((voice == 0) || (desc == 0) || (out_snapshot == 0)
        || (voice->direction != 1)
        || (voice->voice_id >= SAMPLE_STREAM_SNAPSHOT_CLASSIC_CAPACITY))
    {
        return 0U;
    }

    sample_stream_snapshot_init(out_snapshot);
    out_snapshot->key = sample_audio_key_classic(voice->sample_id);
    out_snapshot->format = desc->format;
    out_snapshot->stride_floats = desc->stride_floats;
    out_snapshot->frames_per_page = desc->frames_per_page;
    out_snapshot->registration_epoch = desc->registration_epoch;
    out_snapshot->generation = voice->generation;
    out_snapshot->current_frame = voice->frame_pos;
    out_snapshot->region_begin = 0U;
    out_snapshot->region_end = desc->total_frames;
    out_snapshot->loop_begin = 0U;
    out_snapshot->loop_end = desc->total_frames;
    out_snapshot->step_q16 = SAMPLE_STREAM_STEP_Q16_ONE;
    out_snapshot->direction = (voice->direction < 0) ? -1 : 1;
    out_snapshot->active = voice->active;
    out_snapshot->loop_enabled = 0U;
    if (sample_stream_snapshot_publish(SAMPLE_STREAM_SNAPSHOT_CLASSIC,
                                       voice->voice_id,
                                       out_snapshot) == 0U)
    {
        return 0U;
    }
    return sample_stream_snapshot_read(SAMPLE_STREAM_SNAPSHOT_CLASSIC,
                                       voice->voice_id,
                                       out_snapshot);
}

static uint8_t sample_cache_update_stream_needs(const sample_cache_voice_t *voice,
                                                const sample_stream_snapshot_t *snapshot)
{
    if ((voice == 0) || (snapshot == 0))
    {
        return 0U;
    }
    return sample_stream_needs_registry_update(
        SAMPLE_STREAM_SNAPSHOT_CLASSIC,
        voice->voice_id,
        snapshot,
        sample_stream_time_now());
}

static void sample_cache_voice_seek(sample_cache_voice_t *voice, uint32_t frame_pos)
{
    if (voice == 0)
    {
        return;
    }

    voice->frame_pos = frame_pos;
    if ((voice->voice_id < SAMPLE_CACHE_MAX_VOICES) && (voice->active != 0U))
    {
        sample_voice_reader_t *const reader = &g_sample_cache_readers[voice->voice_id];
        if (reader->active != 0U)
        {
            sample_voice_reader_seek(reader, frame_pos);
        }
        else if (voice->sample_id < SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
        {
            (void)sample_cache_voice_bind_reader(voice, &g_sample_cache[voice->sample_id]);
        }
    }
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
        sample_page_cache_load_full_sample_key_alloc(sample_audio_key_classic(sample_id),
                                                     fp,
                                                     &desc->info,
                                                     desc->total_frames,
                                                     desc->data_offset,
                                                     sample_cache_io_buffer(),
                                                     SAMPLE_CACHE_IO_BYTES,
                                                     SAMPLE_PAGE_ALLOC_SLOT_PERMANENT);
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
    desc->cache_capacity_frames = sample_audio_format_required_page_count(
                                      desc->format, desc->total_frames)
                                  * desc->frames_per_page;
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

    sample_page_stream_info_t stream_info;
    if (sample_page_cache_get_stream_info(sample_id, &stream_info) != 0U)
    {
        desc->format = stream_info.format;
        desc->stride_floats = stream_info.stride_floats;
        desc->frames_per_page = stream_info.frames_per_page;
        desc->registration_epoch = stream_info.registration_epoch;
    }

    sample_play_plan_t forward_plan;
    sample_play_plan_init(&forward_plan);
    forward_plan.key = sample_audio_key_classic(sample_id);
    forward_plan.sample_id = sample_id;
    forward_plan.format = desc->format;
    forward_plan.stride_floats = desc->stride_floats;
    forward_plan.frames_per_page = desc->frames_per_page;
    forward_plan.start_frame = 0U;
    forward_plan.region_begin = 0U;
    forward_plan.region_end = desc->total_frames;
    forward_plan.min_ready_frames = SAMPLE_PREP_MIN_READY_FRAMES;
    sample_play_plan_page_span_t forward_span;
    if (sample_play_plan_frames_to_page_span(&forward_plan,
                                             SAMPLE_PREP_MIN_READY_FRAMES,
                                             &forward_span) == 0U)
    {
        desc->last_error = 8U;
        g_sample_cache_last_fresult[sample_id] = FR_NOT_ENOUGH_CORE;
        return 0U;
    }
    if (sample_cache_request_pin_page_span(sample_id, &forward_span) == 0U)
    {
        desc->last_error = 8U;
        g_sample_cache_last_fresult[sample_id] = FR_NOT_ENOUGH_CORE;
        return 0U;
    }
    if (sample_page_cache_get_page_state(sample_id, 0U) != SAMPLE_PAGE_READY)
    {
    }

    /*
     * Long STREAM cold base follows the product minimum-ready contract on both
     * entry sides. The reverse side uses the shared play-plan frame->page span
     * helper because an unaligned tail start can require one more physical page
     * than the format-specific minimum-ready frame count divided by the
     * format-specific page geometry.
     */
    sample_play_plan_t reverse_plan;
    sample_play_plan_init(&reverse_plan);
    reverse_plan.key = sample_audio_key_classic(sample_id);
    reverse_plan.sample_id = sample_id;
    reverse_plan.format = desc->format;
    reverse_plan.stride_floats = desc->stride_floats;
    reverse_plan.frames_per_page = desc->frames_per_page;
    reverse_plan.start_frame = desc->total_frames - 1U;
    reverse_plan.region_begin = 0U;
    reverse_plan.region_end = desc->total_frames;
    reverse_plan.direction = 1U;
    reverse_plan.min_ready_frames = SAMPLE_PREP_MIN_READY_FRAMES;
    sample_play_plan_page_span_t reverse_span;
    if ((sample_play_plan_frames_to_page_span(&reverse_plan,
                                              SAMPLE_PREP_MIN_READY_FRAMES,
                                              &reverse_span) != 0U)
        && (reverse_span.page_count != 0U))
    {
        if (sample_cache_request_pin_page_span(sample_id, &reverse_span) == 0U)
        {
            desc->last_error = 8U;
            g_sample_cache_last_fresult[sample_id] = FR_NOT_ENOUGH_CORE;
            return 0U;
        }
    }

    desc->cache = 0;
    desc->cache_window_start_frame = 0U;
    desc->cache_valid_frames = desc->frames_per_page
                               * sample_audio_format_presocle_pages(desc->format);
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

static uint8_t sample_cache_request_pin_page_span(uint16_t sample_id,
                                                  const sample_play_plan_page_span_t *span)
{
    if ((span == 0) || (span->valid == 0U) || (span->page_count == 0U)
        || (span->page_start > span->page_end))
    {
        return 0U;
    }

    for (uint32_t page_index = span->page_start; page_index <= span->page_end; ++page_index)
    {
        if (sample_page_cache_reserve_page_key_alloc(
                sample_audio_key_classic(sample_id),
                page_index,
                SAMPLE_PAGE_ALLOC_MARGIN) == 0U)
        {
            return 0U;
        }
        if (sample_page_cache_pin_page_key_alloc(sample_audio_key_classic(sample_id),
                                                 page_index,
                                                 SAMPLE_PAGE_ALLOC_MARGIN) == 0U)
        {
            return 0U;
        }
    }

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
    out_block->format = SAMPLE_AUDIO_FORMAT_INVALID;
    out_block->frames_per_page = 0U;
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
        if (g_sample_cache_readers[voice_id].active == 0U)
        {
            if (sample_cache_voice_bind_reader(&g_sample_cache_voice[voice_id], desc) == 0U)
            {
                out_block->status = (desc->mode == SAMPLE_CACHE_MODE_STREAM)
                                        ? SAMPLE_CACHE_BLOCK_UNDERRUN
                                        : SAMPLE_CACHE_BLOCK_NOT_READY;
                return out_block->status;
            }
        }
        return (sample_voice_reader_begin_block(&g_sample_cache_readers[voice_id],
                                                max_frames,
                                                out_block) != 0U)
                   ? out_block->status
                   : SAMPLE_CACHE_BLOCK_NOT_READY;
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

    out_block->l = &desc->cache[cache_index * desc->stride_floats];
    out_block->r = (desc->format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
                       ? 0
                       : (&desc->cache[(cache_index * desc->stride_floats) + 1U]);
    out_block->frames = frames;
    out_block->frame_stride = desc->stride_floats;
    out_block->frame_step = (int32_t)desc->stride_floats;
    out_block->format = desc->format;
    out_block->frames_per_page = desc->frames_per_page;
    out_block->is_mono = (desc->format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO) ? 1U : 0U;
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

    return sample_audio_format_page_index_from_frame(desc->format, desc->total_frames - 1U);
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

static uint8_t sample_cache_stream_start_base_ready(uint16_t sample_id,
                                                    const sample_cache_desc_t *desc)
{
    if ((sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY) || (desc == 0)
        || (desc->total_frames == 0U))
    {
        return 0U;
    }

    if ((desc->mode != SAMPLE_CACHE_MODE_STREAM) || (desc->fully_cached != 0U))
    {
        return sample_cache_start_frame_available(sample_id);
    }

    const uint32_t last_page = sample_cache_stream_last_page_index(desc);
    uint32_t required_pages = sample_audio_format_presocle_pages(desc->format);
    if (required_pages > (last_page + 1U))
    {
        required_pages = last_page + 1U;
    }

    for (uint32_t page = 0U; page < required_pages; ++page)
    {
        if (sample_page_cache_get_page_state(sample_id, page) != SAMPLE_PAGE_READY)
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t sample_cache_stream_window_ready(uint16_t sample_id,
                                                const sample_cache_desc_t *desc,
                                                uint32_t frame_index,
                                                int8_t direction)
{
    if ((sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY) || (desc == 0)
        || (desc->total_frames == 0U) || (frame_index >= desc->total_frames))
    {
        return 0U;
    }
    if ((desc->mode != SAMPLE_CACHE_MODE_STREAM) || (desc->fully_cached != 0U))
    {
        return sample_cache_frame_available(desc, frame_index);
    }

    const uint32_t current_page = sample_audio_format_page_index_from_frame(desc->format,
                                                                             frame_index);
    const uint32_t last_page = sample_cache_stream_last_page_index(desc);
    const uint32_t pages = sample_audio_format_window_pages(desc->format);
    for (uint32_t ahead = 0U; ahead < pages; ++ahead)
    {
        uint32_t page = current_page;
        if (direction < 0)
        {
            if (current_page < ahead)
            {
                break;
            }
            page = current_page - ahead;
        }
        else
        {
            page = current_page + ahead;
            if (page > last_page)
            {
                break;
            }
        }
        if (sample_page_cache_get_page_state(sample_id, page) != SAMPLE_PAGE_READY)
        {
            return 0U;
        }
    }
    return 1U;
}

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

static void sample_cache_update_active_stream_needs(void)
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

        sample_stream_snapshot_t snapshot;
        if (sample_cache_publish_stream_snapshot(voice, desc, &snapshot) == 0U)
        {
            continue;
        }
        (void)sample_cache_update_stream_needs(voice, &snapshot);
    }
}

static void sample_cache_release_pending_stream_needs(void)
{
    for (uint32_t i = 0U; i < SAMPLE_CACHE_MAX_VOICES; ++i)
    {
        sample_cache_voice_t *const voice = &g_sample_cache_voice[i];
        if ((voice->active == 0U) && (voice->stream_release_pending != 0U))
        {
            sample_cache_voice_release(voice);
        }
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

    const sample_audio_key_t cache_key = sample_audio_key_classic(sample_id);
    const sample_audio_format_t cache_format = sample_audio_format_or_stereo(desc->format);
    const uint32_t first_page = sample_audio_format_page_index_from_frame(cache_format,
                                                                            start_frame);
    uint8_t pages_reserved = 1U;
    for (uint32_t i = 0U;
         i < sample_audio_format_presocle_pages(desc->format);
         ++i)
    {
        if (sample_page_cache_reserve_page_key_alloc(cache_key,
                                                     first_page + i,
                                                     SAMPLE_PAGE_ALLOC_MARGIN) == 0U)
        {
            pages_reserved = 0U;
            break;
        }
    }
    if (pages_reserved == 0U)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 8U;
        return 0U;
    }
    if (sample_page_cache_pin_page_key_alloc(sample_audio_key_classic(sample_id),
                                                      sample_audio_format_page_index_from_frame(
                                                          desc->format, start_frame),
                                             SAMPLE_PAGE_ALLOC_MARGIN) == 0U)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 8U;
        return 0U;
    }

    sample_stream_manager_service(byte_budget);
    if (sample_page_cache_get_page_state(
            sample_id,
            sample_audio_format_page_index_from_frame(desc->format, start_frame))
        != SAMPLE_PAGE_READY)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 12U;
        return 0U;
    }

    desc->cache_valid_frames = desc->frames_per_page
                               * sample_audio_format_presocle_pages(desc->format);
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
    g_sample_cache_voice_generation_counter = 0U;

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

    g_sample_cache_last_fresult[sample_id] = FR_OK;

    char prepared_path[SAMPLE_POOL_PATH_MAX];
    if (sample_cache_trim_path_copy(prepared_path, sizeof(prepared_path), path) == 0U)
    {
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_NAME;
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        g_sample_cache_last_fresult[sample_id] = FR_TIMEOUT;
        return 0U;
    }

    uint8_t preflight_ok = 0U;
    if (sd_access_fs_mount_if_needed() != 0U)
    {
        FIL preflight_fp;
        const FRESULT preflight_fr = f_open(&preflight_fp, prepared_path, FA_READ);
        if (preflight_fr == FR_OK)
        {
            (void)f_close(&preflight_fp);
            preflight_ok = 1U;
        }
        else
        {
            g_sample_cache_last_fresult[sample_id] = preflight_fr;
        }
    }
    else
    {
        g_sample_cache_last_fresult[sample_id] = FR_DISK_ERR;
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
    if (preflight_ok == 0U)
    {
        return 0U;
    }

    sample_cache_release_slot(sample_id);
    sample_cache_clear_desc(&g_sample_cache[sample_id]);

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    desc->sample_id = sample_id;
    desc->state = SAMPLE_CACHE_PREPARING;

    memcpy(desc->path, prepared_path, strlen(prepared_path) + 1U);

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
    desc->format = sample_audio_format_from_channels(desc->info.channels);
    desc->stride_floats = (uint16_t)sample_audio_format_stride_floats(desc->format);
    desc->frames_per_page = sample_audio_format_frames_per_page(desc->format);
    if (sample_audio_format_is_valid(desc->format) == 0U)
    {
        desc->last_error = 6U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
        goto done;
    }
    desc->cache_capacity_frames =
        SAMPLE_CACHE_FULL_MAX_BYTES / sample_audio_format_bytes_per_float_frame(desc->format);
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
    sample_cache_release_pending_stream_needs();
    sample_cache_update_active_stream_needs();

    if (byte_budget == 0U)
    {
        return;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_STREAM) == 0U)
    {
        brick6_stream_underrun_trace_service_blocked(
            BRICK6_STREAM_TRACE_REASON_GATE_BLOCKED,
            (uint8_t)sd_access_gate_current_owner(),
            0U);
        return;
    }

    sample_stream_manager_service(byte_budget);
    if (sample_stream_manager_has_pending_sd_work() != 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_STREAM);
        return;
    }
    if (sd_access_gate_streaming_critical_active() != 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_STREAM);
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

    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_STREAM);
}

uint8_t sample_cache_has_pending_sd_work(void)
{
    sample_cache_update_active_stream_needs();
    if (sample_stream_manager_has_pending_sd_work() != 0U)
    {
        return 1U;
    }

    return (sample_cache_pick_refill_candidate() < SAMPLE_CACHE_HOT_SAMPLE_CAPACITY) ? 1U : 0U;
}

uint8_t sample_cache_is_ready(uint16_t sample_id)
{
    return (sample_cache_get_slot_readiness(sample_id) == SAMPLE_CACHE_SLOT_PLAYABLE) ? 1U : 0U;
}

sample_cache_slot_readiness_t sample_cache_get_slot_readiness(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY)
    {
        return SAMPLE_CACHE_SLOT_ERROR;
    }

    const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    switch (desc->state)
    {
        case SAMPLE_CACHE_EMPTY:
            return SAMPLE_CACHE_SLOT_EMPTY;

        case SAMPLE_CACHE_PREPARING:
        case SAMPLE_CACHE_PREFILLING:
            return SAMPLE_CACHE_SLOT_PREPARING;

        case SAMPLE_CACHE_READY_FULL:
            return SAMPLE_CACHE_SLOT_PLAYABLE;

        case SAMPLE_CACHE_READY_PARTIAL:
        case SAMPLE_CACHE_PLAYING:
            if (sample_cache_stream_start_base_ready(sample_id, desc) == 0U)
            {
                return SAMPLE_CACHE_SLOT_START_PENDING;
            }
            return SAMPLE_CACHE_SLOT_PLAYABLE;

        case SAMPLE_CACHE_DONE:
        case SAMPLE_CACHE_NEEDS_REPREPARE:
        case SAMPLE_CACHE_UNDERRUN:
            return SAMPLE_CACHE_SLOT_NEEDS_REPREPARE;

        case SAMPLE_CACHE_ERROR:
        default:
            return SAMPLE_CACHE_SLOT_ERROR;
    }
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

uint8_t sample_cache_resolve_classic_source(uint16_t sample_id,
                                            sample_resolved_source_t *out_source)
{
    if (out_source != 0)
    {
        sample_resolved_source_init(out_source);
    }
    if ((sample_id >= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY) || (out_source == 0))
    {
        return 0U;
    }

    const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->sample_id != sample_id)
        || (desc->total_frames == 0U)
        || (desc->state == SAMPLE_CACHE_EMPTY)
        || (desc->state == SAMPLE_CACHE_ERROR))
    {
        return 0U;
    }

    out_source->key = sample_audio_key_classic(sample_id);
    out_source->path = desc->path;
    out_source->total_frames = desc->total_frames;
    out_source->data_offset = desc->data_offset;
    out_source->data_size = desc->info.data_size;
    out_source->sample_rate = desc->info.sample_rate;
    out_source->channels = desc->info.channels;
    out_source->format = desc->format;
    out_source->stride_floats = desc->stride_floats;
    out_source->frames_per_page = desc->frames_per_page;
    out_source->registration_epoch = desc->registration_epoch;
    out_source->bits_per_sample = desc->info.bits_per_sample;
    out_source->block_align = desc->info.block_align;
    out_source->root_note = 60U;
    out_source->fine_tune_cents = 0;
    out_source->region_begin = 0U;
    out_source->region_end = desc->total_frames;
    out_source->loop_begin = 0U;
    out_source->loop_end = desc->total_frames;
    out_source->loop_mode = SAMPLE_PLAY_LOOP_NONE;
    out_source->reverse = 0U;
    out_source->raw_pcm24 = 0U;
    out_source->rate = 1.0f;
    out_source->gain = 1.0f;
    out_source->owner_track_id = UINT8_MAX;
    out_source->note = 60U;
    out_source->velocity = 127U;
    out_source->source_kind = 0U;
    out_source->instrument_id = UINT16_MAX;
    out_source->zone_id = UINT16_MAX;
    return sample_resolved_source_is_valid(out_source);
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
         && (sample_page_cache_get_page_state(
                 sample_id,
                 sample_audio_format_page_index_from_frame(desc->format, frame_index))
             != SAMPLE_PAGE_READY))
        || ((desc->mode != SAMPLE_CACHE_MODE_STREAM) && (sample_cache_frame_available(desc, frame_index) == 0U)))
    {
        return 0U;
    }

    sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    if ((voice->active != 0U) || (voice->stream_release_pending != 0U))
    {
        sample_cache_voice_release(voice);
    }
    sample_cache_voice_bind(voice, sample_id, frame_index);
    voice->voice_id = voice_id;
    if (sample_cache_voice_reserve_start_window(voice, desc) == 0U)
    {
        sample_cache_voice_release(voice);
        return 0U;
    }
    if (sample_cache_stream_window_ready(sample_id,
                                         desc,
                                         frame_index,
                                         voice->direction) == 0U)
    {
        sample_cache_voice_release(voice);
        return 0U;
    }
    if (sample_cache_voice_bind_reader(voice, desc) == 0U)
    {
        sample_cache_voice_release(voice);
        return 0U;
    }
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
    (void)sample_cache_inspect_voice_block(voice_id, max_frames, out_block);
    if ((out_block->status == SAMPLE_CACHE_BLOCK_OK) && (out_block->frames != 0U))
    {
    }
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

    sample_voice_reader_t *const reader = &g_sample_cache_readers[voice_id];
    if (reader->active == 0U)
    {
        sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_DONE);
        return;
    }

    sample_voice_reader_commit_block(reader, consumed_frames);
    voice->frame_pos = reader->frame_pos;
    if (reader->active == 0U)
    {
        sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_DONE);
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
        if (sample_page_cache_try_acquire_page(sample_id,
                                               sample_audio_format_page_index_from_frame(
                                                   desc->format, frame_index),
                                               &page_span) == 0U)
        {
            return 0U;
        }

        out_span->l = page_span.frames_interleaved;
        out_span->r = (page_span.format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
                         ? 0
                         : (&page_span.frames_interleaved[1U]);
        out_span->frames = page_span.frame_count;
        out_span->frame_stride = page_span.stride_floats;
        out_span->format = page_span.format;
        out_span->frames_per_page = page_span.frames_per_page;
        out_span->start_frame = page_span.start_frame;
        out_span->backing_page_index = page_span.page_index;
        out_span->is_mono = (page_span.format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO) ? 1U : 0U;
        out_span->page_acquired = 1U;
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

    out_span->l = &desc->cache[start_index * desc->stride_floats];
    out_span->r = (desc->format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
                      ? 0
                      : (&desc->cache[(start_index * desc->stride_floats) + 1U]);
    out_span->frames = frames;
    out_span->frame_stride = desc->stride_floats;
    out_span->format = desc->format;
    out_span->frames_per_page = desc->frames_per_page;
    out_span->start_frame = start_frame;
    out_span->backing_page_index = 0U;
    out_span->is_mono = (desc->format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO) ? 1U : 0U;
    out_span->page_acquired = 0U;
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
    if (voice->voice_id < SAMPLE_CACHE_MAX_VOICES)
    {
        sample_voice_reader_update_frame_pos(&g_sample_cache_readers[voice->voice_id], frame_pos);
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
    if ((voice->voice_id < SAMPLE_CACHE_MAX_VOICES)
        && (voice->sample_id < SAMPLE_CACHE_HOT_SAMPLE_CAPACITY))
    {
        (void)sample_cache_voice_bind_reader(voice, &g_sample_cache[voice->sample_id]);
    }
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
            const float sample_l = src_l[i * block.frame_step];
            const float sample_r = src_r[i * block.frame_step];
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
        voice->stream_release_pending = 1U;
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
        (void)sample_page_cache_begin_read_block(voice->sample_id, frame_index, 1U, &page_block);
        if ((page_block.status != SAMPLE_PAGE_BLOCK_OK) || (page_block.frame_count == 0U))
        {
            sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_UNDERRUN);
            return 0U;
        }

        *out_l = page_block.frames_interleaved[0];
        *out_r = (page_block.format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
                     ? page_block.frames_interleaved[0]
                     : page_block.frames_interleaved[1U];
        sample_page_cache_commit_read_block(
            voice->sample_id,
            sample_audio_format_page_index_from_frame(desc->format, frame_index));
        voice->frame_pos = frame_index + 1U;
        return 1U;
    }

    if (sample_cache_frame_available(desc, frame_index) == 0U)
    {
        sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_UNDERRUN);
        return 0U;
    }

    const uint32_t cache_index = sample_cache_frame_offset(desc, frame_index);
    *out_l = desc->cache[cache_index * desc->stride_floats];
    *out_r = (desc->format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
                 ? *out_l
                 : desc->cache[(cache_index * desc->stride_floats) + 1U];
    voice->frame_pos = frame_index + 1U;
    return 1U;
}

uint8_t sample_cache_peek_frame(uint16_t sample_id, uint32_t frame_index, float *out_l, float *out_r)
{
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
        (void)sample_page_cache_begin_read_block(sample_id, frame_index, 1U, &page_block);
        if ((page_block.status != SAMPLE_PAGE_BLOCK_OK) || (page_block.frame_count == 0U))
        {
            return 0U;
        }

        *out_l = page_block.frames_interleaved[0];
        *out_r = (page_block.format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
                     ? page_block.frames_interleaved[0]
                     : page_block.frames_interleaved[1U];
        sample_page_cache_commit_read_block(
            sample_id,
            sample_audio_format_page_index_from_frame(desc->format, frame_index));
        return 1U;
    }

    if ((desc->cache == 0) || (sample_cache_frame_available(desc, frame_index) == 0U))
    {
        return 0U;
    }

    const uint32_t cache_index = sample_cache_frame_offset(desc, frame_index);
    *out_l = desc->cache[cache_index * desc->stride_floats];
    *out_r = (desc->format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
                 ? *out_l
                 : desc->cache[(cache_index * desc->stride_floats) + 1U];
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
