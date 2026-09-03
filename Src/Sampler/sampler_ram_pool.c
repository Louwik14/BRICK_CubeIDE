#include "Sampler/sampler_ram_pool.h"

#include <string.h>
#include "stm32h7xx.h"

#include "ff.h"
#include "Platform/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "SD/sd_scheduler_runtime.h"
#include "Storage/wav_parser.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_page_cache_port.h"
#include "IPC/sampler_ram_audio_projection_control.h"
#include "ControlRT/control_rt_publication.h"
#include "IPC/control_audio_timing.h"

#define SAMPLER_RAM_IO_BYTES (8192U)
#define SAMPLER_RAM_WAVEFORM_DEFAULT_SERVICE_FRAMES (4096U)

typedef struct
{
    sampler_ram_slot_t slots[SAMPLER_RAM_POOL_MAX_SLOTS];
    sampler_ram_result_t last_result;
    uint32_t generation_counter;
} sampler_ram_pool_state_t;

STORAGE_STATE_SDRAM static sampler_ram_pool_state_t g_sampler_ram_pool;
AUDIO_WARM ALIGN32 static uint8_t g_sampler_ram_io[SAMPLER_RAM_IO_BYTES];
static volatile uint8_t g_sampler_ram_clear_request_valid;
static volatile uint16_t g_sampler_ram_clear_request_slot;

typedef enum
{
    SAMPLER_RAM_LOAD_IDLE = 0,
    SAMPLER_RAM_LOAD_MOUNT,
    SAMPLER_RAM_LOAD_OPEN,
    SAMPLER_RAM_LOAD_PARSE,
    SAMPLER_RAM_LOAD_ALLOCATE,
    SAMPLER_RAM_LOAD_SEEK,
    SAMPLER_RAM_LOAD_READ,
    SAMPLER_RAM_LOAD_CONVERT,
    SAMPLER_RAM_LOAD_CLOSE,
    SAMPLER_RAM_LOAD_WAIT_RETIRE,
    SAMPLER_RAM_LOAD_PUBLISH,
    SAMPLER_RAM_LOAD_DONE
} sampler_ram_load_state_t;

typedef struct
{
    sampler_ram_load_state_t state;
    FIL file;
    wav_info_t info;
    sample_page_loader_allocation_t allocation;
    sampler_ram_slot_t candidate;
    char path[SAMPLER_RAM_POOL_PATH_MAX];
    uint32_t media_epoch;
    uint32_t frames_done;
    uint32_t buffered_frames;
    uint32_t converted_frames;
    uint16_t ram_slot;
    uint16_t global_slot;
    sampler_ram_result_t result;
    uint8_t file_open;
    uint8_t failed;
    uint8_t prepared;
    uint32_t prepared_file_size;
    uint32_t prepared_expected_crc32;
    uint32_t prepared_data_bytes;
    uint32_t prepared_page_count;
    uint32_t prepared_cost_bytes;
} sampler_ram_load_job_t;

STORAGE_STATE_SDRAM static sampler_ram_load_job_t g_sampler_ram_load_job;
static volatile uint8_t g_sampler_ram_load_request_valid;
static uint16_t g_sampler_ram_load_request_slot;
static char g_sampler_ram_load_request_path[SAMPLER_RAM_POOL_PATH_MAX];
static uint8_t sampler_ram_copy_path(char *dst, uint32_t dst_size, const char *src);

static void sampler_ram_load_job_boot_init(void)
{
    memset(&g_sampler_ram_load_job, 0, sizeof(g_sampler_ram_load_job));
    g_sampler_ram_load_job.state = SAMPLER_RAM_LOAD_IDLE;
    g_sampler_ram_load_request_valid = 0U;
}

void sampler_ram_pool_load_async_cancel(void)
{
    sampler_ram_load_job_t *const job = &g_sampler_ram_load_job;
    if (job->file_open != 0U)
    {
        (void)f_close(&job->file);
        job->file_open = 0U;
    }
    if (job->allocation.page_count != 0U)
    {
        sample_page_cache_port_release_shared(job->allocation.first_slot,
                                              job->allocation.page_count);
    }
    memset(job, 0, sizeof(*job));
    job->state = SAMPLER_RAM_LOAD_IDLE;
}

uint8_t sampler_ram_pool_request_clear(uint16_t ram_slot)
{
    if ((ram_slot >= SAMPLER_RAM_POOL_MAX_SLOTS)
        || (sampler_ram_pool_load_async_busy() != 0U)
        || (g_sampler_ram_clear_request_valid != 0U))
    {
        return 0U;
    }
    g_sampler_ram_clear_request_slot = ram_slot;
    __DMB();
    g_sampler_ram_clear_request_valid = 1U;
    return 1U;
}

void sampler_ram_pool_storage_request_service(void)
{
    if (g_sampler_ram_clear_request_valid != 0U)
    {
        const uint16_t slot = g_sampler_ram_clear_request_slot;
        g_sampler_ram_clear_request_valid = 0U;
        sampler_ram_pool_clear(slot);
        return;
    }
    if (g_sampler_ram_load_request_valid != 0U)
    {
        const uint16_t slot = g_sampler_ram_load_request_slot;
        char path[SAMPLER_RAM_POOL_PATH_MAX];
        (void)sampler_ram_copy_path(path, sizeof(path), g_sampler_ram_load_request_path);
        g_sampler_ram_load_request_valid = 0U;
        (void)sampler_ram_pool_load_async_begin(slot, path);
    }
}
static CTRL_STATE uint64_t
    g_sampler_ram_retire_not_before_sample[SAMPLER_RAM_POOL_MAX_SLOTS];
static CTRL_STATE uint8_t
    g_sampler_ram_retire_stop_committed[SAMPLER_RAM_POOL_MAX_SLOTS];
static CTRL_STATE uint8_t g_sampler_ram_retire_invariant_failed;

uint16_t sampler_ram_format_channels(sampler_ram_format_t format)
{
    switch (format)
    {
        case SAMPLER_RAM_FORMAT_FLOAT32_MONO:
            return 1U;
        case SAMPLER_RAM_FORMAT_FLOAT32_STEREO_INTERLEAVED:
            return 2U;
        default:
            return 0U;
    }
}

