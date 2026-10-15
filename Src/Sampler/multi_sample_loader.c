#include "Sampler/multi_sample_loader.h"

#include <string.h>

#include "Sampler/multi_sample_index.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_page_cache_port.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_stream_fatfs_map.h"
#include "Sampler/sample_cache.h"
#include "Storage/project_control.h"
#include "Storage/sd_access_gate.h"
#include "Seq/seq_runtime.h"
#include "Platform/memory_layout.h"
#include "Storage/audio_recorder.h"
#include "Storage/wav_parser.h"

#define MULTI_SAMPLE_LOADER_PATH_MAX SAMPLE_PAGE_CACHE_PATH_MAX
#define MULTI_SAMPLE_PREP_WORK_QUANTUM_BYTES (8U * 1024U)

typedef struct
{
    uint8_t used;
    uint16_t logical_id;
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

static multi_sample_load_status_t g_multi_load_status;
static uint8_t g_multi_load_active;
static uint16_t g_multi_register_sample_id;
static multi_sample_load_request_t g_multi_load_request;
SDRAM_MULTI_LOAD static multi_sample_load_request_t
    g_multi_load_queue[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];

typedef struct
{
    uint16_t sample_id;
    uint16_t page_count;
    uint32_t next_page;
} multi_sample_prefill_plan_t;

typedef struct
{
    uint16_t plan_count;
    uint16_t current_plan;
} multi_sample_prefill_state_t;

SDRAM_MULTI_LOAD static multi_sample_prefill_plan_t
    g_multi_prefill_plans[MULTI_SAMPLE_MAX_SAMPLES];
static multi_sample_prefill_state_t g_multi_prefill;
static uint16_t g_multi_register_index;
static uint8_t g_multi_index_pending;
static uint8_t g_multi_index_ready;
SDRAM_MULTI_LOAD static multi_sample_index_t g_multi_index_value;
#define g_multi_sample_file (*sample_stream_coop_file_shared())
static uint8_t g_multi_sample_file_active;
SDRAM_MULTI_LOAD static char g_multi_register_path[MULTI_SAMPLE_LOADER_PATH_MAX];

static uint8_t multi_loader_complete_already_ready(uint16_t logical_id,
                                                    const char *index_path,
                                                    uint16_t instrument_id)
{
    const project_control_asset_result_t completion =
        project_control_complete_multi_runtime(logical_id, index_path,
                                               instrument_id, 1U);
    if (completion != PROJECT_CONTROL_ASSET_READY)
    {
        g_multi_load_status.last_error = MULTI_SAMPLE_LOAD_REGISTER_FAIL;
        return 0U;
    }
    return 1U;
}

uint8_t multi_sample_load_required_prep_pages(
    const multi_sample_index_t *index, uint32_t *out_pages);

void multi_sample_loader_init(void)
{
    memset(&g_multi_load_status, 0, sizeof(g_multi_load_status));
    g_multi_load_active = 0U;
    g_multi_register_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
    memset(g_multi_load_queue, 0, sizeof(g_multi_load_queue));
    memset(&g_multi_prefill, 0, sizeof(g_multi_prefill));
    g_multi_register_index = 0U;
    g_multi_index_pending = 0U;
    g_multi_index_ready = 0U;
    memset(&g_multi_index_value, 0, sizeof(g_multi_index_value));
    sample_stream_coop_file_init(&g_multi_sample_file);
    g_multi_sample_file_active = 0U;
    memset(g_multi_register_path, 0, sizeof(g_multi_register_path));
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

static uint8_t multi_loader_join_index_sample_path(
    char *out,
    uint32_t out_size,
    const char *index_path,
    const char *relative_path,
    uint16_t relative_len)
{
    if ((out == 0) || (index_path == 0) || (relative_path == 0)
        || (out_size == 0U) || (relative_len == 0U)) return 0U;
    const char *slash = strrchr(index_path, '/');
    const char *backslash = strrchr(index_path, '\\');
    if ((slash == 0) || ((backslash != 0) && (backslash > slash)))
        slash = backslash;
    if ((slash == 0) || (slash == index_path)) return 0U;
    const uint32_t base_len = (uint32_t)(slash - index_path);
    const uint32_t total = base_len + 1U + relative_len;
    if (total >= out_size) return 0U;
    memcpy(out, index_path, base_len);
    out[base_len] = '/';
    memcpy(&out[base_len + 1U], relative_path, relative_len);
    out[total] = '\0';
    return 1U;
}

uint8_t multi_sample_resolve_index_sample_path(
    const char *index_path,
    const multi_sample_index_t *index,
    uint16_t sample_index,
    char *out,
    uint32_t out_size)
{
    if ((index == NULL) || (sample_index >= index->sample_count))
    {
        return 0U;
    }
    const multi_sample_index_sample_t *const sample =
        &index->samples[sample_index];
    return multi_loader_join_index_sample_path(
        out, out_size, index_path, &index->strings[sample->path_offset],
        sample->path_len);
}

static wav_info_t multi_loader_wav_info_from_pool_sample(
    const multi_sample_desc_t *sample)
{
    wav_info_t info;
    memset(&info, 0, sizeof(info));
    if (sample != 0)
    {
        info.audio_format = 3U;
        info.encoding = WAV_SAMPLE_ENCODING_IEEE_FLOAT;
        info.sample_rate = 48000U;
        info.channels = sample->stride_floats;
        info.bits_per_sample = 32U;
        info.valid_bits_per_sample = 32U;
        info.fmt_chunk_size = 16U;
        info.has_fact = 1U;
        info.fact_sample_length = sample->total_frames;
        info.block_align = (uint16_t)(sample->stride_floats * sizeof(float));
        info.byte_rate = 48000U * info.block_align;
        info.data_offset = 512U;
        info.data_size = sample->total_frames * info.block_align;
    }
    return info;
}

static uint8_t multi_loader_runtime_stopped(void)
{
    return ((seq_runtime_is_running() == 0U)
            && (seq_runtime_is_start_pending() == 0U)) ? 1U : 0U;
}

static void multi_loader_set_error(multi_sample_load_result_t error,
                                   uint16_t failed_sample)
{
    (void)failed_sample;
    g_multi_load_status.last_error = error;
    g_multi_load_status.state = MULTI_SAMPLE_INSTRUMENT_ERROR;
    if (g_multi_load_request.used != 0U)
    {
        project_control_complete_multi_runtime(g_multi_load_request.logical_id,
                                               g_multi_load_request.path,
                                               g_multi_load_status.instrument_id,
                                               0U);
    }
    (void)multi_sample_pool_clear_instrument(g_multi_load_status.instrument_id);
    if (g_multi_sample_file_active != 0U)
    {
        sample_stream_coop_file_close(&g_multi_sample_file);
        sample_stream_coop_file_init(&g_multi_sample_file);
        g_multi_sample_file_active = 0U;
    }
    g_multi_index_pending = 0U;
    g_multi_index_ready = 0U;
    g_multi_load_active = 0U;
    memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
    memset(&g_multi_prefill, 0, sizeof(g_multi_prefill));
    g_multi_register_index = 0U;
}

static multi_sample_prep_budget_t multi_loader_calc_prep_budget(
    const multi_sample_index_t *index)
{
    multi_sample_prep_budget_t budget = {
        .budget_pages = SAMPLE_PAGE_SLOT_POOL_COUNT,
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
        const uint32_t sample_pages = ((sample->channels == 1U)
                                       || (sample->channels == 2U))
            ? SAMPLE_PAGE_MIN_READY_PAGES : 0U;
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
    memset(&g_multi_load_status, 0, sizeof(g_multi_load_status));
    g_multi_load_status.instrument_id = instrument_id;
    g_multi_load_status.last_error = MULTI_SAMPLE_LOAD_OK;
    g_multi_load_status.state = multi_sample_pool_get_state(instrument_id);
    g_multi_load_active = 0U;
    g_multi_register_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    memset(&g_multi_prefill, 0, sizeof(g_multi_prefill));
    g_multi_register_index = 0U;

    if ((index_path == 0) || (index_path[0] == '\0')
        || (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
    {
        g_multi_load_status.last_error = MULTI_SAMPLE_LOAD_INVALID_ARG;
        return MULTI_SAMPLE_LOAD_INVALID_ARG;
    }

    if (multi_sample_pool_get_state(instrument_id) == MULTI_SAMPLE_INSTRUMENT_READY)
    {
        g_multi_load_status.last_error = MULTI_SAMPLE_LOAD_ALREADY_READY;
        g_multi_load_status.state = MULTI_SAMPLE_INSTRUMENT_READY;
        return MULTI_SAMPLE_LOAD_ALREADY_READY;
    }
    if (multi_sample_pool_get_state(instrument_id) == MULTI_SAMPLE_INSTRUMENT_RETIRING)
    {
        g_multi_load_status.last_error = MULTI_SAMPLE_LOAD_SD_BUSY;
        return MULTI_SAMPLE_LOAD_SD_BUSY;
    }

    if ((multi_loader_runtime_stopped() == 0U) && (g_multi_index_ready == 0U))
    {
        if (audio_recorder_is_active() != 0U)
        {
            g_multi_load_status.last_error = MULTI_SAMPLE_LOAD_SD_BUSY;
            return MULTI_SAMPLE_LOAD_SD_BUSY;
        }
        if (multi_sample_index_load_cooperative_begin(index_path) == 0U)
        {
            g_multi_load_status.last_error = MULTI_SAMPLE_LOAD_SD_BUSY;
            return MULTI_SAMPLE_LOAD_SD_BUSY;
        }
        g_multi_index_pending = 1U;
        g_multi_load_active = 1U;
        g_multi_load_status.state = MULTI_SAMPLE_INSTRUMENT_LOADING;
        return MULTI_SAMPLE_LOAD_OK;
    }

    if ((audio_recorder_is_active() != 0U)
        || ((multi_loader_runtime_stopped() != 0U)
            && ((sample_cache_has_pending_sd_work() != 0U)
                || (sample_stream_manager_has_pending_sd_work() != 0U))))
    {
        g_multi_load_status.last_error = MULTI_SAMPLE_LOAD_SD_BUSY;
        return MULTI_SAMPLE_LOAD_SD_BUSY;
    }

    multi_sample_index_t index;
    const multi_sample_index_result_t load_result = g_multi_index_ready != 0U
        ? MULTI_SAMPLE_INDEX_OK : multi_sample_index_load(index_path, &index);
    if (g_multi_index_ready != 0U)
    {
        index = g_multi_index_value;
        g_multi_index_ready = 0U;
    }
    if (load_result != MULTI_SAMPLE_INDEX_OK)
    {
        g_multi_load_status.last_error = (load_result == MULTI_SAMPLE_INDEX_LIMIT)
            ? MULTI_SAMPLE_LOAD_INDEX_LIMIT
            : MULTI_SAMPLE_LOAD_INDEX_FAIL;
        return g_multi_load_status.last_error;
    }

    const multi_sample_index_result_t apply_result =
        multi_sample_index_apply_to_pool(&index, instrument_id);
    if (apply_result != MULTI_SAMPLE_INDEX_OK)
    {
        g_multi_load_status.last_error =
            (apply_result == MULTI_SAMPLE_INDEX_FORMAT_MISMATCH)
                ? MULTI_SAMPLE_LOAD_FORMAT_MISMATCH
                : MULTI_SAMPLE_LOAD_POOL_FAIL;
        (void)multi_sample_pool_set_state(instrument_id, MULTI_SAMPLE_INSTRUMENT_ERROR);
        g_multi_load_status.state = MULTI_SAMPLE_INSTRUMENT_ERROR;
        return g_multi_load_status.last_error;
    }
    (void)multi_sample_pool_set_index_path(instrument_id, index_path);

    const multi_sample_prep_budget_t prep_budget =
        multi_loader_calc_prep_budget(&index);
    g_multi_load_status.prep_pages_required = prep_budget.required_pages;
    if ((prep_budget.required_pages > prep_budget.budget_pages)
        || (prep_budget.samples_preparable < index.sample_count))
       
    {
        multi_loader_set_error(
            MULTI_SAMPLE_LOAD_PREP_BUDGET_EXCEEDED,
            MULTI_SAMPLE_POOL_INVALID_ID);
        return MULTI_SAMPLE_LOAD_PREP_BUDGET_EXCEEDED;
    }

    const uint32_t product_cost_bytes =
        (uint32_t)prep_budget.required_pages
            * sample_audio_format_page_bytes(index.format);
    uint16_t existing_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if ((sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_MULTI,
                                            instrument_id,
                                            &existing_global) == 0U)
        && (sample_global_pool_find_free_slot() == SAMPLE_GLOBAL_POOL_INVALID_INDEX))
    {
        g_multi_load_status.last_error = MULTI_SAMPLE_LOAD_POOL_FAIL;
        return MULTI_SAMPLE_LOAD_POOL_FAIL;
    }
    if (sample_global_pool_validate_budget(SAMPLE_GLOBAL_KIND_MULTI,
                                           instrument_id,
                                           product_cost_bytes) == 0U)
    {
        g_multi_load_status.last_error = MULTI_SAMPLE_LOAD_PREP_BUDGET_EXCEEDED;
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

    g_multi_register_sample_id = instrument->sample_head_id;
    g_multi_load_status.state = MULTI_SAMPLE_INSTRUMENT_LOADING;
    (void)multi_sample_pool_set_state(instrument_id, MULTI_SAMPLE_INSTRUMENT_LOADING);

    g_multi_register_index = 0U;
    g_multi_prefill.plan_count = 0U;
    g_multi_prefill.current_plan = 0U;
    g_multi_load_active = 1U;
    return MULTI_SAMPLE_LOAD_OK;
}

multi_sample_load_result_t multi_sample_load_instrument(uint16_t logical_id,
                                                        const char *index_path,
                                                        uint16_t instrument_id)
{
    if (project_control_begin_multi_runtime(logical_id,index_path,instrument_id) == 0U)
    {
        return MULTI_SAMPLE_LOAD_INVALID_ARG;
    }
    if (g_multi_load_active != 0U)
    {
        const multi_sample_load_result_t queued =
            multi_loader_enqueue(logical_id, index_path, instrument_id);
        if (queued != MULTI_SAMPLE_LOAD_OK)
            project_control_complete_multi_runtime(logical_id,index_path,instrument_id,0U);
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
        project_control_complete_multi_runtime(logical_id,index_path,instrument_id,0U);
        return MULTI_SAMPLE_LOAD_PATH_TOO_LONG;
    }
    multi_sample_load_result_t result =
        multi_loader_start_instrument(index_path, instrument_id);
    if (result == MULTI_SAMPLE_LOAD_SD_BUSY)
    {
        const multi_sample_load_result_t queued =
            multi_loader_enqueue(logical_id, index_path, instrument_id);
        memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
        if (queued != MULTI_SAMPLE_LOAD_OK)
            project_control_complete_multi_runtime(logical_id,index_path,instrument_id,0U);
        return queued;
    }
    uint8_t completion_done = 0U;
    if (result == MULTI_SAMPLE_LOAD_ALREADY_READY)
    {
        completion_done = 1U;
        if (multi_loader_complete_already_ready(
                logical_id, index_path, instrument_id) == 0U)
            result = MULTI_SAMPLE_LOAD_REGISTER_FAIL;
    }
    if ((completion_done == 0U)
        && (result != MULTI_SAMPLE_LOAD_OK)
        && (result != MULTI_SAMPLE_LOAD_ALREADY_READY))
    {
        (void)project_control_complete_multi_runtime(
            logical_id,index_path,instrument_id,0U);
        (void)multi_sample_pool_clear_instrument(instrument_id);
    }
    if (result != MULTI_SAMPLE_LOAD_OK)
        memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
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
            const uint16_t logical_id = g_multi_load_queue[i].logical_id;
            const uint16_t instrument_id = g_multi_load_queue[i].instrument_id;
            (void)multi_loader_copy_text(path, sizeof(path), g_multi_load_queue[i].path);
            g_multi_load_request = g_multi_load_queue[i];
            multi_sample_load_result_t result =
                multi_loader_start_instrument(path, instrument_id);
            if (result != MULTI_SAMPLE_LOAD_SD_BUSY)
            {
                g_multi_load_queue[i].used = 0U;
                if ((result != MULTI_SAMPLE_LOAD_OK)
                    && (result != MULTI_SAMPLE_LOAD_ALREADY_READY))
                {
                    project_control_complete_multi_runtime(logical_id,path,instrument_id,0U);
                    (void)multi_sample_pool_clear_instrument(instrument_id);
                }
                else if (result == MULTI_SAMPLE_LOAD_ALREADY_READY
                         && (multi_loader_complete_already_ready(
                             logical_id, path, instrument_id) == 0U))
                {
                    result = MULTI_SAMPLE_LOAD_REGISTER_FAIL;
                    (void)multi_sample_pool_clear_instrument(instrument_id);
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

/* 0=error, 1=registered, 2=cooperative work pending. */
static uint8_t multi_loader_register_next_sample(void)
{
    const multi_sample_instrument_t *const instrument =
        multi_sample_pool_get_instrument(g_multi_load_status.instrument_id);
    if ((instrument == 0)
        || (g_multi_register_index >= instrument->sample_count)
        || (g_multi_register_index >= MULTI_SAMPLE_MAX_SAMPLES))
    {
        return 0U;
    }
    const uint16_t index = g_multi_register_index;
    const uint16_t sample_id = g_multi_register_sample_id;
    const multi_sample_desc_t *const sample = multi_sample_pool_get_sample(sample_id);
    if (sample == 0) return 0U;
    char full_path[MULTI_SAMPLE_LOADER_PATH_MAX];
    if (multi_loader_join_index_sample_path(
            full_path, sizeof(full_path), instrument->index_path, sample->path,
            (uint16_t)strlen(sample->path)) == 0U)
    {
        return 0U;
    }
    const sample_audio_key_t key = multi_sample_pool_sample_key(sample_id);
    const wav_info_t info = multi_loader_wav_info_from_pool_sample(sample);
    sample_page_stream_info_t stream;
    uint16_t prefill_pages = 0U;
    sample_stream_manager_release_key(key);
    if (multi_loader_runtime_stopped() != 0U)
    {
        if (sample_page_cache_port_prepare_stream(
                key, full_path, &info, sample->total_frames, 512U,
                NULL, &stream, &prefill_pages) == 0U)
            return 0U;
    }
    else
    {
        if (g_multi_sample_file_active == 0U)
        {
            if (multi_loader_copy_text(g_multi_register_path,
                    sizeof(g_multi_register_path), full_path) == 0U
                || sample_stream_coop_file_begin(
                    &g_multi_sample_file, full_path) == 0U)
                return 0U;
            g_multi_sample_file_active = 1U;
            return 2U;
        }
        sample_stream_coop_file_service(&g_multi_sample_file, 16U);
        if (g_multi_sample_file.state == SAMPLE_STREAM_COOP_ERROR)
            return 0U;
        if (g_multi_sample_file.state != SAMPLE_STREAM_COOP_READY)
            return 2U;
        sample_stream_physical_map_t map;
        uint32_t file_size = 0U;
        memset(&map, 0, sizeof(map));
        if (sample_stream_coop_file_take_map(
                &g_multi_sample_file, &map, &file_size) == 0U)
            return 0U;
        if (sample_page_cache_port_prepare_stream_mapped(
                key, g_multi_register_path, &info, sample->total_frames,
                512U, &map, file_size, &stream,
                &prefill_pages) == 0U)
        {
            sample_stream_physical_map_release(&map);
            return 0U;
        }
        sample_stream_coop_file_close(&g_multi_sample_file);
        sample_stream_coop_file_init(&g_multi_sample_file);
        g_multi_sample_file_active = 0U;
    }
    if ((stream.format != sample->format)
        || (stream.stride_floats != sample->stride_floats)
        || (stream.frames_per_page != sample->frames_per_page)
        || (prefill_pages == 0U))
    {
        sample_page_cache_port_clear(key);
        return 0U;
    }
    if (multi_sample_pool_set_sample_registration_epoch(
            sample_id, stream.registration_epoch) == 0U)
    {
        sample_page_cache_port_clear(key);
        return 0U;
    }
    g_multi_prefill_plans[index] = (multi_sample_prefill_plan_t){
        .sample_id = sample_id,
        .page_count = prefill_pages,
        .next_page = 0U,
    };
    g_multi_prefill.plan_count++;
    g_multi_load_status.pages_requested = (uint16_t)(
        g_multi_load_status.pages_requested + prefill_pages);
    g_multi_register_sample_id = multi_sample_pool_next_sample_id(sample_id);
    g_multi_register_index++;
    return 1U;
}

static uint8_t multi_loader_finish_instrument(void)
{
    const multi_sample_instrument_t *const instrument =
        multi_sample_pool_get_instrument(g_multi_load_status.instrument_id);
    if ((instrument == 0)
        || (sample_global_pool_register_multi(g_multi_load_status.instrument_id,
                                              instrument->index_path,
                                              (uint32_t)g_multi_load_status.prep_pages_required
                                                  * sample_audio_format_page_bytes(
                                                      instrument->format),
                                              instrument->sample_count,
                                              0) == 0U))
    {
        return 0U;
    }

    if (multi_sample_pool_set_state(g_multi_load_status.instrument_id,
            MULTI_SAMPLE_INSTRUMENT_READY) == 0U
        || project_control_complete_multi_runtime(
            g_multi_load_request.logical_id, g_multi_load_request.path,
            g_multi_load_status.instrument_id, 1U) != PROJECT_CONTROL_ASSET_READY)
        return 0U;
    g_multi_load_active = 0U;
    g_multi_load_status.state = MULTI_SAMPLE_INSTRUMENT_READY;
    g_multi_load_status.last_error = MULTI_SAMPLE_LOAD_OK;
    memset(&g_multi_load_request, 0, sizeof(g_multi_load_request));
    memset(&g_multi_prefill, 0, sizeof(g_multi_prefill));
    g_multi_register_index = 0U;
    multi_loader_start_next_queued();
    return 1U;
}

void multi_sample_service_load(uint32_t byte_budget)
{
    if (g_multi_load_active == 0U)
    {
        multi_loader_start_next_queued();
        return;
    }

    if (g_multi_index_pending != 0U)
    {
        multi_sample_index_load_cooperative_service(16U);
        multi_sample_index_result_t index_result = MULTI_SAMPLE_INDEX_OK;
        if (multi_sample_index_load_cooperative_take_result(
                &g_multi_index_value, &index_result) == 0U) return;
        g_multi_index_pending = 0U;
        if (index_result != MULTI_SAMPLE_INDEX_OK)
        {
            multi_loader_set_error(
                (index_result == MULTI_SAMPLE_INDEX_LIMIT)
                    ? MULTI_SAMPLE_LOAD_INDEX_LIMIT
                    : MULTI_SAMPLE_LOAD_INDEX_FAIL,
                MULTI_SAMPLE_POOL_INVALID_ID);
            return;
        }
        g_multi_index_ready = 1U;
        g_multi_load_active = 0U;
        const multi_sample_load_result_t result = multi_loader_start_instrument(
            g_multi_load_request.path, g_multi_load_request.instrument_id);
        if (result != MULTI_SAMPLE_LOAD_OK)
            multi_loader_set_error(result, MULTI_SAMPLE_POOL_INVALID_ID);
        return;
    }
    if (byte_budget == 0U) return;

    const multi_sample_instrument_t *instrument =
        multi_sample_pool_get_instrument(g_multi_load_status.instrument_id);
    if (instrument == 0)
    {
        multi_loader_set_error(MULTI_SAMPLE_LOAD_POOL_FAIL,
                               MULTI_SAMPLE_POOL_INVALID_ID);
        return;
    }
    uint32_t work_items = (byte_budget + MULTI_SAMPLE_PREP_WORK_QUANTUM_BYTES - 1U)
        / MULTI_SAMPLE_PREP_WORK_QUANTUM_BYTES;
    while ((work_items != 0U)
        && (g_multi_register_index < instrument->sample_count))
    {
        if ((sample_cache_has_pending_sd_work() != 0U)
            || (sd_access_gate_current_owner() != SD_ACCESS_CLIENT_NONE))
        {
            return;
        }
        const uint8_t register_result = multi_loader_register_next_sample();
        if (register_result == 0U)
        {
            multi_loader_set_error(MULTI_SAMPLE_LOAD_REGISTER_FAIL,
                g_multi_register_sample_id);
            return;
        }
        if (register_result == 2U) return;
        --work_items;
    }
    if (g_multi_register_index < instrument->sample_count) return;

    uint32_t page_checks = (byte_budget + SAMPLE_PAGE_BYTES - 1U)
        / SAMPLE_PAGE_BYTES;
    while ((page_checks != 0U)
        && (g_multi_prefill.current_plan < g_multi_prefill.plan_count))
    {
        multi_sample_prefill_plan_t *const plan =
            &g_multi_prefill_plans[g_multi_prefill.current_plan];
        const sample_page_state_t state = sample_page_cache_get_page_state_key(
            multi_sample_pool_sample_key(plan->sample_id), plan->next_page);
        if ((state == SAMPLE_PAGE_RESERVED) || (state == SAMPLE_PAGE_LOADING))
            return;
        if (state != SAMPLE_PAGE_READY)
        {
            multi_loader_set_error(MULTI_SAMPLE_LOAD_PAGE_ERROR, plan->sample_id);
            return;
        }
        ++g_multi_load_status.pages_ready;
        ++plan->next_page;
        --page_checks;
        if (plan->next_page >= plan->page_count)
            ++g_multi_prefill.current_plan;
    }
    if (g_multi_prefill.current_plan >= g_multi_prefill.plan_count)
    {
        if (multi_loader_finish_instrument() == 0U)
            multi_loader_set_error(MULTI_SAMPLE_LOAD_POOL_FAIL,
                                   MULTI_SAMPLE_POOL_INVALID_ID);
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

uint8_t multi_sample_load_is_active(void)
{
    return g_multi_load_active;
}

uint8_t multi_sample_cancel_load(void)
{
    if (g_multi_load_active == 0U)
    {
        return 0U;
    }

    multi_loader_set_error(MULTI_SAMPLE_LOAD_CANCELLED,
                           (g_multi_prefill.current_plan < g_multi_prefill.plan_count)
                               ? g_multi_prefill_plans[g_multi_prefill.current_plan].sample_id
                               : MULTI_SAMPLE_POOL_INVALID_ID);
    return 1U;
}

void multi_sample_cancel_all_loads(void)
{
    (void)multi_sample_cancel_load();
    for (uint16_t i = 0U; i < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++i)
    {
        if (g_multi_load_queue[i].used != 0U)
        {
            project_control_complete_multi_runtime(g_multi_load_queue[i].logical_id,
                                                   g_multi_load_queue[i].path,
                                                   g_multi_load_queue[i].instrument_id,
                                                   0U);
            (void)multi_sample_pool_clear_instrument(
                g_multi_load_queue[i].instrument_id);
        }
    }
    memset(g_multi_load_queue, 0, sizeof(g_multi_load_queue));
}

void multi_sample_get_load_status(multi_sample_load_status_t *out_status)
{
    if (out_status != 0)
    {
        *out_status = g_multi_load_status;
    }
}
