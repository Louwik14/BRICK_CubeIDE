#include "Sampler/sample_cache.h"

#include <ctype.h>
#include <string.h>

#include "Platform/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_page_cache_port.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_classic_audio_projection_control.h"
#include "Storage/waveform_cache.h"
#include "ff.h"

#define SAMPLE_CACHE_MAX_VOICES (16U)
#define SAMPLE_CACHE_STREAM_START_PAGES SAMPLE_PAGE_CLASSIC_FORWARD_WINDOW_PAGES
#define SAMPLE_CACHE_STREAM_TAIL_PAGES SAMPLE_PAGE_CLASSIC_REVERSE_WINDOW_PAGES
#define SAMPLE_CACHE_STREAM_FORWARD_LOOKAHEAD_PAGES SAMPLE_PAGE_CLASSIC_FORWARD_LOOKAHEAD_PAGES
#define SAMPLE_CACHE_STREAM_REVERSE_LOOKAHEAD_PAGES SAMPLE_PAGE_CLASSIC_REVERSE_LOOKAHEAD_PAGES
#define SAMPLE_CACHE_STREAM_STATIC_PAGES SAMPLE_CACHE_STREAM_START_PAGES
#define SAMPLE_CACHE_FULL_MAX_BYTES (SAMPLE_CACHE_STREAM_STATIC_PAGES * SAMPLE_PAGE_BYTES)

SDRAM_CLASSIC_POOL static sample_cache_desc_t g_sample_cache[SAMPLE_CLASSIC_CAPACITY];
static CTRL_STATE FRESULT g_sample_cache_last_fresult[SAMPLE_CLASSIC_CAPACITY];
static uint8_t g_sample_cache_stream_gate_held;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_CLASSIC_CAPACITY <= SAMPLE_PAGE_CACHE_ID_CAPACITY,
               "hot sample cache ids must fit in the page-cache id space");
_Static_assert(SAMPLE_CACHE_FULL_MAX_BYTES == (SAMPLE_CACHE_STREAM_STATIC_PAGES * SAMPLE_PAGE_BYTES),
               "FULL byte threshold must match the fixed page budget");
#endif

static uint8_t sample_cache_try_prepare_full_via_page_cache(uint16_t sample_id,
                                                            sample_cache_desc_t *desc,
                                                            FIL *fp,
                                                            const char *path);
static uint8_t sample_cache_prepare_partial_via_page_cache(uint16_t sample_id,
                                                           sample_cache_desc_t *desc,
                                                           FIL *map_file,
                                                           const char *path);
static uint8_t sample_cache_reserve_static_page_span(uint16_t sample_id,
                                                  const sample_play_plan_page_span_t *span);
static uint32_t sample_cache_stream_last_page_index(const sample_cache_desc_t *desc);
static uint8_t sample_cache_stream_start_base_ready(uint16_t sample_id,
                                                    const sample_cache_desc_t *desc);