uint16_t sampler_ram_format_bytes_per_frame(sampler_ram_format_t format)
{
    const uint16_t channels = sampler_ram_format_channels(format);
    return (channels != 0U) ? (uint16_t)(channels * sizeof(float)) : 0U;
}

uint8_t sampler_ram_frames_to_bytes(sampler_ram_format_t format,
                                    uint32_t frames,
                                    uint32_t *out_bytes)
{
    if (out_bytes != 0)
    {
        *out_bytes = 0U;
    }

    const uint16_t bytes_per_frame = sampler_ram_format_bytes_per_frame(format);
    const uint64_t bytes = (uint64_t)frames * bytes_per_frame;
    if ((bytes_per_frame == 0U) || (bytes > UINT32_MAX))
    {
        return 0U;
    }

    if (out_bytes != 0)
    {
        *out_bytes = (uint32_t)bytes;
    }
    return 1U;
}

uint8_t sampler_ram_bytes_to_pages(uint32_t bytes, uint32_t *out_pages)
{
    if (out_pages != 0)
    {
        *out_pages = 0U;
    }
    if (bytes == 0U)
    {
        return 0U;
    }

    const uint64_t pages = ((uint64_t)bytes + SAMPLE_PAGE_BYTES - 1U)
                           / SAMPLE_PAGE_BYTES;
    if ((pages == 0U) || (pages > UINT32_MAX))
    {
        return 0U;
    }
    if (out_pages != 0)
    {
        *out_pages = (uint32_t)pages;
    }
    return 1U;
}

uint8_t sampler_ram_format_cost_bytes(sampler_ram_format_t format,
                                      uint32_t frames,
                                      uint32_t *out_logical_bytes,
                                      uint32_t *out_page_count,
                                      uint32_t *out_cost_bytes)
{
    if (out_logical_bytes != 0)
    {
        *out_logical_bytes = 0U;
    }
    if (out_page_count != 0)
    {
        *out_page_count = 0U;
    }
    if (out_cost_bytes != 0)
    {
        *out_cost_bytes = 0U;
    }

    uint32_t logical_bytes = 0U;
    uint32_t page_count = 0U;
    if ((sampler_ram_frames_to_bytes(format, frames, &logical_bytes) == 0U)
        || (sampler_ram_bytes_to_pages(logical_bytes, &page_count) == 0U))
    {
        return 0U;
    }

    const uint64_t cost = (uint64_t)page_count * SAMPLE_PAGE_BYTES;
    if (cost > UINT32_MAX)
    {
        return 0U;
    }
    if (out_logical_bytes != 0)
    {
        *out_logical_bytes = logical_bytes;
    }
    if (out_page_count != 0)
    {
        *out_page_count = page_count;
    }
    if (out_cost_bytes != 0)
    {
        *out_cost_bytes = (uint32_t)cost;
    }
    return 1U;
}

static uint16_t sampler_ram_waveform_abs_i16(int16_t value)
{
    if (value >= 0)
    {
        return (uint16_t)value;
    }
    if (value == (int16_t)-32768)
    {
        return 32768U;
    }
    return (uint16_t)(-value);
}

static int16_t sampler_ram_waveform_float_to_i16(float value)
{
    if (value >= 1.0f)
    {
        return 32767;
    }
    if (value <= -1.0f)
    {
        return -32768;
    }
    if (value >= 0.0f)
    {
        return (int16_t)((value * 32767.0f) + 0.5f);
    }
    return (int16_t)((value * 32768.0f) - 0.5f);
}

static void sampler_ram_waveform_set_empty(sampler_ram_slot_t *slot)
{
    if (slot == 0)
    {
        return;
    }
    memset(&slot->waveform, 0, sizeof(slot->waveform));
    slot->waveform.state = SAMPLE_RAM_WAVEFORM_EMPTY;
}

static void sampler_ram_waveform_set_error(sampler_ram_slot_t *slot)
{
    if (slot == 0)
    {
        return;
    }
    memset(&slot->waveform, 0, sizeof(slot->waveform));
    slot->waveform.state = SAMPLE_RAM_WAVEFORM_ERROR;
    slot->waveform.sample_generation = slot->generation;
}

static uint8_t sampler_ram_waveform_slot_ready(const sampler_ram_slot_t *slot)
{
    return (uint8_t)((slot != 0)
                    && (slot->state == SAMPLER_RAM_SLOT_READY)
                    && (sampler_ram_format_channels(slot->format) != 0U)
                    && (slot->data != 0)
                    && (slot->frames != 0U)
                    && (slot->channels != 0U));
}

static void sampler_ram_waveform_begin(sampler_ram_slot_t *slot)
{
    if (slot == 0)
    {
        return;
    }

    memset(&slot->waveform, 0, sizeof(slot->waveform));
    slot->waveform.state = SAMPLE_RAM_WAVEFORM_BUILDING;
    slot->waveform.sample_generation = slot->generation;
    slot->waveform.frame_count = slot->frames;
    slot->waveform.channels = (uint8_t)slot->channels;
    slot->waveform.format = (uint8_t)slot->format;
    slot->waveform.columns = SAMPLE_RAM_WAVEFORM_COLUMNS;
    for (uint16_t i = 0U; i < SAMPLE_RAM_WAVEFORM_COLUMNS; ++i)
    {
        slot->waveform.min[i] = 32767;
        slot->waveform.max[i] = -32768;
    }
}

static uint8_t sampler_ram_waveform_matches_slot(const sampler_ram_slot_t *slot)
{
    if (slot == 0)
    {
        return 0U;
    }

    const sample_ram_waveform_overview_t *const waveform = &slot->waveform;
    return (uint8_t)((waveform->state != SAMPLE_RAM_WAVEFORM_EMPTY)
                    && (waveform->state != SAMPLE_RAM_WAVEFORM_ERROR)
                    && (waveform->sample_generation == slot->generation)
                    && (waveform->frame_count == slot->frames)
                    && (waveform->channels == (uint8_t)slot->channels)
                    && (waveform->format == (uint8_t)slot->format)
                    && (waveform->columns == SAMPLE_RAM_WAVEFORM_COLUMNS));
}

