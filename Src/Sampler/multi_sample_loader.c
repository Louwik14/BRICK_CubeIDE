#include "Sampler/multi_sample_loader.h"

#include <string.h>

#include "Sampler/multi_sample_index.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_cache.h"
#include "SD/sd_diskio.h"
#include "Seq/seq_runtime.h"
#include "Storage/looper_storage.h"
#include "Storage/memory_layout.h"
#include "Storage/multi_record_writer.h"
#include "Storage/sd_access_gate.h"
#include "Storage/wav_audio_codec.h"
#include "Storage/wav_parser.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#define MULTI_SAMPLE_LOADER_PATH_MAX SAMPLE_PAGE_CACHE_PATH_MAX
#define MULTI_SAMPLE_BULK_READ_BYTES (64U * 1024U)
#define MULTI_SAMPLE_BULK_MAX_BATCH_PAGES \
    (MULTI_SAMPLE_BULK_READ_BYTES / 4096U)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert((MULTI_SAMPLE_BULK_READ_BYTES % 512U) == 0U,
               "Multi bulk buffer must remain sector aligned in size");
_Static_assert(MULTI_SAMPLE_BULK_MAX_BATCH_PAGES >= SAMPLE_AUDIO_FORMAT_STEREO_PRESOCLE_PAGES,
               "one bulk batch must cover a stereo presocle when source width permits");
#endif

typedef struct
{
    uint8_t used;
    uint16_t instrument_id;
    char path[MULTI_SAMPLE_LOADER_PATH_MAX];
} multi_sample_load_request_t;

typedef struct
{
    uint16_t required_pages;
    uint16_t budget_pages;
    uint16_t samples_preparable;
    uint16_t first_unpreparable_sample;
} multi_sample_prep_budget_t;

static multi_sample_load_diag_t g_multi_load_diag;
static uint8_t g_multi_load_active;
static uint16_t g_multi_load_first_sample_id;
SDRAM_MULTI_LOAD static multi_sample_load_request_t
    g_multi_load_queue[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];

typedef struct
{
    uint16_t sample_id;
    uint16_t pages_remaining;
    uint32_t range_first[2];
    uint32_t range_last[2];
    uint8_t range_count;
    uint8_t current_range;
    uint16_t reserved;
    uint32_t next_page;
} multi_sample_bulk_plan_t;

typedef struct
{
    FIL file;
    uint16_t plan_count;
    uint16_t current_plan;
    uint8_t file_open;
    uint8_t exclusive;
    uint16_t reserved;
    uint32_t started_at_ms;
} multi_sample_bulk_state_t;

SDRAM_MULTI_LOAD static multi_sample_bulk_plan_t
    g_multi_bulk_plans[MULTI_SAMPLE_MAX_SAMPLES];
SDRAM_MULTI_LOAD static uint8_t g_multi_bulk_read_buffer[MULTI_SAMPLE_BULK_READ_BYTES];
static multi_sample_bulk_state_t g_multi_bulk;

static uint8_t multi_loader_copy_text(char *dst, uint32_t dst_size, const char *src)
{
    if ((dst == 0) || (dst_size == 0U) || (src == 0) || (src[0] == '\0'))
    {
        return 0U;
    }

    uint32_t i = 0U;
    while ((i + 1U) < dst_size)
    {
        dst[i] = src[i];
        if (src[i] == '\0')
        {
            return 1U;
        }
        i++;
    }

    dst[i] = '\0';
    return (src[i] == '\0') ? 1U : 0U;
}

