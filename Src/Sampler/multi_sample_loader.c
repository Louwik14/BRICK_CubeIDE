#include "Sampler/multi_sample_loader.h"

#include <string.h>

#include "Sampler/multi_sample_import.h"
#include "Sampler/multi_sample_index.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_page_cache_port.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_stream_transport.h"
#include "Sampler/sample_cache.h"
#include "Seq/seq_runtime.h"
#include "Platform/memory_layout.h"
#include "Storage/audio_recorder.h"
#include "Storage/wav_parser.h"
#include "Storage/storage_io_wakeup.h"
#include "stm32h7xx_hal.h"

#define MULTI_SAMPLE_LOADER_PATH_MAX SAMPLE_PAGE_CACHE_PATH_MAX

typedef struct
{
    uint8_t used;
    uint16_t logical_id;
    uint16_t instrument_id;
    uint8_t cancelled;
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
static multi_sample_load_request_t g_multi_load_request;
SDRAM_MULTI_LOAD static multi_sample_load_request_t g_multi_external_request;
static volatile uint8_t g_multi_external_request_valid;
SDRAM_MULTI_LOAD static multi_sample_load_completion_t g_multi_load_completion;
static volatile uint8_t g_multi_load_completion_valid;
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
    uint16_t plan_count;
    uint16_t current_plan;
    uint32_t started_at_ms;
    uint32_t pending_sequence;
    uint8_t pending;
    uint8_t cancel_requested;
} multi_sample_bulk_state_t;

SDRAM_MULTI_LOAD static multi_sample_bulk_plan_t
    g_multi_bulk_plans[MULTI_SAMPLE_MAX_SAMPLES];
static multi_sample_bulk_state_t g_multi_bulk;
static uint8_t multi_loader_copy_text(char *dst, uint32_t dst_size,
                                      const char *src);

static void multi_loader_publish_completion_for(uint16_t logical_id,
                                                uint16_t instrument_id,
                                                const char *path,
                                                uint8_t success)
{
    if (g_multi_load_completion_valid != 0U)
        return;
    g_multi_load_completion.logical_id = logical_id;
    g_multi_load_completion.instrument_id = instrument_id;
    g_multi_load_completion.success = success;
    (void)multi_loader_copy_text(g_multi_load_completion.path,
                                 sizeof(g_multi_load_completion.path),
                                 path);
    __DMB();
    g_multi_load_completion_valid = 1U;
}

static void multi_loader_publish_completion(uint8_t success)
{
    if (g_multi_load_request.used != 0U)
        multi_loader_publish_completion_for(g_multi_load_request.logical_id,
                                            g_multi_load_request.instrument_id,
                                            g_multi_load_request.path, success);
}

uint8_t multi_sample_load_required_prep_pages(
    const multi_sample_index_t *index, uint32_t *out_pages);

void multi_sample_loader_init(void)
{
    memset(&g_multi_load_diag, 0, sizeof(g_multi_load_diag));
    g_multi_load_active = 0U;
    g_multi_load_first_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
    memset(&g_multi_external_request, 0, sizeof(g_multi_external_request));
    g_multi_external_request_valid = 0U;
    memset(&g_multi_load_completion, 0, sizeof(g_multi_load_completion));
    g_multi_load_completion_valid = 0U;
    memset(g_multi_load_queue, 0, sizeof(g_multi_load_queue));
    memset(&g_multi_bulk, 0, sizeof(g_multi_bulk));
}

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