static uint32_t sample_cache_product_cost_bytes(uint32_t frames,
                                                sample_audio_format_t format)
{
    const uint32_t prep_frames = (frames < SAMPLE_PREP_MIN_READY_FRAMES)
                                     ? frames
                                     : SAMPLE_PREP_MIN_READY_FRAMES;
    return sample_audio_format_required_page_count(
               sample_audio_format_or_stereo(format), prep_frames)
           * SAMPLE_PAGE_BYTES;
}
static const char *sample_cache_path(uint16_t sample_id)
{
    const sample_global_slot_t *const slot = sample_global_pool_get_slot(sample_id);
    return ((slot != 0) && (slot->kind == SAMPLE_GLOBAL_KIND_CLASSIC))
               ? slot->path
               : 0;
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

static void sample_cache_release_slot(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
    {
        return;
    }

    sample_stream_manager_release_sample(sample_id);
    sample_page_cache_port_clear(sample_audio_key_classic(sample_id));
}

static uint8_t sample_cache_try_prepare_full_via_page_cache(uint16_t sample_id,
                                                            sample_cache_desc_t *desc,
                                                            FIL *fp,
                                                            const char *path)
{
    if ((desc == 0) || (fp == 0) || (desc->total_frames == 0U)
        || (desc->mode != SAMPLE_CACHE_MODE_FULL))
    {
        return 0U;
    }

    const sample_page_load_result_t page_result =
        sample_page_cache_port_load_full(sample_audio_key_classic(sample_id),
                                         path, fp, &desc->info,
                                         desc->total_frames, desc->info.data_offset,
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
        sample_page_cache_port_clear(sample_audio_key_classic(sample_id));
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
    desc->loaded_frames = desc->total_frames;
    desc->fully_cached = 1U;
    desc->state = SAMPLE_CACHE_READY_FULL;
    desc->last_error = 0U;
    return 1U;
}

static uint8_t sample_cache_prepare_partial_via_page_cache(uint16_t sample_id,
                                                           sample_cache_desc_t *desc,
                                                           FIL *map_file,
                                                           const char *path)
{
    if ((desc == 0) || (desc->mode != SAMPLE_CACHE_MODE_STREAM))
    {
        return 0U;
    }

    if (sample_page_cache_port_register_file(
            sample_audio_key_classic(sample_id),
            path,
            &desc->info,
            desc->total_frames,
            desc->info.data_offset,
            map_file) == 0U)
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
    if (sample_cache_reserve_static_page_span(sample_id, &forward_span) == 0U)
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
        if (sample_cache_reserve_static_page_span(sample_id, &reverse_span) == 0U)
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
    desc->loaded_frames = desc->cache_valid_frames;
    desc->fully_cached = 0U;
    desc->state = SAMPLE_CACHE_READY_PARTIAL;
    desc->last_error = 0U;
    return 1U;
}

static uint8_t sample_cache_reserve_static_page_span(uint16_t sample_id,
                                                  const sample_play_plan_page_span_t *span)
{
    if ((span == 0) || (span->valid == 0U) || (span->page_count == 0U)
        || (span->page_start > span->page_end))
    {
        return 0U;
    }

    for (uint32_t page_index = span->page_start; page_index <= span->page_end; ++page_index)
    {
        if (sample_page_cache_port_reserve_static(
                sample_audio_key_classic(sample_id), page_index,
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

static uint8_t sample_cache_start_frame_available(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
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

static uint8_t sample_cache_stream_start_base_ready(uint16_t sample_id,
                                                    const sample_cache_desc_t *desc)
{
    if ((sample_id >= SAMPLE_CLASSIC_CAPACITY) || (desc == 0)
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

static uint8_t sample_cache_stream_start_base_failed(uint16_t sample_id,
                                                     const sample_cache_desc_t *desc)
{
    if ((sample_id >= SAMPLE_CLASSIC_CAPACITY) || (desc == 0)
        || (desc->mode != SAMPLE_CACHE_MODE_STREAM) || (desc->total_frames == 0U))
    {
        return 0U;
    }
    const uint32_t last_page = sample_cache_stream_last_page_index(desc);
    uint32_t required_pages = sample_audio_format_presocle_pages(desc->format);
    if (required_pages > (last_page + 1U))
    {
        required_pages = last_page + 1U;
    }
    for (uint32_t page = 0U; page < required_pages; ++page)
    {
        if (sample_page_cache_get_page_state(sample_id, page) == SAMPLE_PAGE_FAILED)
        {
            return 1U;
        }
    }
    return 0U;
}

void sample_cache_init(void)
{
    sample_classic_audio_projection_init();
    sample_page_cache_reset();
    sample_stream_manager_init();
    if ((g_sample_cache_stream_gate_held != 0U)
        && (sd_access_gate_current_owner() == SD_ACCESS_CLIENT_SAMPLE_STREAM))
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_STREAM);
    }
    g_sample_cache_stream_gate_held = 0U;

    for (uint32_t i = 0U; i < SAMPLE_CLASSIC_CAPACITY; ++i)
    {
        sample_cache_clear_desc(&g_sample_cache[i]);
        g_sample_cache_last_fresult[i] = FR_OK;
    }

}

void sample_cache_clear(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
    {
        return;
    }

    sample_cache_release_slot(sample_id);
    sample_cache_clear_desc(&g_sample_cache[sample_id]);
    g_sample_cache_last_fresult[sample_id] = FR_OK;
}

uint8_t sample_cache_prepare(uint16_t sample_id, const char *path)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
    {
        return 0U;
    }

    g_sample_cache_last_fresult[sample_id] = FR_OK;

    char prepared_path[SAMPLE_CLASSIC_PATH_MAX];
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

    sample_classic_audio_projection_withdraw(sample_id);
    sample_classic_audio_projection_withdraw(sample_id);
    sample_cache_release_slot(sample_id);
    sample_cache_clear_desc(&g_sample_cache[sample_id]);
    sample_global_pool_clear_slot(sample_id);

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    desc->state = SAMPLE_CACHE_PREPARING;

    uint8_t ok = 0U;
    uint8_t fp_open = 0U;
    FIL fp;
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        desc->last_error = 3U;
        g_sample_cache_last_fresult[sample_id] = FR_DISK_ERR;
        goto done;
    }

    const FRESULT open_fr = f_open(&fp, prepared_path, FA_READ);
    if (open_fr != FR_OK)
    {
        desc->last_error = 4U;
        g_sample_cache_last_fresult[sample_id] = open_fr;
        goto done;
    }
    fp_open = 1U;

    if (wav_parser_parse_info(&fp, &desc->info) == 0U)
    {
        desc->last_error = 5U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_OBJECT;
        goto done;
    }

    if (desc->info.sample_rate != 48000U)
    {
        desc->last_error = 15U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
        goto done;
    }

    if (sample_cache_wav_format_supported(&desc->info) == 0U)
    {
        desc->last_error = 6U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
        goto done;
    }

    desc->total_frames = desc->info.data_size / desc->info.block_align;
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
    desc->loaded_frames = 0U;

    if (desc->mode == SAMPLE_CACHE_MODE_FULL)
    {
        desc->cache = 0;
        desc->cache_window_start_frame = 0U;
        desc->state = SAMPLE_CACHE_PREFILLING;
        if (sample_cache_try_prepare_full_via_page_cache(sample_id, desc, &fp,
                                                         prepared_path) == 0U)
        {
            goto done;
        }

        ok = 1U;
        goto done;
    }

    desc->cache = 0;
    desc->cache_window_start_frame = 0U;
    desc->state = SAMPLE_CACHE_PREFILLING;
    if (sample_cache_prepare_partial_via_page_cache(sample_id, desc, &fp,
                                                    prepared_path) == 0U)
    {
        goto done;
    }

    ok = 1U;

done:
    if (fp_open != 0U)
    {
        (void)f_close(&fp);
    }
    if (ok != 0U)
    {
        const uint32_t product_cost = sample_cache_product_cost_bytes(
            desc->total_frames, desc->format);
        if (sample_global_pool_register_classic_at(sample_id, prepared_path,
                                                   product_cost) == 0U)
        {
            ok = 0U;
            desc->last_error = 8U;
            g_sample_cache_last_fresult[sample_id] = FR_DENIED;
        }
        else
        {
            (void)waveform_cache_request_for_wav_known_duration(
                prepared_path, WAVEFORM_CACHE_REASON_EDITOR_VISIBLE,
                desc->total_frames, desc->info.sample_rate);
        }
    }
    if (ok == 0U)
    {
        sample_cache_release_slot(sample_id);
        desc->state = SAMPLE_CACHE_ERROR;
    }
    else if (sample_cache_is_ready(sample_id) != 0U)
    {
        (void)sample_classic_audio_projection_publish(sample_id);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
    return ok;
}

void sample_cache_service(uint32_t byte_budget)
{
    for (uint16_t sample_id = 0U; sample_id < SAMPLE_CLASSIC_CAPACITY; ++sample_id)
    {
        if (sample_cache_is_ready(sample_id) != 0U)
            (void)sample_classic_audio_projection_publish(sample_id);
        else
            sample_classic_audio_projection_withdraw(sample_id);
    }
    if (byte_budget == 0U)
    {
        return;
    }

    if ((g_sample_cache_stream_gate_held == 0U)
        && (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_STREAM) == 0U))
    {
        return;
    }
    g_sample_cache_stream_gate_held = 1U;

    sample_stream_manager_service(byte_budget);
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_STREAM);
    g_sample_cache_stream_gate_held = 0U;
}

uint8_t sample_cache_has_pending_sd_work(void)
{
    if (sample_stream_manager_has_pending_sd_work() != 0U)
    {
        return 1U;
    }

    if (sample_page_cache_has_reserved_range(0U, SAMPLE_CLASSIC_CAPACITY) != 0U)
    {
        return 1U;
    }

    return 0U;
}

uint8_t sample_cache_is_ready(uint16_t sample_id)
{
    return (sample_cache_get_slot_readiness(sample_id) == SAMPLE_CACHE_SLOT_PLAYABLE) ? 1U : 0U;
}

sample_cache_slot_readiness_t sample_cache_get_slot_readiness(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
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
            if (sample_cache_stream_start_base_failed(sample_id, desc) != 0U)
            {
                return SAMPLE_CACHE_SLOT_ERROR;
            }
            if (sample_cache_stream_start_base_ready(sample_id, desc) == 0U)
            {
                return SAMPLE_CACHE_SLOT_START_PENDING;
            }
            return SAMPLE_CACHE_SLOT_PLAYABLE;

        case SAMPLE_CACHE_ERROR:
        default:
            return SAMPLE_CACHE_SLOT_ERROR;
    }
}

sample_cache_state_t sample_cache_get_state(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
    {
        return SAMPLE_CACHE_ERROR;
    }

    return g_sample_cache[sample_id].state;
}

uint8_t sample_cache_get_last_error(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
    {
        return 1U;
    }

    return g_sample_cache[sample_id].last_error;
}

uint8_t sample_cache_get_last_fresult(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
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
    if ((sample_id >= SAMPLE_CLASSIC_CAPACITY) || (out_source == 0))
    {
        return 0U;
    }

    const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->total_frames == 0U)
        || (desc->state == SAMPLE_CACHE_EMPTY)
        || (desc->state == SAMPLE_CACHE_ERROR))
    {
        return 0U;
    }

    out_source->key = sample_audio_key_classic(sample_id);
    out_source->path = sample_cache_path(sample_id);
    out_source->total_frames = desc->total_frames;
    out_source->data_offset = desc->info.data_offset;
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

uint8_t sample_cache_peek_frame(uint16_t sample_id, uint32_t frame_index, float *out_l, float *out_r)
{
    if ((sample_id >= SAMPLE_CLASSIC_CAPACITY) || (out_l == 0) || (out_r == 0))
    {
        return 0U;
    }

    const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->state != SAMPLE_CACHE_READY_FULL)
        && (desc->state != SAMPLE_CACHE_READY_PARTIAL))
    {
        return 0U;
    }

    if (frame_index >= desc->total_frames)
    {
        return 0U;
    }

    if ((desc->mode == SAMPLE_CACHE_MODE_STREAM) && (desc->fully_cached == 0U))
    {
        sample_page_span_t page;
        const uint32_t page_index = sample_audio_format_page_index_from_frame(
            desc->format, frame_index);
        if (sample_page_cache_control_resolve_page(
                sample_id, page_index, &page) == 0U)
        {
            return 0U;
        }
        const uint32_t page_offset = frame_index - page.start_frame;
        const float *const frame =
            &page.frames_interleaved[page_offset * page.stride_floats];
        *out_l = frame[0];
        *out_r = (page.format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
                     ? frame[0] : frame[1U];
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