static multi_sample_load_result_t multi_loader_enqueue(const char *index_path,
                                                       uint16_t instrument_id)
{
    if ((index_path == 0) || (index_path[0] == '\0')
        || (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
    {
        return MULTI_SAMPLE_LOAD_INVALID_ARG;
    }

    for (uint16_t i = 0U; i < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++i)
    {
        if ((g_multi_load_queue[i].used != 0U)
            && (g_multi_load_queue[i].instrument_id == instrument_id))
        {
            return MULTI_SAMPLE_LOAD_OK;
        }
    }

    for (uint16_t i = 0U; i < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++i)
    {
        if (g_multi_load_queue[i].used == 0U)
        {
            if (multi_loader_copy_text(g_multi_load_queue[i].path,
                                       sizeof(g_multi_load_queue[i].path),
                                       index_path)
                == 0U)
            {
                return MULTI_SAMPLE_LOAD_PATH_TOO_LONG;
            }

            g_multi_load_queue[i].instrument_id = instrument_id;
            g_multi_load_queue[i].used = 1U;
            return MULTI_SAMPLE_LOAD_OK;
        }
    }

    return MULTI_SAMPLE_LOAD_SD_BUSY;
}

static multi_sample_load_result_t multi_loader_start_instrument(const char *index_path,
                                                               uint16_t instrument_id);

static uint8_t multi_loader_parent_dir(const char *path, char *out, uint32_t out_size)
{
    if ((path == 0) || (out == 0) || (out_size == 0U))
    {
        return 0U;
    }

    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if ((slash == 0) || ((backslash != 0) && (backslash > slash)))
    {
        slash = backslash;
    }

    if ((slash == 0) || (slash == path))
    {
        return 0U;
    }

    const uint32_t len = (uint32_t)(slash - path);
    if (len >= out_size)
    {
        return 0U;
    }

    memcpy(out, path, len);
    out[len] = '\0';
    return 1U;
}

static uint8_t multi_loader_join_path(char *out,
                                      uint32_t out_size,
                                      const char *base_dir,
                                      const char *relative_path,
                                      uint16_t relative_len)
{
    if ((out == 0) || (base_dir == 0) || (relative_path == 0)
        || (out_size == 0U) || (relative_len == 0U))
    {
        return 0U;
    }

    const uint32_t base_len = (uint32_t)strlen(base_dir);
    const uint8_t needs_sep =
        ((base_len != 0U) && (base_dir[base_len - 1U] != '/')
         && (base_dir[base_len - 1U] != '\\'))
            ? 1U
            : 0U;
    const uint32_t total = base_len + (needs_sep != 0U ? 1U : 0U) + relative_len;
    if (total >= out_size)
    {
        return 0U;
    }

    memcpy(out, base_dir, base_len);
    uint32_t pos = base_len;
    if (needs_sep != 0U)
    {
        out[pos++] = '/';
    }
    memcpy(&out[pos], relative_path, relative_len);
    out[pos + relative_len] = '\0';
    return 1U;
}

static wav_info_t multi_loader_wav_info_from_index_sample(
    const multi_sample_index_sample_t *sample)
{
    wav_info_t info;
    memset(&info, 0, sizeof(info));
    if (sample != 0)
    {
        info.audio_format = 1U;
        info.sample_rate = sample->sample_rate;
        info.channels = sample->channels;
        info.bits_per_sample = sample->bits_per_sample;
        info.block_align = (uint16_t)((sample->channels * sample->bits_per_sample) / 8U);
        info.byte_rate = sample->sample_rate * info.block_align;
        info.data_offset = sample->data_offset;
        info.data_size = sample->data_size;
    }
    return info;
}

static void multi_loader_bulk_close_file(void)
{
    if (g_multi_bulk.file_open != 0U)
    {
        (void)f_close(&g_multi_bulk.file);
        g_multi_bulk.file_open = 0U;
    }
}

static void multi_loader_bulk_release_exclusive(void)
{
    multi_loader_bulk_close_file();
    if (g_multi_bulk.exclusive != 0U)
    {
        sd_access_gate_end_bulk_exclusive();
        g_multi_bulk.exclusive = 0U;
    }
}

static uint8_t multi_loader_bulk_runtime_stopped(void)
{
    if ((seq_runtime_is_running() != 0U)
        || (seq_runtime_is_start_pending() != 0U)
        || (sample_page_cache_has_window_locks() != 0U))
    {
        return 0U;
    }
    return ((g_multi_bulk.exclusive != 0U)
            || (sample_stream_manager_has_pending_sd_work() == 0U))
               ? 1U
               : 0U;
}

static void multi_loader_set_error(multi_sample_load_result_t error,
                                   uint16_t failed_sample)
{
    multi_loader_bulk_release_exclusive();
    if (g_multi_bulk.started_at_ms != 0U)
    {
        g_multi_load_diag.elapsed_ms = HAL_GetTick() - g_multi_bulk.started_at_ms;
    }
    g_multi_load_diag.last_error = error;
    g_multi_load_diag.last_failed_sample = failed_sample;
    g_multi_load_diag.state = MULTI_SAMPLE_INSTRUMENT_ERROR;
    (void)multi_sample_pool_clear_instrument(g_multi_load_diag.instrument_id);
    g_multi_load_active = 0U;
    memset(&g_multi_bulk, 0, sizeof(g_multi_bulk));
}

typedef struct
{
    uint32_t start_first;
    uint32_t start_last;
    uint32_t loop_first;
    uint32_t loop_last;
    uint8_t has_loop_span;
    uint8_t unique_pages;
} multi_loader_boundary_pages_t;

static multi_loader_boundary_pages_t multi_loader_sample_boundary_pages(
    uint32_t total_frames,
    uint16_t channels,
    uint8_t has_loop,
    uint32_t loop_begin,
    uint32_t loop_end)
{
    multi_loader_boundary_pages_t result = {0};
    if (total_frames == 0U)
    {
        return result;
    }

    const uint32_t contract_frames =
        (total_frames < SAMPLE_PREP_MIN_READY_FRAMES) ? total_frames
                                                       : SAMPLE_PREP_MIN_READY_FRAMES;
    const sample_audio_format_t format = sample_audio_format_or_stereo(
        sample_audio_format_from_channels(channels));
    result.start_first = 0U;
    result.start_last = sample_audio_format_page_index_from_frame(format, contract_frames - 1U);
    uint32_t pages = result.start_last + 1U;

    if ((has_loop != 0U) && (loop_end > loop_begin) && (loop_end <= total_frames))
    {
        uint32_t loop_ready_end = loop_begin + SAMPLE_PREP_MIN_READY_FRAMES;
        if ((loop_ready_end < loop_begin) || (loop_ready_end > loop_end))
        {
            loop_ready_end = loop_end;
        }
        result.loop_first = sample_audio_format_page_index_from_frame(format,
                                                                       loop_begin);
        result.loop_last = sample_audio_format_page_index_from_frame(format,
                                                                      loop_ready_end - 1U);
        result.has_loop_span = 1U;
        const uint32_t loop_pages = result.loop_last - result.loop_first + 1U;
        uint32_t overlap = 0U;
        if ((result.loop_first <= result.start_last) && (result.loop_last >= result.start_first))
        {
            const uint32_t overlap_first = (result.loop_first > result.start_first)
                                               ? result.loop_first
                                               : result.start_first;
            const uint32_t overlap_last = (result.loop_last < result.start_last)
                                              ? result.loop_last
                                              : result.start_last;
            overlap = overlap_last - overlap_first + 1U;
        }
        pages += loop_pages - overlap;
    }

    const uint32_t max_budget_pages =
        SAMPLE_PREP_MULTI_BUDGET_BYTES / SAMPLE_PAGE_BYTES;
    if (pages > max_budget_pages)
    {
        pages = max_budget_pages;
    }
    if (pages > UINT8_MAX)
    {
        pages = UINT8_MAX;
    }
    result.unique_pages = (uint8_t)pages;
    return result;
}

static uint8_t multi_loader_bulk_plan_build(
    multi_sample_bulk_plan_t *plan,
    uint16_t sample_id,
    const multi_loader_boundary_pages_t *boundaries)
{
    if ((plan == 0) || (boundaries == 0) || (boundaries->unique_pages == 0U))
    {
        return 0U;
    }

    memset(plan, 0, sizeof(*plan));
    plan->sample_id = sample_id;
    plan->pages_remaining = boundaries->unique_pages;
    plan->range_first[0] = boundaries->start_first;
    plan->range_last[0] = boundaries->start_last;
    plan->range_count = 1U;

    if (boundaries->has_loop_span != 0U)
    {
        if (boundaries->loop_first <= (boundaries->start_last + 1U))
        {
            if (boundaries->loop_last > plan->range_last[0])
            {
                plan->range_last[0] = boundaries->loop_last;
            }
        }
        else
        {
            plan->range_first[1] = boundaries->loop_first;
            plan->range_last[1] = boundaries->loop_last;
            plan->range_count = 2U;
        }
    }

    plan->next_page = plan->range_first[0];
    return 1U;
}

static uint8_t multi_loader_bulk_prepare_plan_pages(
    const multi_sample_bulk_plan_t *plan)
{
    if ((plan == 0) || (plan->range_count == 0U) || (plan->range_count > 2U))
    {
        return 0U;
    }

    const sample_audio_key_t key = sample_audio_key_multi(plan->sample_id);
    for (uint8_t range = 0U; range < plan->range_count; ++range)
    {
        for (uint32_t page = plan->range_first[range];
             page <= plan->range_last[range];
             ++page)
        {
            if ((sample_page_cache_prepare_bulk_page_key_alloc(
                     key, page, SAMPLE_PAGE_ALLOC_SLOT_PERMANENT) == 0U)
                || (sample_page_cache_pin_page_key_alloc(
                        key, page, SAMPLE_PAGE_ALLOC_SLOT_PERMANENT) == 0U))
            {
                return 0U;
            }
        }
    }
    return 1U;
}

static multi_sample_prep_budget_t multi_loader_calc_prep_budget(
    const multi_sample_index_t *index)
{
    multi_sample_prep_budget_t budget = {
        .budget_pages = SAMPLE_PREP_MULTI_BUDGET_PAGES,
        .first_unpreparable_sample = MULTI_SAMPLE_POOL_INVALID_ID,
    };
    uint32_t required_pages = 0U;

    if (index == 0)
    {
        return budget;
    }

    for (uint16_t i = 0U; i < index->sample_count; ++i)
    {
        const multi_sample_index_sample_t *const sample = &index->samples[i];
        const uint16_t pages = (uint16_t)multi_loader_sample_boundary_pages(
            sample->total_frames,
            sample->channels,
            sample->has_loop,
            sample->loop_begin,
            sample->loop_end).unique_pages;
        if (pages == 0U)
        {
            if (budget.first_unpreparable_sample == MULTI_SAMPLE_POOL_INVALID_ID)
            {
                budget.first_unpreparable_sample = i;
            }
            budget.required_pages = UINT16_MAX;
            continue;
        }

        required_pages += pages;
        if (required_pages > UINT16_MAX)
        {
            budget.required_pages = UINT16_MAX;
        }
        else
        {
            budget.required_pages = (uint16_t)required_pages;
        }

        if (required_pages > budget.budget_pages)
        {
            if (budget.first_unpreparable_sample == MULTI_SAMPLE_POOL_INVALID_ID)
            {
                budget.first_unpreparable_sample = i;
            }
            continue;
        }

        budget.samples_preparable++;
    }

    return budget;
}

static multi_sample_load_result_t multi_loader_start_instrument(const char *index_path,
                                                               uint16_t instrument_id)
{
    memset(&g_multi_load_diag, 0, sizeof(g_multi_load_diag));
    g_multi_load_diag.instrument_id = instrument_id;
    g_multi_load_diag.last_failed_sample = MULTI_SAMPLE_POOL_INVALID_ID;
    g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_OK;
    g_multi_load_diag.state = multi_sample_pool_get_state(instrument_id);
    g_multi_load_active = 0U;
    g_multi_load_first_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    memset(&g_multi_bulk, 0, sizeof(g_multi_bulk));

    if ((index_path == 0) || (index_path[0] == '\0')
        || (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
    {
        g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_INVALID_ARG;
        return MULTI_SAMPLE_LOAD_INVALID_ARG;
    }

    if (multi_sample_pool_get_state(instrument_id) == MULTI_SAMPLE_INSTRUMENT_READY)
    {
        g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_ALREADY_READY;
        g_multi_load_diag.state = MULTI_SAMPLE_INSTRUMENT_READY;
        return MULTI_SAMPLE_LOAD_ALREADY_READY;
    }

    if (multi_loader_bulk_runtime_stopped() == 0U)
    {
        g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_TRANSPORT_ACTIVE;
        return MULTI_SAMPLE_LOAD_TRANSPORT_ACTIVE;
    }

    if ((multi_record_writer_any_active() != 0U)
        || (looper_storage_raw_export_is_active() != 0U)
        || (sample_cache_has_pending_sd_work() != 0U)
        || (sample_stream_manager_has_pending_sd_work() != 0U))
    {
        g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_SD_BUSY;
        return MULTI_SAMPLE_LOAD_SD_BUSY;
    }

    multi_sample_index_t index;
    const multi_sample_index_result_t load_result = multi_sample_index_load(index_path, &index);
    if (load_result != MULTI_SAMPLE_INDEX_OK)
    {
        g_multi_load_diag.last_error = (load_result == MULTI_SAMPLE_INDEX_LIMIT)
            ? MULTI_SAMPLE_LOAD_INDEX_LIMIT
            : MULTI_SAMPLE_LOAD_INDEX_FAIL;
        return g_multi_load_diag.last_error;
    }

    const multi_sample_index_result_t apply_result =
        multi_sample_index_apply_to_pool(&index, instrument_id);
    if (apply_result != MULTI_SAMPLE_INDEX_OK)
    {
        g_multi_load_diag.last_error =
            (apply_result == MULTI_SAMPLE_INDEX_FORMAT_MISMATCH)
                ? MULTI_SAMPLE_LOAD_FORMAT_MISMATCH
                : MULTI_SAMPLE_LOAD_POOL_FAIL;
        (void)multi_sample_pool_set_state(instrument_id, MULTI_SAMPLE_INSTRUMENT_ERROR);
        g_multi_load_diag.state = MULTI_SAMPLE_INSTRUMENT_ERROR;
        return g_multi_load_diag.last_error;
    }
    (void)multi_sample_pool_set_index_path(instrument_id, index_path);

    const multi_sample_prep_budget_t prep_budget =
        multi_loader_calc_prep_budget(&index);
    g_multi_load_diag.prep_pages_required = prep_budget.required_pages;
    g_multi_load_diag.prep_pages_budget = prep_budget.budget_pages;
    g_multi_load_diag.prep_samples_preparable = prep_budget.samples_preparable;
    if ((prep_budget.required_pages > prep_budget.budget_pages)
        || (prep_budget.samples_preparable < index.sample_count))
    {
        const multi_sample_instrument_t *const failed_instrument =
            multi_sample_pool_get_instrument(instrument_id);
        multi_loader_set_error(
            MULTI_SAMPLE_LOAD_PREP_BUDGET_EXCEEDED,
            ((failed_instrument != 0)
             && (prep_budget.first_unpreparable_sample != MULTI_SAMPLE_POOL_INVALID_ID))
                ? (uint16_t)(failed_instrument->first_sample_id
                             + prep_budget.first_unpreparable_sample)
                : MULTI_SAMPLE_POOL_INVALID_ID);
        return MULTI_SAMPLE_LOAD_PREP_BUDGET_EXCEEDED;
    }

    const uint32_t product_cost_bytes =
        (uint32_t)prep_budget.required_pages * SAMPLE_PAGE_BYTES;
    uint16_t existing_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if ((sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_MULTI,
                                            instrument_id,
                                            &existing_global) == 0U)
        && (sample_global_pool_find_free_slot() == SAMPLE_GLOBAL_POOL_INVALID_INDEX))
    {
        g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_POOL_FAIL;
        return MULTI_SAMPLE_LOAD_POOL_FAIL;
    }
    if (sample_global_pool_validate_budget(SAMPLE_GLOBAL_KIND_MULTI,
                                           instrument_id,
                                           product_cost_bytes) == 0U)
    {
        g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_PREP_BUDGET_EXCEEDED;
        return MULTI_SAMPLE_LOAD_PREP_BUDGET_EXCEEDED;
    }

    const multi_sample_instrument_t *const instrument =
        multi_sample_pool_get_instrument(instrument_id);
    if ((instrument == 0) || (instrument->sample_count != index.sample_count)
        || (instrument->sample_count == 0U))
    {
        multi_loader_set_error(MULTI_SAMPLE_LOAD_POOL_FAIL, MULTI_SAMPLE_POOL_INVALID_ID);
        return MULTI_SAMPLE_LOAD_POOL_FAIL;
    }
    if (sample_global_pool_validate_entries(SAMPLE_GLOBAL_KIND_MULTI,
                                            instrument_id,
                                            instrument->sample_count) == 0U)
    {
        multi_loader_set_error(MULTI_SAMPLE_LOAD_POOL_FAIL, MULTI_SAMPLE_POOL_INVALID_ID);
        return MULTI_SAMPLE_LOAD_POOL_FAIL;
    }

    char base_dir[MULTI_SAMPLE_LOADER_PATH_MAX];
    if (multi_loader_parent_dir(index_path, base_dir, sizeof(base_dir)) == 0U)
    {
        multi_loader_set_error(MULTI_SAMPLE_LOAD_PATH_TOO_LONG, MULTI_SAMPLE_POOL_INVALID_ID);
        return MULTI_SAMPLE_LOAD_PATH_TOO_LONG;
    }

    g_multi_load_first_sample_id = instrument->first_sample_id;
    g_multi_load_diag.total_samples = instrument->sample_count;
    g_multi_load_diag.state = MULTI_SAMPLE_INSTRUMENT_LOADING;
    (void)multi_sample_pool_set_state(instrument_id, MULTI_SAMPLE_INSTRUMENT_LOADING);

    for (uint16_t i = 0U; i < index.sample_count; ++i)
    {
        const multi_sample_index_sample_t *const sample = &index.samples[i];
        char full_path[MULTI_SAMPLE_LOADER_PATH_MAX];
        if (multi_loader_join_path(full_path,
                                   sizeof(full_path),
                                   base_dir,
                                   &index.strings[sample->path_offset],
                                   sample->path_len) == 0U)
        {
            multi_loader_set_error(MULTI_SAMPLE_LOAD_PATH_TOO_LONG,
                                   (uint16_t)(g_multi_load_first_sample_id + i));
            return MULTI_SAMPLE_LOAD_PATH_TOO_LONG;
        }

        const uint16_t multi_sample_id = (uint16_t)(g_multi_load_first_sample_id + i);
        const sample_audio_key_t key = sample_audio_key_multi(multi_sample_id);
        const wav_info_t info = multi_loader_wav_info_from_index_sample(sample);
        sample_stream_manager_release_key(key);
        if (sample_page_cache_register_stream_sample_key_no_map(key,
                                                                full_path,
                                                                &info,
                                                                sample->total_frames,
                                                                sample->data_offset) == 0U)
        {
            multi_loader_set_error(MULTI_SAMPLE_LOAD_REGISTER_FAIL, multi_sample_id);
            return MULTI_SAMPLE_LOAD_REGISTER_FAIL;
        }

        const multi_loader_boundary_pages_t boundaries =
            multi_loader_sample_boundary_pages(sample->total_frames,
                                               sample->channels,
                                               sample->has_loop,
                                               sample->loop_begin,
                                               sample->loop_end);
        if ((i >= MULTI_SAMPLE_MAX_SAMPLES)
            || (multi_loader_bulk_plan_build(&g_multi_bulk_plans[i],
                                             multi_sample_id,
                                             &boundaries) == 0U)
            || (multi_loader_bulk_prepare_plan_pages(&g_multi_bulk_plans[i]) == 0U))
        {
            multi_loader_set_error(MULTI_SAMPLE_LOAD_NOT_ENOUGH_CACHE, multi_sample_id);
            return MULTI_SAMPLE_LOAD_NOT_ENOUGH_CACHE;
        }
        g_multi_load_diag.pages_requested =
            (uint16_t)(g_multi_load_diag.pages_requested + boundaries.unique_pages);
    }

    g_multi_bulk.plan_count = index.sample_count;
    g_multi_bulk.current_plan = 0U;
    g_multi_bulk.started_at_ms = HAL_GetTick();
    g_multi_load_diag.pages_remaining = g_multi_load_diag.pages_requested;
    g_multi_load_diag.samples_remaining = index.sample_count;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_multi_load_active = 1U;
    return MULTI_SAMPLE_LOAD_OK;
}

multi_sample_load_result_t multi_sample_load_instrument(const char *index_path,
                                                        uint16_t instrument_id)
{
    if (g_multi_load_active != 0U)
    {
        return multi_loader_enqueue(index_path, instrument_id);
    }

    const multi_sample_load_result_t result =
        multi_loader_start_instrument(index_path, instrument_id);
    if (result == MULTI_SAMPLE_LOAD_SD_BUSY)
    {
        return multi_loader_enqueue(index_path, instrument_id);
    }

    return result;
}

static void multi_loader_start_next_queued(void)
{
    if (g_multi_load_active != 0U)
    {
        return;
    }

    for (uint16_t i = 0U; i < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++i)
    {
        if (g_multi_load_queue[i].used != 0U)
        {
            char path[MULTI_SAMPLE_LOADER_PATH_MAX];
            const uint16_t instrument_id = g_multi_load_queue[i].instrument_id;
            (void)multi_loader_copy_text(path, sizeof(path), g_multi_load_queue[i].path);
            const multi_sample_load_result_t result =
                multi_loader_start_instrument(path, instrument_id);
            if (result != MULTI_SAMPLE_LOAD_SD_BUSY)
            {
                g_multi_load_queue[i].used = 0U;
            }
            return;
        }
    }
}

static uint8_t multi_loader_bulk_open_current_sample(
    multi_sample_bulk_plan_t *plan,
    const multi_sample_desc_t **out_sample)
{
    if ((plan == 0) || (out_sample == 0))
    {
        return 0U;
    }

    const multi_sample_desc_t *const sample = multi_sample_pool_get_sample(plan->sample_id);
    sample_page_stream_info_t stream_info;
    if ((sample == 0) || (sample->block_align == 0U)
        || (sample_page_cache_get_stream_info_key(sample_audio_key_multi(plan->sample_id),
                                                  &stream_info) == 0U))
    {
        return 0U;
    }

    if (g_multi_bulk.file_open == 0U)
    {
        if (f_open(&g_multi_bulk.file, stream_info.path, FA_READ) != FR_OK)
        {
            return 0U;
        }
        g_multi_bulk.file_open = 1U;
        g_multi_load_diag.file_opens++;
    }
    *out_sample = sample;
    return 1U;
}

static uint8_t multi_loader_bulk_seek_range(const multi_sample_bulk_plan_t *plan,
                                            const multi_sample_desc_t *sample)
{
    if ((plan == 0) || (sample == 0) || (plan->current_range >= plan->range_count)
        || (plan->next_page != plan->range_first[plan->current_range]))
    {
        return 1U;
    }

    const uint64_t frame = (uint64_t)plan->next_page * sample->frames_per_page;
    const uint64_t offset = (uint64_t)sample->data_offset
                            + (frame * sample->block_align);
    if (offset > UINT32_MAX)
    {
        return 0U;
    }
    g_multi_load_diag.seeks++;
    return (f_lseek(&g_multi_bulk.file, (FSIZE_t)offset) == FR_OK) ? 1U : 0U;
}

static uint8_t multi_loader_bulk_read_batch(multi_sample_bulk_plan_t *plan,
                                            const multi_sample_desc_t *sample)
{
    sample_page_load_target_t targets[MULTI_SAMPLE_BULK_MAX_BATCH_PAGES];
    sample_page_load_token_t tokens[MULTI_SAMPLE_BULK_MAX_BATCH_PAGES];
    uint32_t target_bytes[MULTI_SAMPLE_BULK_MAX_BATCH_PAGES];
    uint32_t batch_bytes = 0U;
    uint32_t target_count = 0U;
    uint32_t page = plan->next_page;
    const uint32_t range_last = plan->range_last[plan->current_range];
    const sample_audio_key_t key = sample_audio_key_multi(plan->sample_id);

    while ((page <= range_last) && (target_count < MULTI_SAMPLE_BULK_MAX_BATCH_PAGES))
    {
        sample_page_load_target_t target;
        if (sample_page_cache_get_bulk_load_target_key(key, page, &target) == 0U)
        {
            return 0U;
        }
        const uint32_t bytes = target.frame_count * sample->block_align;
        if ((bytes == 0U) || (bytes > MULTI_SAMPLE_BULK_READ_BYTES))
        {
            return 0U;
        }
        if ((target_count != 0U) && (batch_bytes > (MULTI_SAMPLE_BULK_READ_BYTES - bytes)))
        {
            break;
        }
        targets[target_count] = target;
        target_bytes[target_count] = bytes;
        batch_bytes += bytes;
        target_count++;
        page++;
    }

    if ((target_count == 0U) || (batch_bytes == 0U))
    {
        return 0U;
    }

    for (uint32_t i = 0U; i < target_count; ++i)
    {
        if (sample_page_cache_begin_in_flight(&targets[i], &tokens[i]) == 0U)
        {
            for (uint32_t j = 0U; j < i; ++j)
            {
                (void)sample_page_cache_finish_in_flight(&tokens[j],
                                                         SAMPLE_PAGE_FINISH_ERROR);
            }
            return 0U;
        }
    }

    UINT bytes_read = 0U;
    if ((f_read(&g_multi_bulk.file,
                g_multi_bulk_read_buffer,
                batch_bytes,
                &bytes_read) != FR_OK)
        || (bytes_read != batch_bytes))
    {
        for (uint32_t i = 0U; i < target_count; ++i)
        {
            (void)sample_page_cache_finish_in_flight(&tokens[i],
                                                     SAMPLE_PAGE_FINISH_ERROR);
        }
        return 0U;
    }
    g_multi_load_diag.read_calls++;
    g_multi_load_diag.bytes_read += batch_bytes;

    const wav_audio_codec_decode_block_fn decode_block =
        (sample->channels == 1U)
            ? wav_audio_codec_select_pcm_decode_mono_block(sample->bits_per_sample)
            : wav_audio_codec_select_pcm_decode_block(sample->channels,
                                                      sample->bits_per_sample);
    const sample_audio_format_t expected_format =
        sample_audio_format_from_channels(sample->channels);
    const uint32_t expected_block_align =
        (uint32_t)sample->channels * ((uint32_t)sample->bits_per_sample / 8U);
    if ((decode_block == 0) || (sample->block_align != expected_block_align)
        || (sample_audio_format_is_valid(expected_format) == 0U))
    {
        for (uint32_t i = 0U; i < target_count; ++i)
        {
            (void)sample_page_cache_finish_in_flight(&tokens[i],
                                                     SAMPLE_PAGE_FINISH_ERROR);
        }
        return 0U;
    }

    uint32_t read_offset = 0U;
    const uint32_t decode_begin = DWT->CYCCNT;
    for (uint32_t i = 0U; i < target_count; ++i)
    {
        if ((targets[i].format != expected_format)
            || (targets[i].stride_floats
                != sample_audio_format_stride_floats(expected_format)))
        {
            for (uint32_t j = i; j < target_count; ++j)
            {
                (void)sample_page_cache_finish_in_flight(&tokens[j],
                                                         SAMPLE_PAGE_FINISH_ERROR);
            }
            return 0U;
        }
        decode_block(&g_multi_bulk_read_buffer[read_offset],
                     targets[i].frames_interleaved,
                     targets[i].frame_count);
        read_offset += target_bytes[i];
        __DMB();
        if (sample_page_cache_finish_in_flight(&tokens[i],
                                               SAMPLE_PAGE_FINISH_READY) == 0U)
        {
            for (uint32_t j = i + 1U; j < target_count; ++j)
            {
                (void)sample_page_cache_finish_in_flight(&tokens[j],
                                                         SAMPLE_PAGE_FINISH_ERROR);
            }
            return 0U;
        }
        if (plan->pages_remaining != 0U)
        {
            plan->pages_remaining--;
        }
        if (g_multi_load_diag.pages_remaining != 0U)
        {
            g_multi_load_diag.pages_remaining--;
        }
        g_multi_load_diag.pages_ready++;
    }
    g_multi_load_diag.decode_cycles += DWT->CYCCNT - decode_begin;

    plan->next_page = page;
    if (plan->next_page > range_last)
    {
        plan->current_range++;
        if (plan->current_range < plan->range_count)
        {
            plan->next_page = plan->range_first[plan->current_range];
        }
    }
    return 1U;
}

static uint8_t multi_loader_bulk_finish_instrument(void)
{
    const multi_sample_instrument_t *const instrument =
        multi_sample_pool_get_instrument(g_multi_load_diag.instrument_id);
    if ((instrument == 0)
        || (sample_global_pool_register_multi(g_multi_load_diag.instrument_id,
                                              instrument->index_path,
                                              (uint32_t)g_multi_load_diag.pages_requested
                                                  * SAMPLE_PAGE_BYTES,
                                              instrument->sample_count,
                                              0) == 0U))
    {
        return 0U;
    }

    multi_loader_bulk_release_exclusive();
    g_multi_load_active = 0U;
    g_multi_load_diag.elapsed_ms = HAL_GetTick() - g_multi_bulk.started_at_ms;
    g_multi_load_diag.state = MULTI_SAMPLE_INSTRUMENT_READY;
    g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_OK;
    (void)multi_sample_pool_set_state(g_multi_load_diag.instrument_id,
                                      MULTI_SAMPLE_INSTRUMENT_READY);
    memset(&g_multi_bulk, 0, sizeof(g_multi_bulk));
    multi_loader_start_next_queued();
    return 1U;
}

void multi_sample_service_load(uint32_t byte_budget)
{
    (void)byte_budget;
    if (g_multi_load_active == 0U)
    {
        multi_loader_start_next_queued();
        return;
    }

    if (multi_loader_bulk_runtime_stopped() == 0U)
    {
        multi_loader_set_error(MULTI_SAMPLE_LOAD_TRANSPORT_ACTIVE,
                               (g_multi_bulk.current_plan < g_multi_bulk.plan_count)
                                   ? g_multi_bulk_plans[g_multi_bulk.current_plan].sample_id
                                   : MULTI_SAMPLE_POOL_INVALID_ID);
        return;
    }

    if (g_multi_bulk.exclusive == 0U)
    {
        if (sd_access_gate_begin_bulk_exclusive() == 0U)
        {
            return;
        }
        g_multi_bulk.exclusive = 1U;
        sd_diskio_read_metrics_reset();
    }

    if (g_multi_bulk.current_plan >= g_multi_bulk.plan_count)
    {
        if (multi_loader_bulk_finish_instrument() == 0U)
        {
            multi_loader_set_error(MULTI_SAMPLE_LOAD_POOL_FAIL,
                                   MULTI_SAMPLE_POOL_INVALID_ID);
        }
        return;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_MULTI_BULK) == 0U)
    {
        return;
    }
    g_multi_load_diag.service_passes++;
    g_multi_load_diag.legacy_rescans_avoided +=
        (uint32_t)g_multi_load_diag.total_samples + g_multi_load_diag.pages_requested;

    multi_sample_bulk_plan_t *const plan =
        &g_multi_bulk_plans[g_multi_bulk.current_plan];
    const multi_sample_desc_t *sample = 0;
    const uint8_t ok =
        (multi_loader_bulk_open_current_sample(plan, &sample) != 0U)
        && (multi_loader_bulk_seek_range(plan, sample) != 0U)
        && (multi_loader_bulk_read_batch(plan, sample) != 0U);

    if ((ok != 0U) && (plan->pages_remaining == 0U))
    {
        multi_loader_bulk_close_file();
        g_multi_bulk.current_plan++;
        g_multi_load_diag.samples_ready++;
        if (g_multi_load_diag.samples_remaining != 0U)
        {
            g_multi_load_diag.samples_remaining--;
        }
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_MULTI_BULK);

    sd_diskio_read_metrics_t physical_metrics;
    sd_diskio_read_metrics_get(&physical_metrics);
    g_multi_load_diag.physical_reads = physical_metrics.read_transactions;
    g_multi_load_diag.physical_bytes = physical_metrics.read_bytes;
    g_multi_load_diag.max_read_bytes = physical_metrics.max_read_bytes;

    if (ok == 0U)
    {
        multi_loader_set_error(MULTI_SAMPLE_LOAD_PAGE_ERROR, plan->sample_id);
    }
}

uint8_t multi_sample_is_ready(uint16_t instrument_id)
{
    return (multi_sample_pool_get_state(instrument_id) == MULTI_SAMPLE_INSTRUMENT_READY)
        ? 1U
        : 0U;
}

uint8_t multi_sample_load_has_pending(void)
{
    if (g_multi_load_active != 0U)
    {
        return 1U;
    }

    for (uint16_t i = 0U; i < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++i)
    {
        if (g_multi_load_queue[i].used != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

uint8_t multi_sample_cancel_load(void)
{
    if (g_multi_load_active == 0U)
    {
        return 0U;
    }

    if ((g_multi_bulk.exclusive != 0U)
        && (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_MULTI_BULK) != 0U))
    {
        multi_loader_bulk_close_file();
        sd_access_gate_release(SD_ACCESS_CLIENT_MULTI_BULK);
    }
    multi_loader_set_error(MULTI_SAMPLE_LOAD_CANCELLED,
                           (g_multi_bulk.current_plan < g_multi_bulk.plan_count)
                               ? g_multi_bulk_plans[g_multi_bulk.current_plan].sample_id
                               : MULTI_SAMPLE_POOL_INVALID_ID);
    return 1U;
}

void multi_sample_get_load_diag(multi_sample_load_diag_t *out_diag)
{
    if (out_diag != 0)
    {
        *out_diag = g_multi_load_diag;
    }
}