static void sampler_ram_waveform_finish(sampler_ram_slot_t *slot)
{
    sample_ram_waveform_overview_t *const waveform = &slot->waveform;
    uint16_t peak = 0U;

    for (uint16_t col = 0U; col < waveform->columns; ++col)
    {
        if (waveform->min[col] > waveform->max[col])
        {
            waveform->min[col] = 0;
            waveform->max[col] = 0;
        }

        const uint16_t min_peak = sampler_ram_waveform_abs_i16(waveform->min[col]);
        const uint16_t max_peak = sampler_ram_waveform_abs_i16(waveform->max[col]);
        if (min_peak > peak)
        {
            peak = min_peak;
        }
        if (max_peak > peak)
        {
            peak = max_peak;
        }
    }

    waveform->ready_columns = waveform->columns;
    waveform->global_peak = peak;
    waveform->build_next_column = waveform->columns;
    waveform->build_next_frame = waveform->frame_count;
    waveform->state = SAMPLE_RAM_WAVEFORM_READY;
}

static void sampler_ram_waveform_service_slot(sampler_ram_slot_t *slot,
                                              uint32_t *frames_left)
{
    if ((slot == 0) || (frames_left == 0) || (*frames_left == 0U))
    {
        return;
    }

    if (sampler_ram_waveform_slot_ready(slot) == 0U)
    {
        if (slot->state == SAMPLER_RAM_SLOT_ERROR)
        {
            sampler_ram_waveform_set_error(slot);
        }
        else
        {
            sampler_ram_waveform_set_empty(slot);
        }
        return;
    }

    if (sampler_ram_waveform_matches_slot(slot) == 0U)
    {
        sampler_ram_waveform_begin(slot);
    }

    sample_ram_waveform_overview_t *const waveform = &slot->waveform;
    if (waveform->state != SAMPLE_RAM_WAVEFORM_BUILDING)
    {
        return;
    }

    while ((*frames_left > 0U) && (waveform->build_next_frame < slot->frames))
    {
        const uint32_t frame = waveform->build_next_frame;
        uint32_t col = (uint32_t)(((uint64_t)frame * waveform->columns) / slot->frames);
        if (col >= waveform->columns)
        {
            col = waveform->columns - 1U;
        }

        const float *const src = &slot->data[frame * slot->channels];
        const int16_t left = sampler_ram_waveform_float_to_i16(src[0]);
        int16_t right = left;
        if (slot->channels > 1U)
        {
            right = sampler_ram_waveform_float_to_i16(src[1]);
        }

        const int16_t frame_min = (left < right) ? left : right;
        const int16_t frame_max = (left > right) ? left : right;
        if (frame_min < waveform->min[col])
        {
            waveform->min[col] = frame_min;
        }
        if (frame_max > waveform->max[col])
        {
            waveform->max[col] = frame_max;
        }

        const uint16_t min_peak = sampler_ram_waveform_abs_i16(frame_min);
        const uint16_t max_peak = sampler_ram_waveform_abs_i16(frame_max);
        if (min_peak > waveform->global_peak)
        {
            waveform->global_peak = min_peak;
        }
        if (max_peak > waveform->global_peak)
        {
            waveform->global_peak = max_peak;
        }

        waveform->build_next_frame++;
        (*frames_left)--;

        if (waveform->build_next_frame < slot->frames)
        {
            uint32_t next_col =
                (uint32_t)(((uint64_t)waveform->build_next_frame * waveform->columns)
                           / slot->frames);
            if (next_col > waveform->columns)
            {
                next_col = waveform->columns;
            }
            waveform->build_next_column = next_col;
            waveform->ready_columns = (uint16_t)next_col;
        }
    }

    if (waveform->build_next_frame >= slot->frames)
    {
        sampler_ram_waveform_finish(slot);
    }
}

static void sampler_ram_set_last(sampler_ram_result_t result)
{
    g_sampler_ram_pool.last_result = result;
}

static uint32_t sampler_ram_next_generation(void)
{
    g_sampler_ram_pool.generation_counter++;
    if (g_sampler_ram_pool.generation_counter == 0U)
    {
        g_sampler_ram_pool.generation_counter = 1U;
    }
    return g_sampler_ram_pool.generation_counter;
}

static uint8_t sampler_ram_copy_path(char *dst, uint32_t dst_size, const char *src)
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

static uint8_t sampler_ram_wav_supported(const wav_info_t *info)
{
    if (info == 0)
    {
        return 0U;
    }
    if (!((info->audio_format == 1U) || (info->audio_format == 65534U)))
    {
        return 0U;
    }
    if (!((info->channels == 1U) || (info->channels == 2U)))
    {
        return 0U;
    }
    if (!((info->bits_per_sample == 16U) || (info->bits_per_sample == 24U)))
    {
        return 0U;
    }
    const uint16_t expected_align =
        (uint16_t)((info->channels * info->bits_per_sample) / 8U);
    return (info->block_align == expected_align) ? 1U : 0U;
}

uint8_t sampler_ram_pool_inspect_wav(const wav_info_t *info,
                                     uint32_t *out_frames,
                                     uint32_t *out_data_bytes,
                                     uint32_t *out_page_count,
                                     uint32_t *out_cost_bytes)
{
    if ((sampler_ram_wav_supported(info) == 0U)
        || (info->data_size == 0U)
        || ((info->data_size % info->block_align) != 0U))
    {
        return 0U;
    }

    const uint32_t frames = info->data_size / info->block_align;
    uint32_t logical_bytes = 0U;
    uint32_t page_count = 0U;
    uint32_t cost_bytes = 0U;
    const sampler_ram_format_t format = (info->channels == 1U)
        ? SAMPLER_RAM_FORMAT_FLOAT32_MONO
        : SAMPLER_RAM_FORMAT_FLOAT32_STEREO_INTERLEAVED;
    if ((sampler_ram_format_cost_bytes(format, frames, &logical_bytes,
                                       &page_count, &cost_bytes) == 0U)
        || (cost_bytes > SAMPLER_RAM_POOL_BYTES))
    {
        return 0U;
    }
    if (out_frames != 0) *out_frames = frames;
    if (out_data_bytes != 0) *out_data_bytes = logical_bytes;
    if (out_page_count != 0) *out_page_count = page_count;
    if (out_cost_bytes != 0) *out_cost_bytes = cost_bytes;
    return 1U;
}