static multi_sample_load_result_t multi_loader_enqueue(uint16_t logical_id,
                                                       const char *index_path,
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
            return ((g_multi_load_queue[i].logical_id == logical_id)
                    && (strcmp(g_multi_load_queue[i].path, index_path) == 0))
                ? MULTI_SAMPLE_LOAD_OK
                : MULTI_SAMPLE_LOAD_POOL_FAIL;
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

            g_multi_load_queue[i].logical_id = logical_id;
            g_multi_load_queue[i].instrument_id = instrument_id;
            g_multi_load_queue[i].used = 1U;
            (void)multi_sample_pool_set_index_path(instrument_id, index_path);
            (void)multi_sample_pool_set_state(instrument_id,
                                              MULTI_SAMPLE_INSTRUMENT_LOADING);
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

static uint8_t multi_loader_bulk_runtime_stopped(void)
{
    return ((seq_runtime_is_running() == 0U)
            && (seq_runtime_is_start_pending() == 0U)) ? 1U : 0U;
}

static void multi_loader_set_error(multi_sample_load_result_t error,
                                   uint16_t failed_sample)
{
    if (g_multi_bulk.started_at_ms != 0U)
    {
        g_multi_load_diag.elapsed_ms = HAL_GetTick() - g_multi_bulk.started_at_ms;
    }
    g_multi_load_diag.last_error = error;
    g_multi_load_diag.last_failed_sample = failed_sample;
    g_multi_load_diag.state = MULTI_SAMPLE_INSTRUMENT_ERROR;
    multi_loader_publish_completion(0U);
    (void)multi_sample_pool_clear_instrument(g_multi_load_diag.instrument_id);
    g_multi_load_active = 0U;
    memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
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

    const sample_audio_format_t format = sample_audio_format_or_stereo(
        sample_audio_format_from_channels(channels));
    result.start_first = 0U;
    uint32_t start_pages = sample_audio_format_multi_presocle_pages(format);
    const uint32_t total_pages = sample_audio_format_required_page_count(format, total_frames);
    if (start_pages > total_pages)
    {
        start_pages = total_pages;
    }
    result.start_last = start_pages - 1U;
    uint32_t pages = result.start_last + 1U;

#if BRICK6_STREAM_PRODUCT_VOICE_LOOP_CACHE_PAGES == 0U
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
#else
    (void)has_loop;
    (void)loop_begin;
    (void)loop_end;
#endif

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
            if (sample_page_cache_port_reserve_static(
                    key, page, SAMPLE_PAGE_ALLOC_SLOT_PERMANENT) == 0U)
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

    if (multi_sample_load_required_prep_pages(index, &required_pages) == 0U)
    {
        budget.required_pages = UINT16_MAX;
        budget.first_unpreparable_sample = 0U;
        return budget;
    }
    budget.required_pages = (required_pages > UINT16_MAX)
        ? UINT16_MAX : (uint16_t)required_pages;
    if (required_pages <= budget.budget_pages)
        budget.samples_preparable = index->sample_count;
    else
        budget.first_unpreparable_sample = 0U;

    return budget;
}

uint8_t multi_sample_load_required_prep_pages(const multi_sample_index_t *index,
                                              uint32_t *out_pages)
{
    if ((index == NULL) || (out_pages == NULL)) return 0U;
    uint32_t pages = 0U;
    for (uint16_t i = 0U; i < index->sample_count; ++i)
    {
        const multi_sample_index_sample_t *const sample = &index->samples[i];
        const sample_audio_format_t format =
            sample_audio_format_from_channels(sample->channels);
        const uint32_t sample_pages =
            (uint32_t)sample_audio_format_multi_start_slot_cost(format)
            * SAMPLE_PREP_MULTI_START_SLOT_PAGES;
        if ((sample_pages == 0U) || (pages > (UINT32_MAX - sample_pages)))
            return 0U;
        pages += sample_pages;
    }
    *out_pages = pages;
    return 1U;
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
    if (multi_sample_pool_get_state(instrument_id) == MULTI_SAMPLE_INSTRUMENT_RETIRING)
    {
        g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_SD_BUSY;
        return MULTI_SAMPLE_LOAD_SD_BUSY;
    }

    if (multi_loader_bulk_runtime_stopped() == 0U)
    {
        g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_TRANSPORT_ACTIVE;
        return MULTI_SAMPLE_LOAD_TRANSPORT_ACTIVE;
    }

    if ((audio_recorder_is_active() != 0U)
        || (sample_cache_has_pending_sd_work() != 0U)
        || (sample_stream_manager_has_pending_sd_work() != 0U))
    {
        g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_SD_BUSY;
        return MULTI_SAMPLE_LOAD_SD_BUSY;
    }

    multi_sample_index_t index;
    const multi_sample_index_result_t load_result =
        multi_sample_index_load(index_path, &index);
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
        if (sample_page_cache_port_register_path(key, full_path, &info,
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

multi_sample_load_result_t multi_sample_load_instrument(uint16_t logical_id,
                                                        const char *index_path,
                                                        uint16_t instrument_id)
{
    if (g_multi_load_active != 0U)
    {
        const multi_sample_load_result_t queued =
            multi_loader_enqueue(logical_id, index_path, instrument_id);
        return queued;
    }

    memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
    g_multi_load_request.used = 1U;
    g_multi_load_request.logical_id = logical_id;
    g_multi_load_request.instrument_id = instrument_id;
    if (multi_loader_copy_text(g_multi_load_request.path,
                               sizeof(g_multi_load_request.path),
                               index_path) == 0U)
    {
        memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
        return MULTI_SAMPLE_LOAD_PATH_TOO_LONG;
    }
    multi_sample_load_result_t result =
        multi_loader_start_instrument(index_path, instrument_id);
    if (result == MULTI_SAMPLE_LOAD_SD_BUSY)
    {
        const multi_sample_load_result_t queued =
            multi_loader_enqueue(logical_id, index_path, instrument_id);
        memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
        return queued;
    }
    if (result == MULTI_SAMPLE_LOAD_ALREADY_READY)
    {
        multi_loader_publish_completion(1U);
    }
    else if ((result != MULTI_SAMPLE_LOAD_OK)
             && (g_multi_load_completion_valid == 0U))
    {
        multi_loader_publish_completion(0U);
    }
    if ((result != MULTI_SAMPLE_LOAD_OK)
        || (result == MULTI_SAMPLE_LOAD_ALREADY_READY))
        memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
    return result;
}

multi_sample_load_result_t multi_sample_load_request_instrument(uint16_t logical_id,
                                                                const char *index_path,
                                                                uint16_t instrument_id)
{
    if ((index_path == 0) || (index_path[0] == '\0')
        || (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
    {
        return MULTI_SAMPLE_LOAD_INVALID_ARG;
    }
    if (strlen(index_path) >= sizeof(g_multi_external_request.path))
        return MULTI_SAMPLE_LOAD_PATH_TOO_LONG;
    if ((multi_sample_import_is_busy() != 0U)
        || (multi_sample_import_delete_is_busy() != 0U)
        || (multi_sample_pool_clear_is_active() != 0U))
        return MULTI_SAMPLE_LOAD_SD_BUSY;
    if ((g_multi_external_request_valid != 0U)
        || (multi_sample_load_has_pending() != 0U))
    {
        return MULTI_SAMPLE_LOAD_SD_BUSY;
    }
    memset(&g_multi_external_request, 0, sizeof(g_multi_external_request));
    g_multi_external_request.logical_id = logical_id;
    g_multi_external_request.instrument_id = instrument_id;
    if (multi_loader_copy_text(g_multi_external_request.path,
                               sizeof(g_multi_external_request.path),
                               index_path) == 0U)
    {
        return MULTI_SAMPLE_LOAD_PATH_TOO_LONG;
    }
    g_multi_external_request.used = 1U;
    __DMB();
    g_multi_external_request_valid = 1U;
    storage_io_wakeup(STORAGE_IO_WAKE_WORK);
    return MULTI_SAMPLE_LOAD_OK;
}

void multi_sample_load_storage_request_service(void)
{
    if (g_multi_external_request_valid == 0U) return;
    const uint16_t logical_id = g_multi_external_request.logical_id;
    const uint16_t instrument_id = g_multi_external_request.instrument_id;
    char path[MULTI_SAMPLE_LOADER_PATH_MAX];
    (void)multi_loader_copy_text(path, sizeof(path), g_multi_external_request.path);
    g_multi_external_request_valid = 0U;
    g_multi_external_request.used = 0U;
    (void)multi_sample_load_instrument(logical_id, path, instrument_id);
}

static void multi_loader_start_next_queued(void)
{
    if ((g_multi_load_active != 0U)
        || (g_multi_load_completion_valid != 0U))
    {
        return;
    }

    for (uint16_t i = 0U; i < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++i)
    {
        if (g_multi_load_queue[i].used != 0U)
        {
            char path[MULTI_SAMPLE_LOADER_PATH_MAX];
            const uint16_t logical_id = g_multi_load_queue[i].logical_id;
            const uint16_t instrument_id = g_multi_load_queue[i].instrument_id;
            g_multi_load_request = g_multi_load_queue[i];
            if (g_multi_load_queue[i].cancelled != 0U)
            {
                if (g_multi_load_completion_valid == 0U)
                {
                    multi_loader_publish_completion_for(
                        logical_id, instrument_id, g_multi_load_queue[i].path, 0U);
                    g_multi_load_queue[i].used = 0U;
                }
                return;
            }
            (void)multi_loader_copy_text(path, sizeof(path), g_multi_load_queue[i].path);
            multi_sample_load_result_t result =
                multi_loader_start_instrument(path, instrument_id);
            if (result != MULTI_SAMPLE_LOAD_SD_BUSY)
            {
                g_multi_load_queue[i].used = 0U;
                if ((result != MULTI_SAMPLE_LOAD_OK)
                    && (result != MULTI_SAMPLE_LOAD_ALREADY_READY))
                {
                    if (g_multi_load_completion_valid == 0U)
                        multi_loader_publish_completion_for(logical_id, instrument_id,
                                                            path, 0U);
                    (void)multi_sample_pool_clear_instrument(instrument_id);
                }
                else if (result == MULTI_SAMPLE_LOAD_ALREADY_READY)
                {
                    multi_loader_publish_completion_for(logical_id, instrument_id,
                                                        path, 1U);
                }
                if (result != MULTI_SAMPLE_LOAD_OK)
                    memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
            }
            else
                memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
            return;
        }
    }
}

static uint8_t multi_loader_bulk_read_step(multi_sample_bulk_plan_t *plan,
                                           const multi_sample_desc_t *sample)
{
    if ((plan == 0) || (sample == 0))
    {
        return 0U;
    }

    const uint32_t range_last = plan->range_last[plan->current_range];
    const sample_audio_key_t key = sample_audio_key_multi(plan->sample_id);

    if (g_multi_bulk.pending != 0U)
    {
        sample_stream_io_result_t result;
        if (sample_stream_transport_take_result(
                g_multi_bulk.pending_sequence, &result) == 0U)
        {
            return 1U;
        }
        g_multi_bulk.pending = 0U;
        if (g_multi_bulk.cancel_requested != 0U)
        {
            (void)sample_page_cache_port_complete(&result);
            sample_stream_io_release_key(key);
            multi_loader_set_error(MULTI_SAMPLE_LOAD_CANCELLED,
                                   plan->sample_id);
            return 1U;
        }
        g_multi_load_diag.read_calls++;
        g_multi_load_diag.file_opens += result.file_opens;
        g_multi_load_diag.seeks += result.seeks;
        g_multi_load_diag.physical_reads += result.physical_reads;
        g_multi_load_diag.bytes_read += result.source_bytes;
        if (result.backend != 0U)
        {
            g_multi_load_diag.physical_bytes += result.read_bytes;
            if (result.read_bytes > g_multi_load_diag.max_read_bytes)
            {
                g_multi_load_diag.max_read_bytes = result.read_bytes;
            }
        }
        g_multi_load_diag.decode_cycles += result.decode_cycles;
        const uint8_t published = sample_page_cache_port_complete(&result);
        if ((result.load_result != SAMPLE_PAGE_LOAD_OK) || (published == 0U))
            return 0U;

        if (plan->pages_remaining != 0U)
            plan->pages_remaining--;
        if (g_multi_load_diag.pages_remaining != 0U)
            g_multi_load_diag.pages_remaining--;
        g_multi_load_diag.pages_ready++;
        plan->next_page++;
        if (plan->next_page > range_last)
        {
            plan->current_range++;
            if (plan->current_range < plan->range_count)
                plan->next_page = plan->range_first[plan->current_range];
        }
        return 1U;
    }

    if (plan->next_page > range_last)
    {
        return (plan->pages_remaining == 0U) ? 1U : 0U;
    }

    if (sample_stream_transport_can_submit() == 0U)
        return 1U;

    const uint32_t start_frame = plan->next_page * sample->frames_per_page;
    if (start_frame >= sample->total_frames)
        return 0U;
    sample_stream_io_command_t command;
    if (sample_page_cache_port_prepare_page(
            key, plan->next_page, SAMPLE_PAGE_ALLOC_SLOT_PERMANENT, 0U,
            &command) == 0U)
        return 0U;
    if (sample_stream_transport_submit(
            &command, &g_multi_bulk.pending_sequence) == 0U)
    {
        sample_page_cache_port_abort(&command);
        return 0U;
    }
    g_multi_bulk.pending = 1U;
    return 1U;
}

static uint8_t multi_loader_bulk_finish_instrument(void)
{
    const multi_sample_instrument_t *const instrument =
        multi_sample_pool_get_instrument(g_multi_load_diag.instrument_id);
    if ((instrument == 0)
        || (sample_global_pool_register_multi(g_multi_load_diag.instrument_id,
                                              instrument->index_path,
                                              (uint32_t)g_multi_load_diag.prep_pages_required
                                                  * SAMPLE_PAGE_BYTES,
                                              instrument->sample_count,
                                              0) == 0U))
    {
        return 0U;
    }

    g_multi_load_active = 0U;
    g_multi_load_diag.elapsed_ms = HAL_GetTick() - g_multi_bulk.started_at_ms;
    g_multi_load_diag.state = MULTI_SAMPLE_INSTRUMENT_READY;
    g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_OK;
    (void)multi_sample_pool_set_state(g_multi_load_diag.instrument_id,
                                      MULTI_SAMPLE_INSTRUMENT_READY);
    multi_loader_publish_completion(1U);
    memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
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

    if ((g_multi_bulk.pending == 0U)
        && (multi_loader_bulk_runtime_stopped() == 0U))
    {
        multi_loader_set_error(MULTI_SAMPLE_LOAD_TRANSPORT_ACTIVE,
                               (g_multi_bulk.current_plan < g_multi_bulk.plan_count)
                                   ? g_multi_bulk_plans[g_multi_bulk.current_plan].sample_id
                                   : MULTI_SAMPLE_POOL_INVALID_ID);
        return;
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

    g_multi_load_diag.service_passes++;
    g_multi_load_diag.saved_page_checks +=
        (uint32_t)g_multi_load_diag.total_samples + g_multi_load_diag.pages_requested;

    multi_sample_bulk_plan_t *const plan =
        &g_multi_bulk_plans[g_multi_bulk.current_plan];
    const multi_sample_desc_t *const sample = multi_sample_pool_get_sample(plan->sample_id);
    const uint8_t ok = (sample != 0)
        && (multi_loader_bulk_read_step(plan, sample) != 0U);

    if ((ok != 0U) && (plan->pages_remaining == 0U))
    {
        sample_stream_io_release_key(sample_audio_key_multi(plan->sample_id));
        g_multi_bulk.current_plan++;
        g_multi_load_diag.samples_ready++;
        if (g_multi_load_diag.samples_remaining != 0U)
        {
            g_multi_load_diag.samples_remaining--;
        }
    }
    if (ok == 0U)
    {
        sample_stream_io_release_key(sample_audio_key_multi(plan->sample_id));
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
    if ((g_multi_load_active != 0U)
        || (g_multi_external_request_valid != 0U)
        || (g_multi_load_completion_valid != 0U))
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

uint8_t multi_sample_load_is_active(void)
{
    return g_multi_load_active;
}

uint8_t multi_sample_load_take_completion(
    multi_sample_load_completion_t *out_completion)
{
    if ((out_completion == 0U) || (g_multi_load_completion_valid == 0U))
        return 0U;
    *out_completion = g_multi_load_completion;
    __DMB();
    g_multi_load_completion_valid = 0U;
    return 1U;
}

uint8_t multi_sample_cancel_load(void)
{
    if (g_multi_load_active == 0U)
    {
        return 0U;
    }

    if (g_multi_bulk.current_plan < g_multi_bulk.plan_count)
    {
        sample_stream_io_release_key(sample_audio_key_multi(
            g_multi_bulk_plans[g_multi_bulk.current_plan].sample_id));
    }
    if (g_multi_bulk.pending != 0U)
    {
        g_multi_bulk.cancel_requested = 1U;
        return 1U;
    }
    multi_loader_set_error(MULTI_SAMPLE_LOAD_CANCELLED,
                           (g_multi_bulk.current_plan < g_multi_bulk.plan_count)
                               ? g_multi_bulk_plans[g_multi_bulk.current_plan].sample_id
                               : MULTI_SAMPLE_POOL_INVALID_ID);
    return 1U;
}

void multi_sample_cancel_all_loads(void)
{
    (void)multi_sample_cancel_load();
    g_multi_external_request_valid = 0U;
    memset(&g_multi_external_request, 0, sizeof(g_multi_external_request));
    for (uint16_t i = 0U; i < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++i)
    {
        if (g_multi_load_queue[i].used != 0U)
        {
            g_multi_load_queue[i].cancelled = 1U;
            (void)multi_sample_pool_clear_instrument(
                g_multi_load_queue[i].instrument_id);
        }
    }
}

void multi_sample_get_load_diag(multi_sample_load_diag_t *out_diag)
{
    if (out_diag != 0)
    {
        *out_diag = g_multi_load_diag;
    }
}
