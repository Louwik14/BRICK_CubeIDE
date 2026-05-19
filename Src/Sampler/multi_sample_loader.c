#include "Sampler/multi_sample_loader.h"

#include <string.h>

#include "Sampler/multi_sample_index.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_cache.h"
#include "Storage/looper_storage.h"
#include "Storage/memory_layout.h"
#include "Storage/multi_record_writer.h"
#include "Storage/sd_access_gate.h"
#include "Storage/wav_parser.h"

#define MULTI_SAMPLE_LOADER_PATH_MAX SAMPLE_PAGE_CACHE_PATH_MAX

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
MULTI_LOAD_SDRAM static multi_sample_load_request_t
    g_multi_load_queue[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];

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

static void multi_loader_set_error(multi_sample_load_result_t error,
                                   uint16_t failed_sample)
{
    g_multi_load_diag.last_error = error;
    g_multi_load_diag.last_failed_sample = failed_sample;
    g_multi_load_diag.state = MULTI_SAMPLE_INSTRUMENT_ERROR;
    (void)multi_sample_pool_set_state(g_multi_load_diag.instrument_id,
                                      MULTI_SAMPLE_INSTRUMENT_ERROR);
    g_multi_load_active = 0U;
}

static uint8_t multi_loader_sample_required_pages(uint32_t total_frames)
{
    if (total_frames == 0U)
    {
        return 0U;
    }

    const uint32_t contract_frames =
        (total_frames < SAMPLE_PREP_MIN_READY_FRAMES) ? total_frames
                                                      : SAMPLE_PREP_MIN_READY_FRAMES;
    uint32_t pages = (contract_frames + SAMPLE_PAGE_FRAMES - 1U) / SAMPLE_PAGE_FRAMES;
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
    return (uint8_t)pages;
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
        const uint16_t pages =
            (uint16_t)multi_loader_sample_required_pages(index->samples[i].total_frames);
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

    if (multi_sample_index_apply_to_pool(&index, instrument_id) != MULTI_SAMPLE_INDEX_OK)
    {
        g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_POOL_FAIL;
        (void)multi_sample_pool_set_state(instrument_id, MULTI_SAMPLE_INSTRUMENT_ERROR);
        g_multi_load_diag.state = MULTI_SAMPLE_INSTRUMENT_ERROR;
        return MULTI_SAMPLE_LOAD_POOL_FAIL;
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

    const multi_sample_instrument_t *const instrument =
        multi_sample_pool_get_instrument(instrument_id);
    if ((instrument == 0) || (instrument->sample_count != index.sample_count)
        || (instrument->sample_count == 0U))
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
        if (sample_page_cache_register_stream_sample_key(key,
                                                         full_path,
                                                         &info,
                                                         sample->total_frames,
                                                         sample->data_offset) == 0U)
        {
            multi_loader_set_error(MULTI_SAMPLE_LOAD_REGISTER_FAIL, multi_sample_id);
            return MULTI_SAMPLE_LOAD_REGISTER_FAIL;
        }

        const uint8_t required_pages =
            multi_loader_sample_required_pages(sample->total_frames);
        for (uint8_t page = 0U; page < required_pages; ++page)
        {
            if (sample_stream_manager_request_page_key(key, page) == 0U)
            {
                multi_loader_set_error(MULTI_SAMPLE_LOAD_NOT_ENOUGH_CACHE, multi_sample_id);
                return MULTI_SAMPLE_LOAD_NOT_ENOUGH_CACHE;
            }
            g_multi_load_diag.pages_requested++;
        }
    }

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

void multi_sample_service_load(uint32_t byte_budget)
{
    if (g_multi_load_active == 0U)
    {
        multi_loader_start_next_queued();
        return;
    }

    uint16_t ready_pages = 0U;
    uint16_t required_pages_total = 0U;
    for (uint16_t i = 0U; i < g_multi_load_diag.total_samples; ++i)
    {
        const uint16_t multi_sample_id = (uint16_t)(g_multi_load_first_sample_id + i);
        const sample_audio_key_t key = sample_audio_key_multi(multi_sample_id);
        const multi_sample_desc_t *const sample =
            multi_sample_pool_get_sample(multi_sample_id);
        if ((sample == 0) || (sample->total_frames == 0U))
        {
            multi_loader_set_error(MULTI_SAMPLE_LOAD_POOL_FAIL, multi_sample_id);
            g_multi_load_diag.pages_ready = ready_pages;
            return;
        }

        const uint8_t required_pages =
            multi_loader_sample_required_pages(sample->total_frames);
        uint8_t sample_ready = 1U;
        required_pages_total += required_pages;
        for (uint8_t page = 0U; page < required_pages; ++page)
        {
            const sample_page_state_t state = sample_page_cache_get_page_state_key(key, page);
            if (state == SAMPLE_PAGE_READY)
            {
                ready_pages++;
            }
            else
            {
                sample_ready = 0U;
                if (state == SAMPLE_PAGE_ERROR)
                {
                    sample_stream_manager_release_key(key);
                    multi_loader_set_error(MULTI_SAMPLE_LOAD_PAGE_ERROR, multi_sample_id);
                    g_multi_load_diag.pages_ready = ready_pages;
                    return;
                }
            }
        }

        if (sample_ready != 0U)
        {
            sample_stream_manager_release_key(key);
        }
    }

    g_multi_load_diag.pages_ready = ready_pages;
    if ((required_pages_total != 0U) && (ready_pages >= required_pages_total))
    {
        g_multi_load_active = 0U;
        g_multi_load_diag.state = MULTI_SAMPLE_INSTRUMENT_READY;
        g_multi_load_diag.last_error = MULTI_SAMPLE_LOAD_OK;
        (void)multi_sample_pool_set_state(g_multi_load_diag.instrument_id,
                                          MULTI_SAMPLE_INSTRUMENT_READY);
        multi_loader_start_next_queued();
        return;
    }

    if ((byte_budget == 0U)
        || (multi_record_writer_any_active() != 0U)
        || (looper_storage_raw_export_is_active() != 0U))
    {
        return;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        return;
    }
    sample_stream_manager_service(byte_budget);
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
}

uint8_t multi_sample_is_ready(uint16_t instrument_id)
{
    return (multi_sample_pool_get_state(instrument_id) == MULTI_SAMPLE_INSTRUMENT_READY)
        ? 1U
        : 0U;
}

void multi_sample_get_load_diag(multi_sample_load_diag_t *out_diag)
{
    if (out_diag != 0)
    {
        *out_diag = g_multi_load_diag;
    }
}