static float sampler_ram_pcm16_to_float(const uint8_t *p)
{
    const int16_t v = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    return (float)v * (1.0f / 32768.0f);
}

static float sampler_ram_pcm24_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0]
                          | ((uint32_t)p[1] << 8)
                          | ((uint32_t)p[2] << 16));
    if ((v & 0x00800000L) != 0)
    {
        v |= (int32_t)0xFF000000L;
    }
    return (float)v * (1.0f / 8388608.0f);
}

void sampler_ram_pool_init(void)
{
    sampler_ram_load_job_boot_init();
    g_sampler_ram_clear_request_valid = 0U;
    (void)sampler_ram_pool_reset_quiesced();
}

uint8_t sampler_ram_pool_reset_quiesced(void)
{
    for (uint16_t i = 0U; i < SAMPLER_RAM_POOL_MAX_SLOTS; ++i)
    {
        if ((g_sampler_ram_pool.slots[i].state == SAMPLER_RAM_SLOT_READY)
            || (g_sampler_ram_pool.slots[i].state == SAMPLER_RAM_SLOT_RETIRING))
        {
            return 0U;
        }
    }
    sampler_ram_pool_load_async_cancel();
    sampler_ram_audio_projection_init();
    memset(g_sampler_ram_retire_not_before_sample, 0,
           sizeof(g_sampler_ram_retire_not_before_sample));
    memset(g_sampler_ram_retire_stop_committed, 0,
           sizeof(g_sampler_ram_retire_stop_committed));
    g_sampler_ram_retire_invariant_failed = 0U;
    uint32_t generation_seed = g_sampler_ram_pool.generation_counter;
    for (uint16_t i = 0U; i < SAMPLER_RAM_POOL_MAX_SLOTS; ++i)
    {
        g_sampler_ram_pool.slots[i].generation = sampler_ram_next_generation();
        if (g_sampler_ram_pool.slots[i].page_count != 0U)
        {
            sample_page_cache_port_release_shared(
                g_sampler_ram_pool.slots[i].first_page_slot,
                g_sampler_ram_pool.slots[i].page_count);
        }
        sample_global_pool_clear_backend(SAMPLE_GLOBAL_KIND_RAM, i);
    }
    generation_seed = g_sampler_ram_pool.generation_counter;
    memset(&g_sampler_ram_pool, 0, sizeof(g_sampler_ram_pool));
    g_sampler_ram_pool.generation_counter = (generation_seed == 0U) ? 1U : generation_seed;
    for (uint16_t i = 0U; i < SAMPLER_RAM_POOL_MAX_SLOTS; ++i)
    {
        g_sampler_ram_pool.slots[i].state = SAMPLER_RAM_SLOT_EMPTY;
        g_sampler_ram_pool.slots[i].global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        g_sampler_ram_pool.slots[i].error = SAMPLER_RAM_RESULT_OK;
        g_sampler_ram_pool.slots[i].generation = sampler_ram_next_generation();
        sampler_ram_waveform_set_empty(&g_sampler_ram_pool.slots[i]);
    }
    sampler_ram_set_last(SAMPLER_RAM_RESULT_OK);
    return 1U;
}

uint16_t sampler_ram_pool_find_free_slot(void)
{
    for (uint16_t i = 0U; i < SAMPLER_RAM_POOL_MAX_SLOTS; ++i)
    {
        if (g_sampler_ram_pool.slots[i].state == SAMPLER_RAM_SLOT_EMPTY)
        {
            return i;
        }
    }
    return SAMPLER_RAM_POOL_INVALID_SLOT;
}

static sd_scheduler_background_admission_t sampler_ram_load_sd_begin(
    sd_scheduler_background_kind_t kind, uint32_t bytes)
{
    const sd_scheduler_background_request_t request = {
        .byte_count = bytes,
        .media_epoch = g_sampler_ram_load_job.media_epoch,
        .kind = kind
    };
    return sd_scheduler_runtime_background_try_begin(&request);
}

static void sampler_ram_load_fail(sampler_ram_result_t result)
{
    sampler_ram_load_job_t *const job = &g_sampler_ram_load_job;
    if (job->allocation.page_count != 0U)
    {
        sample_page_cache_port_release_shared(job->allocation.first_slot,
                                                       job->allocation.page_count);
        memset(&job->allocation, 0, sizeof(job->allocation));
    }
    job->result = result;
    job->failed = 1U;
    job->state = (job->file_open != 0U) ? SAMPLER_RAM_LOAD_CLOSE : SAMPLER_RAM_LOAD_DONE;
    sampler_ram_set_last(result);
}

static uint8_t sampler_ram_pool_load_async_begin_internal(
    uint16_t ram_slot, const char *path, const wav_info_t *prepared_info)
{
    sampler_ram_load_job_t *const job = &g_sampler_ram_load_job;
    if ((job->state != SAMPLER_RAM_LOAD_IDLE)
        || (ram_slot >= SAMPLER_RAM_POOL_MAX_SLOTS)
        || (path == 0) || (path[0] == '\0'))
    {
        return 0U;
    }
    memset(job, 0, sizeof(*job));
    if (sampler_ram_copy_path(job->path, sizeof(job->path), path) == 0U)
    {
        job->state = SAMPLER_RAM_LOAD_DONE;
        job->result = SAMPLER_RAM_RESULT_PATH_TOO_LONG;
        return 0U;
    }
    job->ram_slot = ram_slot;
    job->global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    job->media_epoch = sd_access_media_epoch();
    if (prepared_info != NULL)
    {
        job->info = *prepared_info;
        job->prepared = 1U;
    }
    job->result = SAMPLER_RAM_RESULT_OK;
    job->state = SAMPLER_RAM_LOAD_MOUNT;
    return 1U;
}

