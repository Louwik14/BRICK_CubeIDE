#include "Sampler/sampler_ram_pool.h"

#include <string.h>

#include "ff.h"
#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "Storage/wav_parser.h"
#include "Sampler/sample_page_cache.h"

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
                    && (slot->format == SAMPLER_RAM_FORMAT_FLOAT32_INTERLEAVED)
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

static void sampler_ram_slot_error_at(uint16_t ram_slot,
                                      sampler_ram_result_t result,
                                      uint16_t forced_global_slot)
{
    if (ram_slot < SAMPLER_RAM_POOL_MAX_SLOTS)
    {
        uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        g_sampler_ram_pool.slots[ram_slot].generation = sampler_ram_next_generation();
        if (g_sampler_ram_pool.slots[ram_slot].page_count != 0U)
        {
            sample_page_cache_release_slot_pool_allocation(
                g_sampler_ram_pool.slots[ram_slot].first_page_slot,
                g_sampler_ram_pool.slots[ram_slot].page_count);
        }
        g_sampler_ram_pool.slots[ram_slot].format = SAMPLER_RAM_FORMAT_NONE;
        g_sampler_ram_pool.slots[ram_slot].data = 0;
        g_sampler_ram_pool.slots[ram_slot].data_offset = 0U;
        g_sampler_ram_pool.slots[ram_slot].first_page_slot = UINT16_MAX;
        g_sampler_ram_pool.slots[ram_slot].page_count = 0U;
        g_sampler_ram_pool.slots[ram_slot].data_bytes = 0U;
        g_sampler_ram_pool.slots[ram_slot].cost_bytes_aligned = 0U;
        g_sampler_ram_pool.slots[ram_slot].state = SAMPLER_RAM_SLOT_ERROR;
        g_sampler_ram_pool.slots[ram_slot].error = result;
        sampler_ram_waveform_set_error(&g_sampler_ram_pool.slots[ram_slot]);
        const uint8_t registered =
            (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
                ? sample_global_pool_register_ram_error_at(
                    forced_global_slot,
                    ram_slot,
                    g_sampler_ram_pool.slots[ram_slot].path)
                : sample_global_pool_register_ram_error(
                    ram_slot,
                    g_sampler_ram_pool.slots[ram_slot].path,
                    &global_slot);
        if (registered != 0U)
        {
            g_sampler_ram_pool.slots[ram_slot].global_slot =
                (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
                    ? forced_global_slot
                    : global_slot;
        }
    }
    sampler_ram_set_last(result);
}

void sampler_ram_pool_init(void)
{
    sampler_ram_pool_reset();
}

void sampler_ram_pool_reset(void)
{
    uint32_t generation_seed = g_sampler_ram_pool.generation_counter;
    for (uint16_t i = 0U; i < SAMPLER_RAM_POOL_MAX_SLOTS; ++i)
    {
        g_sampler_ram_pool.slots[i].generation = sampler_ram_next_generation();
        if (g_sampler_ram_pool.slots[i].page_count != 0U)
        {
            sample_page_cache_release_slot_pool_allocation(
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

sampler_ram_result_t sampler_ram_pool_load_wav_auto(const char *path,
                                                    uint16_t *out_ram_slot,
                                                    uint16_t *out_global_slot)
{
    const uint16_t ram_slot = sampler_ram_pool_find_free_slot();
    if (out_ram_slot != 0)
    {
        *out_ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    }
    if (ram_slot == SAMPLER_RAM_POOL_INVALID_SLOT)
    {
        sampler_ram_set_last(SAMPLER_RAM_RESULT_POOL_FULL);
        return SAMPLER_RAM_RESULT_POOL_FULL;
    }

    const sampler_ram_result_t result =
        sampler_ram_pool_load_wav(ram_slot, path, out_global_slot);
    if (result == SAMPLER_RAM_RESULT_OK)
    {
        if (out_ram_slot != 0)
        {
            *out_ram_slot = ram_slot;
        }
    }
    return result;
}

static sampler_ram_result_t sampler_ram_pool_load_wav_impl(uint16_t ram_slot,
                                                           uint16_t forced_global_slot,
                                                           const char *path,
                                                           uint16_t *out_global_slot)
{
    FIL fp;
    wav_info_t info;
    UINT br = 0U;
    uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    sample_page_raw_allocation_t allocation;

    if (out_global_slot != 0)
    {
        *out_global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }
    if ((ram_slot >= SAMPLER_RAM_POOL_MAX_SLOTS) || (path == 0) || (path[0] == '\0'))
    {
        sampler_ram_set_last(SAMPLER_RAM_RESULT_INVALID_ARG);
        return SAMPLER_RAM_RESULT_INVALID_ARG;
    }
    if (sampler_ram_copy_path(g_sampler_ram_pool.slots[ram_slot].path,
                              sizeof(g_sampler_ram_pool.slots[ram_slot].path),
                              path) == 0U)
    {
        sampler_ram_set_last(SAMPLER_RAM_RESULT_PATH_TOO_LONG);
        return SAMPLER_RAM_RESULT_PATH_TOO_LONG;
    }
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        sampler_ram_set_last(SAMPLER_RAM_RESULT_SD_BUSY);
        return SAMPLER_RAM_RESULT_SD_BUSY;
    }
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        sampler_ram_set_last(SAMPLER_RAM_RESULT_SD_MOUNT_FAIL);
        return SAMPLER_RAM_RESULT_SD_MOUNT_FAIL;
    }
    if (f_open(&fp, path, FA_READ) != FR_OK)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        sampler_ram_set_last(SAMPLER_RAM_RESULT_OPEN_FAIL);
        return SAMPLER_RAM_RESULT_OPEN_FAIL;
    }
    if (wav_parser_parse_info(&fp, &info) == 0)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        sampler_ram_set_last(SAMPLER_RAM_RESULT_WAV_PARSE_FAIL);
        return SAMPLER_RAM_RESULT_WAV_PARSE_FAIL;
    }
    if ((sampler_ram_wav_supported(&info) == 0U)
        || (info.data_size == 0U)
        || ((info.data_size % info.block_align) != 0U))
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        sampler_ram_set_last(SAMPLER_RAM_RESULT_WAV_UNSUPPORTED);
        return SAMPLER_RAM_RESULT_WAV_UNSUPPORTED;
    }

    const uint32_t frames = info.data_size / info.block_align;
    const uint16_t bytes_per_frame = (uint16_t)(2U * sizeof(float));
    if ((frames == 0U) || (frames > (UINT32_MAX / bytes_per_frame)))
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        sampler_ram_set_last(SAMPLER_RAM_RESULT_TOO_LARGE);
        return SAMPLER_RAM_RESULT_TOO_LARGE;
    }
    const uint32_t data_bytes = frames * bytes_per_frame;
    const uint32_t page_count =
        (data_bytes + SAMPLE_PAGE_BYTES - 1U) / SAMPLE_PAGE_BYTES;
    const uint32_t cost = page_count * SAMPLE_PAGE_BYTES;
    if ((cost == 0U) || (cost > SAMPLER_RAM_POOL_BYTES))
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        sampler_ram_set_last(SAMPLER_RAM_RESULT_TOO_LARGE);
        return SAMPLER_RAM_RESULT_TOO_LARGE;
    }
    if ((forced_global_slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
        && (sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_RAM,
                                               ram_slot,
                                               &global_slot) == 0U)
        && sample_global_pool_find_free_slot() >= SAMPLE_GLOBAL_POOL_MAX_SLOTS)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        sampler_ram_set_last(SAMPLER_RAM_RESULT_GLOBAL_SLOT_FULL);
        return SAMPLER_RAM_RESULT_GLOBAL_SLOT_FULL;
    }
    if (sample_global_pool_validate_budget(SAMPLE_GLOBAL_KIND_RAM,
                                           ram_slot,
                                           cost) == 0U)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        sampler_ram_set_last(SAMPLER_RAM_RESULT_GLOBAL_BUDGET_FULL);
        return SAMPLER_RAM_RESULT_GLOBAL_BUDGET_FULL;
    }

    sampler_ram_pool_clear(ram_slot);
    memset(&allocation, 0, sizeof(allocation));
    if (sample_page_cache_alloc_slot_pool_bytes(data_bytes, &allocation) == 0U)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        sampler_ram_set_last(SAMPLER_RAM_RESULT_RAM_POOL_FULL);
        return SAMPLER_RAM_RESULT_RAM_POOL_FULL;
    }

    sampler_ram_slot_t *const slot = &g_sampler_ram_pool.slots[ram_slot];
    memset(slot, 0, sizeof(*slot));
    slot->state = SAMPLER_RAM_SLOT_LOADING;
    slot->global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    (void)sampler_ram_copy_path(slot->path, sizeof(slot->path), path);
    slot->format = SAMPLER_RAM_FORMAT_FLOAT32_INTERLEAVED;
    slot->channels = 2U;
    slot->sample_rate = info.sample_rate;
    slot->frames = frames;
    slot->bytes_per_frame = bytes_per_frame;
    slot->data_offset = (uint32_t)allocation.first_slot * SAMPLE_PAGE_BYTES;
    slot->first_page_slot = allocation.first_slot;
    slot->page_count = allocation.page_count;
    slot->generation = sampler_ram_next_generation();
    slot->data_bytes = data_bytes;
    slot->cost_bytes_aligned = allocation.capacity_bytes;
    slot->data = (float *)allocation.data;
    slot->error = SAMPLER_RAM_RESULT_OK;

    if (f_lseek(&fp, info.data_offset) != FR_OK)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        sampler_ram_slot_error_at(ram_slot, SAMPLER_RAM_RESULT_READ_FAIL, forced_global_slot);
        return SAMPLER_RAM_RESULT_READ_FAIL;
    }

    uint32_t frames_done = 0U;
    while (frames_done < frames)
    {
        uint32_t frames_chunk = (SAMPLER_RAM_IO_BYTES / info.block_align);
        if (frames_chunk == 0U)
        {
            (void)f_close(&fp);
            sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
            sampler_ram_slot_error_at(ram_slot, SAMPLER_RAM_RESULT_READ_FAIL, forced_global_slot);
            return SAMPLER_RAM_RESULT_READ_FAIL;
        }
        if (frames_chunk > (frames - frames_done))
        {
            frames_chunk = frames - frames_done;
        }
        const UINT bytes_to_read = (UINT)(frames_chunk * info.block_align);
        if ((f_read(&fp, g_sampler_ram_io, bytes_to_read, &br) != FR_OK)
            || (br != bytes_to_read))
        {
            (void)f_close(&fp);
            sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
            sampler_ram_slot_error_at(ram_slot, SAMPLER_RAM_RESULT_READ_FAIL, forced_global_slot);
            return SAMPLER_RAM_RESULT_READ_FAIL;
        }

        float *dst = &slot->data[frames_done * 2U];
        const uint8_t *src = g_sampler_ram_io;
        if (info.bits_per_sample == 16U)
        {
            for (uint32_t i = 0U; i < frames_chunk; ++i)
            {
                const float left = sampler_ram_pcm16_to_float(src);
                src += 2U;
                const float right = (info.channels == 2U)
                                        ? sampler_ram_pcm16_to_float(src)
                                        : left;
                src += (info.channels == 2U) ? 2U : 0U;
                dst[(i * 2U) + 0U] = left;
                dst[(i * 2U) + 1U] = right;
            }
        }
        else
        {
            for (uint32_t i = 0U; i < frames_chunk; ++i)
            {
                const float left = sampler_ram_pcm24_to_float(src);
                src += 3U;
                const float right = (info.channels == 2U)
                                        ? sampler_ram_pcm24_to_float(src)
                                        : left;
                src += (info.channels == 2U) ? 3U : 0U;
                dst[(i * 2U) + 0U] = left;
                dst[(i * 2U) + 1U] = right;
            }
        }
        frames_done += frames_chunk;
    }

    (void)f_close(&fp);
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);

    const uint8_t registered =
        (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
            ? sample_global_pool_register_ram_at(forced_global_slot,
                                                 ram_slot,
                                                 path,
                                                 allocation.capacity_bytes)
            : sample_global_pool_register_ram(ram_slot,
                                              path,
                                              allocation.capacity_bytes,
                                              &global_slot);
    if (registered == 0U)
    {
        sampler_ram_pool_clear(ram_slot);
        sampler_ram_set_last(SAMPLER_RAM_RESULT_REGISTER_FAIL);
        return SAMPLER_RAM_RESULT_REGISTER_FAIL;
    }

    slot->state = SAMPLER_RAM_SLOT_READY;
    slot->global_slot = (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
                            ? forced_global_slot
                            : global_slot;
    sampler_ram_waveform_begin(slot);
    sampler_ram_set_last(SAMPLER_RAM_RESULT_OK);
    if (out_global_slot != 0)
    {
        *out_global_slot = slot->global_slot;
    }
    return SAMPLER_RAM_RESULT_OK;
}

sampler_ram_result_t sampler_ram_pool_load_wav(uint16_t ram_slot,
                                               const char *path,
                                               uint16_t *out_global_slot)
{
    return sampler_ram_pool_load_wav_impl(ram_slot,
                                          SAMPLE_GLOBAL_POOL_INVALID_INDEX,
                                          path,
                                          out_global_slot);
}

sampler_ram_result_t sampler_ram_pool_load_wav_at(uint16_t ram_slot,
                                                  uint16_t global_slot,
                                                  const char *path)
{
    if (global_slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
    {
        sampler_ram_set_last(SAMPLER_RAM_RESULT_INVALID_ARG);
        return SAMPLER_RAM_RESULT_INVALID_ARG;
    }

    uint16_t loaded_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    const sampler_ram_result_t result =
        sampler_ram_pool_load_wav_impl(ram_slot, global_slot, path, &loaded_global);
    if ((result != SAMPLER_RAM_RESULT_OK)
        && (ram_slot < SAMPLER_RAM_POOL_MAX_SLOTS)
        && (path != 0)
        && (path[0] != '\0'))
    {
        (void)sampler_ram_copy_path(g_sampler_ram_pool.slots[ram_slot].path,
                                    sizeof(g_sampler_ram_pool.slots[ram_slot].path),
                                    path);
        sampler_ram_slot_error_at(ram_slot, result, global_slot);
    }
    return result;
}

void sampler_ram_pool_clear(uint16_t ram_slot)
{
    if (ram_slot >= SAMPLER_RAM_POOL_MAX_SLOTS)
    {
        return;
    }
    const sampler_ram_slot_t *const old = &g_sampler_ram_pool.slots[ram_slot];
    const uint32_t generation = sampler_ram_next_generation();
    g_sampler_ram_pool.slots[ram_slot].generation = generation;
    if (old->page_count != 0U)
    {
        sample_page_cache_release_slot_pool_allocation(old->first_page_slot,
                                                       old->page_count);
    }
    sample_global_pool_clear_backend(SAMPLE_GLOBAL_KIND_RAM, ram_slot);
    memset(&g_sampler_ram_pool.slots[ram_slot], 0, sizeof(g_sampler_ram_pool.slots[ram_slot]));
    g_sampler_ram_pool.slots[ram_slot].state = SAMPLER_RAM_SLOT_EMPTY;
    g_sampler_ram_pool.slots[ram_slot].global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    g_sampler_ram_pool.slots[ram_slot].first_page_slot = UINT16_MAX;
    g_sampler_ram_pool.slots[ram_slot].generation = generation;
    sampler_ram_waveform_set_empty(&g_sampler_ram_pool.slots[ram_slot]);
}

const sampler_ram_slot_t *sampler_ram_pool_get_slot(uint16_t ram_slot)
{
    if (ram_slot >= SAMPLER_RAM_POOL_MAX_SLOTS)
    {
        return 0;
    }
    return &g_sampler_ram_pool.slots[ram_slot];
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
    const uint32_t total = sample_page_cache_slot_pool_total_bytes();
    const uint32_t free_bytes = sample_page_cache_slot_pool_free_bytes();
    return (free_bytes >= total) ? 0U : (total - free_bytes);
}

uint32_t sampler_ram_pool_get_free_bytes(void)
{
    return sample_page_cache_slot_pool_free_bytes();
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
        case SAMPLER_RAM_RESULT_SD_BUSY:
            return "SD BUSY";
        case SAMPLER_RAM_RESULT_SD_MOUNT_FAIL:
            return "SD UNAVAILABLE";
        case SAMPLER_RAM_RESULT_OPEN_FAIL:
            return "OPEN FAIL";
        case SAMPLER_RAM_RESULT_WAV_PARSE_FAIL:
            return "BAD WAV";
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