uint8_t sampler_ram_pool_request_load(uint16_t ram_slot, const char *path)
{
    if ((ram_slot >= SAMPLER_RAM_POOL_MAX_SLOTS)
        || (path == 0) || (path[0] == '\0')
        || (strlen(path) >= sizeof(g_sampler_ram_load_request_path))
        || (sampler_ram_pool_load_async_busy() != 0U)
        || (g_sampler_ram_load_request_valid != 0U))
    {
        return 0U;
    }
    g_sampler_ram_load_request_slot = ram_slot;
    (void)sampler_ram_copy_path(g_sampler_ram_load_request_path,
                                sizeof(g_sampler_ram_load_request_path), path);
    __DMB();
    g_sampler_ram_load_request_valid = 1U;
    return 1U;
}

uint8_t sampler_ram_pool_load_async_begin(uint16_t ram_slot, const char *path)
{
    return sampler_ram_pool_load_async_begin_internal(ram_slot, path, NULL);
}

uint8_t sampler_ram_pool_load_async_begin_prepared(uint16_t ram_slot,
                                                   const char *path,
                                                   const wav_info_t *info,
                                                   uint32_t source_file_size,
                                                   uint32_t source_crc32,
                                                   uint32_t data_bytes,
                                                   uint32_t page_count,
                                                   uint32_t cost_bytes)
{
    if ((info == NULL) || (source_file_size == 0U)) return 0U;
    if (sampler_ram_pool_load_async_begin_internal(ram_slot, path, info) == 0U)
        return 0U;
    g_sampler_ram_load_job.prepared_file_size = source_file_size;
    g_sampler_ram_load_job.prepared_expected_crc32 = source_crc32;
    g_sampler_ram_load_job.prepared_data_bytes = data_bytes;
    g_sampler_ram_load_job.prepared_page_count = page_count;
    g_sampler_ram_load_job.prepared_cost_bytes = cost_bytes;
    return 1U;
}

uint8_t sampler_ram_pool_load_async_busy(void)
{
    return ((g_sampler_ram_load_job.state != SAMPLER_RAM_LOAD_IDLE)
            && (g_sampler_ram_load_job.state != SAMPLER_RAM_LOAD_DONE)) ? 1U : 0U;
}

void sampler_ram_pool_load_async_service(void)
{
    sampler_ram_load_job_t *const job = &g_sampler_ram_load_job;
    if ((job->state == SAMPLER_RAM_LOAD_IDLE) || (job->state == SAMPLER_RAM_LOAD_DONE))
    {
        return;
    }

    if ((job->state == SAMPLER_RAM_LOAD_ALLOCATE)
        || (job->state == SAMPLER_RAM_LOAD_CONVERT)
        || (job->state == SAMPLER_RAM_LOAD_WAIT_RETIRE)
        || (job->state == SAMPLER_RAM_LOAD_PUBLISH))
    {
        if (job->state == SAMPLER_RAM_LOAD_ALLOCATE)
        {
            const uint32_t frames = job->info.data_size / job->info.block_align;
            const sampler_ram_format_t format = (job->info.channels == 1U)
                ? SAMPLER_RAM_FORMAT_FLOAT32_MONO
                : SAMPLER_RAM_FORMAT_FLOAT32_STEREO_INTERLEAVED;
            uint32_t data_bytes = 0U;
            uint32_t page_count = 0U;
            uint32_t cost = 0U;
            if (job->prepared != 0U)
            {
                data_bytes = job->prepared_data_bytes;
                page_count = job->prepared_page_count;
                cost = job->prepared_cost_bytes;
            }
            else if (sampler_ram_format_cost_bytes(format, frames, &data_bytes,
                                                   &page_count, &cost) == 0U)
            {
                sampler_ram_load_fail(SAMPLER_RAM_RESULT_TOO_LARGE);
                return;
            }
            if ((frames == 0U) || (data_bytes == 0U) || (page_count == 0U)
                || (cost == 0U) || (cost > SAMPLER_RAM_POOL_BYTES))
            {
                sampler_ram_load_fail(SAMPLER_RAM_RESULT_TOO_LARGE);
                return;
            }
            if ((sample_global_pool_validate_entries(SAMPLE_GLOBAL_KIND_RAM,
                                                     job->ram_slot, 1U) == 0U))
            {
                sampler_ram_load_fail(SAMPLER_RAM_RESULT_GLOBAL_SLOT_FULL);
                return;
            }
            if (sample_global_pool_validate_budget(SAMPLE_GLOBAL_KIND_RAM,
                                                   job->ram_slot, cost) == 0U)
            {
                sampler_ram_load_fail(SAMPLER_RAM_RESULT_GLOBAL_BUDGET_FULL);
                return;
            }
            if (sample_page_cache_port_alloc_shared(data_bytes, &job->allocation) == 0U)
            {
                sampler_ram_load_fail(SAMPLER_RAM_RESULT_RAM_POOL_FULL);
                return;
            }
            sampler_ram_slot_t *const candidate = &job->candidate;
            memset(candidate, 0, sizeof(*candidate));
            candidate->state = SAMPLER_RAM_SLOT_LOADING;
            candidate->global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
            (void)sampler_ram_copy_path(candidate->path, sizeof(candidate->path), job->path);
            candidate->format = format;
            candidate->channels = sampler_ram_format_channels(format);
            candidate->sample_rate = job->info.sample_rate;
            candidate->frames = frames;
            candidate->bytes_per_frame = sampler_ram_format_bytes_per_frame(format);
            candidate->data_offset = (uint32_t)job->allocation.first_slot * SAMPLE_PAGE_BYTES;
            candidate->first_page_slot = job->allocation.first_slot;
            candidate->page_count = job->allocation.page_count;
            candidate->data_bytes = data_bytes;
            candidate->cost_bytes_aligned = job->allocation.capacity_bytes;
            candidate->data = (float *)sample_page_cache_port_resolve_shared(
                &job->allocation);
            candidate->error = SAMPLER_RAM_RESULT_OK;
            job->state = SAMPLER_RAM_LOAD_SEEK;
            return;
        }

        if (job->state == SAMPLER_RAM_LOAD_CONVERT)
        {
            sampler_ram_slot_t *const candidate = &job->candidate;
            uint32_t count = job->buffered_frames - job->converted_frames;
            if (count > 256U)
            {
                count = 256U;
            }
            const uint8_t *src = &g_sampler_ram_io[
                job->converted_frames * job->info.block_align];
            float *dst = &candidate->data[
                (job->frames_done + job->converted_frames) * candidate->channels];
            for (uint32_t i = 0U; i < count; ++i)
            {
                dst[i * candidate->channels] = (job->info.bits_per_sample == 16U)
                    ? sampler_ram_pcm16_to_float(src)
                    : sampler_ram_pcm24_to_float(src);
                src += (job->info.bits_per_sample == 16U) ? 2U : 3U;
                if (candidate->channels == 2U)
                {
                    dst[(i * candidate->channels) + 1U] =
                        (job->info.bits_per_sample == 16U)
                            ? sampler_ram_pcm16_to_float(src)
                            : sampler_ram_pcm24_to_float(src);
                    src += (job->info.bits_per_sample == 16U) ? 2U : 3U;
                }
            }
            job->converted_frames += count;
            if (job->converted_frames >= job->buffered_frames)
            {
                job->frames_done += job->buffered_frames;
                job->buffered_frames = 0U;
                job->converted_frames = 0U;
                job->state = (job->frames_done >= candidate->frames)
                    ? SAMPLER_RAM_LOAD_CLOSE : SAMPLER_RAM_LOAD_READ;
            }
            return;
        }

        sampler_ram_slot_t *const old = &g_sampler_ram_pool.slots[job->ram_slot];
        if (job->state == SAMPLER_RAM_LOAD_WAIT_RETIRE)
        {
            sampler_ram_pool_service_retire();
            if (old->state != SAMPLER_RAM_SLOT_EMPTY) return;
            job->state = SAMPLER_RAM_LOAD_PUBLISH;
        }
        if (old->state == SAMPLER_RAM_SLOT_READY)
        {
            sampler_ram_pool_clear(job->ram_slot);
            job->state = SAMPLER_RAM_LOAD_WAIT_RETIRE;
            return;
        }
        if (old->state == SAMPLER_RAM_SLOT_RETIRING)
        {
            job->state = SAMPLER_RAM_LOAD_WAIT_RETIRE;
            return;
        }
        sampler_ram_slot_t old_snapshot = *old;
        uint16_t global_slot = old_snapshot.global_slot;
        const uint8_t registered = (global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
            ? sample_global_pool_register_ram_at(global_slot, job->ram_slot,
                                                 job->path, job->allocation.capacity_bytes)
            : sample_global_pool_register_ram(job->ram_slot, job->path,
                                              job->allocation.capacity_bytes, &global_slot);
        if (registered == 0U)
        {
            sampler_ram_load_fail(SAMPLER_RAM_RESULT_REGISTER_FAIL);
            return;
        }
        job->candidate.global_slot = global_slot;
        job->candidate.generation = sampler_ram_next_generation();
        job->candidate.state = SAMPLER_RAM_SLOT_READY;
        sampler_ram_waveform_begin(&job->candidate);
        const uint32_t primask = __get_PRIMASK();
        __disable_irq();
        *old = job->candidate;
        __DMB();
        if (primask == 0U)
        {
            __enable_irq();
        }
        (void)sampler_ram_audio_projection_publish(job->ram_slot, old);
        memset(&job->allocation, 0, sizeof(job->allocation));
        if (old_snapshot.page_count != 0U)
        {
            sample_page_cache_port_release_shared(old_snapshot.first_page_slot,
                                                           old_snapshot.page_count);
        }
        job->global_slot = global_slot;
        job->result = SAMPLER_RAM_RESULT_OK;
        job->state = SAMPLER_RAM_LOAD_DONE;
        sampler_ram_set_last(SAMPLER_RAM_RESULT_OK);
        return;
    }

    const sd_scheduler_background_kind_t kind =
        ((job->state == SAMPLER_RAM_LOAD_PARSE) || (job->state == SAMPLER_RAM_LOAD_READ))
            ? SD_SCHEDULER_BACKGROUND_DATA : SD_SCHEDULER_BACKGROUND_METADATA;
    uint32_t bytes = 0U;
    if (job->state == SAMPLER_RAM_LOAD_PARSE)
    {
        bytes = SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;
    }
    else if (job->state == SAMPLER_RAM_LOAD_READ)
    {
        const uint32_t frames_left = job->candidate.frames - job->frames_done;
        uint32_t frames = SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES / job->info.block_align;
        if (frames > frames_left)
        {
            frames = frames_left;
        }
        bytes = frames * job->info.block_align;
    }
    const sd_scheduler_background_admission_t admission = sampler_ram_load_sd_begin(kind, bytes);
    if (admission == SD_SCHEDULER_BACKGROUND_NOT_NOW)
    {
        return;
    }
    if (admission != SD_SCHEDULER_BACKGROUND_GO)
    {
        job->file_open = 0U;
        sampler_ram_load_fail(SAMPLER_RAM_RESULT_SD_MOUNT_FAIL);
        return;
    }

    switch (job->state)
    {
        case SAMPLER_RAM_LOAD_MOUNT:
            if (sd_access_fs_mount_if_needed() != 0U)
            {
                job->state = SAMPLER_RAM_LOAD_OPEN;
            }
            else
            {
                sampler_ram_load_fail(SAMPLER_RAM_RESULT_SD_MOUNT_FAIL);
            }
            break;
        case SAMPLER_RAM_LOAD_OPEN:
            if (f_open(&job->file, job->path, FA_READ) == FR_OK)
            {
                job->file_open = 1U;
                uint32_t actual_crc32 = 0U;
                if ((job->prepared != 0U)
                    && (((uint32_t)f_size(&job->file) != job->prepared_file_size)
                        || (wav_parser_crc32_file(&job->file, &actual_crc32) == 0U)
                        || (actual_crc32 != job->prepared_expected_crc32)))
                {
                    sampler_ram_load_fail(SAMPLER_RAM_RESULT_READ_FAIL);
                }
                else
                {
                    job->state = (job->prepared != 0U)
                        ? SAMPLER_RAM_LOAD_ALLOCATE : SAMPLER_RAM_LOAD_PARSE;
                }
            }
            else
            {
                sampler_ram_load_fail(SAMPLER_RAM_RESULT_OPEN_FAIL);
            }
            break;
        case SAMPLER_RAM_LOAD_PARSE:
            if ((wav_parser_parse_info(&job->file, &job->info) != 0)
                && (sampler_ram_wav_supported(&job->info) != 0U)
                && (job->info.data_size != 0U)
                && ((job->info.data_size % job->info.block_align) == 0U))
            {
                job->state = SAMPLER_RAM_LOAD_ALLOCATE;
            }
            else
            {
                sampler_ram_load_fail(SAMPLER_RAM_RESULT_WAV_UNSUPPORTED);
            }
            break;
        case SAMPLER_RAM_LOAD_SEEK:
            if (f_lseek(&job->file, job->info.data_offset) == FR_OK)
            {
                job->state = SAMPLER_RAM_LOAD_READ;
            }
            else
            {
                sampler_ram_load_fail(SAMPLER_RAM_RESULT_READ_FAIL);
            }
            break;
        case SAMPLER_RAM_LOAD_READ:
        {
            const uint32_t frames_left = job->candidate.frames - job->frames_done;
            uint32_t frames = SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES / job->info.block_align;
            if (frames > frames_left)
            {
                frames = frames_left;
            }
            const UINT wanted = (UINT)(frames * job->info.block_align);
            UINT read = 0U;
            if ((f_read(&job->file, g_sampler_ram_io, wanted, &read) == FR_OK)
                && (read == wanted))
            {
                job->buffered_frames = frames;
                job->converted_frames = 0U;
                job->state = SAMPLER_RAM_LOAD_CONVERT;
            }
            else
            {
                sampler_ram_load_fail(SAMPLER_RAM_RESULT_READ_FAIL);
            }
            break;
        }
        case SAMPLER_RAM_LOAD_CLOSE:
            if (job->file_open != 0U)
            {
                if (f_close(&job->file) != FR_OK)
                {
                    job->file_open = 0U;
                    sampler_ram_load_fail(SAMPLER_RAM_RESULT_READ_FAIL);
                    break;
                }
                job->file_open = 0U;
            }
            job->state = (job->failed != 0U)
                ? SAMPLER_RAM_LOAD_DONE : SAMPLER_RAM_LOAD_PUBLISH;
            break;
        default:
            break;
    }
    sd_scheduler_runtime_background_end();
}

uint8_t sampler_ram_pool_load_async_take_result(sampler_ram_result_t *out_result,
                                                uint16_t *out_ram_slot,
                                                uint16_t *out_global_slot,
                                                const char **out_path)
{
    sampler_ram_load_job_t *const job = &g_sampler_ram_load_job;
    if (job->state != SAMPLER_RAM_LOAD_DONE)
    {
        return 0U;
    }
    if (out_result != 0) *out_result = job->result;
    if (out_ram_slot != 0) *out_ram_slot = job->ram_slot;
    if (out_global_slot != 0) *out_global_slot = job->global_slot;
    if (out_path != 0) *out_path = job->path;
    job->state = SAMPLER_RAM_LOAD_IDLE;
    return 1U;
}

static void sampler_ram_pool_finalize_clear(uint16_t ram_slot)
{
    if (ram_slot >= SAMPLER_RAM_POOL_MAX_SLOTS)
    {
        return;
    }
    sampler_ram_slot_t *const slot = &g_sampler_ram_pool.slots[ram_slot];
    const sampler_ram_slot_t old = *slot;
    sampler_ram_audio_projection_withdraw(ram_slot, old.generation);
    const uint32_t generation = sampler_ram_next_generation();
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    slot->state = SAMPLER_RAM_SLOT_EMPTY;
    slot->generation = generation;
    __DMB();
    if (primask == 0U)
    {
        __enable_irq();
    }
    if (old.page_count != 0U)
    {
        sample_page_cache_port_release_shared(old.first_page_slot,
                                                       old.page_count);
    }
    sample_global_pool_clear_backend(SAMPLE_GLOBAL_KIND_RAM, ram_slot);
    memset(&g_sampler_ram_pool.slots[ram_slot], 0, sizeof(g_sampler_ram_pool.slots[ram_slot]));
    g_sampler_ram_pool.slots[ram_slot].state = SAMPLER_RAM_SLOT_EMPTY;
    g_sampler_ram_pool.slots[ram_slot].global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    g_sampler_ram_pool.slots[ram_slot].first_page_slot = UINT16_MAX;
    g_sampler_ram_pool.slots[ram_slot].generation = generation;
    sampler_ram_waveform_set_empty(&g_sampler_ram_pool.slots[ram_slot]);
}

void sampler_ram_pool_clear(uint16_t ram_slot)
{
    if (ram_slot >= SAMPLER_RAM_POOL_MAX_SLOTS) return;
    sampler_ram_slot_t *const slot = &g_sampler_ram_pool.slots[ram_slot];
    if (slot->state == SAMPLER_RAM_SLOT_RETIRING) return;
    if (slot->state != SAMPLER_RAM_SLOT_READY)
    {
        sampler_ram_pool_finalize_clear(ram_slot);
        return;
    }

    const uint32_t generation = slot->generation;
    sampler_ram_audio_projection_withdraw(ram_slot, generation);
    slot->state = SAMPLER_RAM_SLOT_RETIRING;
    g_sampler_ram_retire_stop_committed[ram_slot] = 0U;
    __DMB();
}

void sampler_ram_pool_retire_all(void)
{
    for (uint16_t i = 0U; i < SAMPLER_RAM_POOL_MAX_SLOTS; ++i)
        sampler_ram_pool_clear(i);
}

void sampler_ram_pool_service_retire(void)
{
    if (g_sampler_ram_retire_invariant_failed != 0U) return;
    for (uint16_t i = 0U; i < SAMPLER_RAM_POOL_MAX_SLOTS; ++i)
    {
        sampler_ram_slot_t *const slot = &g_sampler_ram_pool.slots[i];
        if (slot->state != SAMPLER_RAM_SLOT_RETIRING) continue;
        uint64_t now_sample = 0U;
        if (!control_rt_now_sample(&now_sample)) continue;
        if (g_sampler_ram_retire_stop_committed[i] == 0U)
        {
            if (control_rt_publication_horizon_active() != 0U) continue;
            if (control_rt_publish_param_now((uint8_t)i, 0xFFF6U,
                    slot->generation, 0U) == 0U)
            {
                g_sampler_ram_retire_invariant_failed = 1U;
                continue;
            }
            g_sampler_ram_retire_not_before_sample[i] = now_sample
                + CONTROL_AUDIO_RESOURCE_RETIRE_GRACE_FRAMES;
            g_sampler_ram_retire_stop_committed[i] = 1U;
        }
        if (now_sample < g_sampler_ram_retire_not_before_sample[i]) continue;
        g_sampler_ram_retire_stop_committed[i] = 0U;
        sampler_ram_pool_finalize_clear(i);
    }
}

uint8_t sampler_ram_pool_retire_idle(void)
{
    if (g_sampler_ram_retire_invariant_failed != 0U) return 0U;
    for (uint16_t i = 0U; i < SAMPLER_RAM_POOL_MAX_SLOTS; ++i)
        if (g_sampler_ram_pool.slots[i].state == SAMPLER_RAM_SLOT_RETIRING)
            return 0U;
    return 1U;
}

uint8_t sampler_ram_pool_retire_failed(void)
{
    return g_sampler_ram_retire_invariant_failed;
}

const sampler_ram_slot_t *sampler_ram_pool_get_slot(uint16_t ram_slot)
{
    if (ram_slot >= SAMPLER_RAM_POOL_MAX_SLOTS)
    {
        return 0;
    }
    const sampler_ram_slot_t *const slot = &g_sampler_ram_pool.slots[ram_slot];
    if (slot->state == SAMPLER_RAM_SLOT_READY)
    {
        __DMB();
    }
    return slot;
}

sampler_ram_slot_state_t sampler_ram_pool_get_state(uint16_t ram_slot)
{
    const sampler_ram_slot_t *const slot = sampler_ram_pool_get_slot(ram_slot);
    return (slot != 0) ? slot->state : SAMPLER_RAM_SLOT_ERROR;
}

const float *sampler_ram_pool_get_data(uint16_t ram_slot)
{
    const sampler_ram_slot_t *const slot = sampler_ram_pool_get_slot(ram_slot);
    return ((slot != 0) && (slot->state == SAMPLER_RAM_SLOT_READY)) ? slot->data : 0;
}

uint32_t sampler_ram_pool_get_cost(uint16_t ram_slot)
{
    const sampler_ram_slot_t *const slot = sampler_ram_pool_get_slot(ram_slot);
    return (slot != 0) ? slot->cost_bytes_aligned : 0U;
}

uint32_t sampler_ram_pool_get_used_bytes(void)
{
    const uint32_t total = sample_page_cache_port_shared_total_bytes();
    const uint32_t free_bytes = sample_page_cache_port_shared_free_bytes();
    return (free_bytes >= total) ? 0U : (total - free_bytes);
}

uint32_t sampler_ram_pool_get_free_bytes(void)
{
    return sample_page_cache_port_shared_free_bytes();
}

sampler_ram_result_t sampler_ram_pool_get_last_result(void)
{
    return g_sampler_ram_pool.last_result;
}

const char *sampler_ram_pool_result_label(sampler_ram_result_t result)
{
    switch (result)
    {
        case SAMPLER_RAM_RESULT_OK:
            return "LOAD OK";
        case SAMPLER_RAM_RESULT_POOL_FULL:
        case SAMPLER_RAM_RESULT_GLOBAL_SLOT_FULL:
            return "SLOT FULL";
        case SAMPLER_RAM_RESULT_GLOBAL_BUDGET_FULL:
        case SAMPLER_RAM_RESULT_RAM_POOL_FULL:
            return "MEM FULL";
        case SAMPLER_RAM_RESULT_TOO_LARGE:
            return "TOO LARGE";
        case SAMPLER_RAM_RESULT_PATH_TOO_LONG:
            return "PATH LONG";
        case SAMPLER_RAM_RESULT_SD_MOUNT_FAIL:
            return "SD UNAVAILABLE";
        case SAMPLER_RAM_RESULT_OPEN_FAIL:
            return "OPEN FAIL";
        case SAMPLER_RAM_RESULT_WAV_UNSUPPORTED:
            return "WAV UNSUPP";
        case SAMPLER_RAM_RESULT_READ_FAIL:
            return "SD READ FAIL";
        case SAMPLER_RAM_RESULT_REGISTER_FAIL:
            return "REGISTER FAIL";
        default:
            return "LOAD FAIL";
    }
}

void sampler_ram_pool_waveform_service(uint32_t frame_budget)
{
    uint32_t frames_left = (frame_budget != 0U)
                               ? frame_budget
                               : SAMPLER_RAM_WAVEFORM_DEFAULT_SERVICE_FRAMES;
    for (uint16_t i = 0U; (i < SAMPLER_RAM_POOL_MAX_SLOTS) && (frames_left > 0U); ++i)
    {
        sampler_ram_waveform_service_slot(&g_sampler_ram_pool.slots[i], &frames_left);
    }
}

const sample_ram_waveform_overview_t *sampler_ram_pool_get_waveform(uint16_t ram_slot)
{
    const sampler_ram_slot_t *const slot = sampler_ram_pool_get_slot(ram_slot);
    if ((slot == 0) || (sampler_ram_waveform_matches_slot(slot) == 0U))
    {
        return 0;
    }
    return &slot->waveform;
}

const sample_ram_waveform_overview_t *sampler_ram_pool_get_waveform_for_global(uint16_t global_slot)
{
    uint16_t ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    if (sample_global_pool_resolve_backend(global_slot,
                                           SAMPLE_GLOBAL_KIND_RAM,
                                           &ram_slot) == 0U)
    {
        return 0;
    }
    return sampler_ram_pool_get_waveform(ram_slot);
}
