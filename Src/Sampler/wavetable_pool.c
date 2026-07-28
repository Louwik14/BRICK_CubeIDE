#include "Sampler/wavetable_pool.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "Sampler/sample_page_cache.h"
#include "Storage/sd_access_gate.h"
#include "Storage/memory_layout.h"
#include "Storage/wav_audio_codec.h"
#include "Storage/wav_parser.h"

#define WAVETABLE_POOL_IO_BYTES (8192U)
#define WAVETABLE_POOL_CACHE_DIR "0:/WAVETABLES/.CACHE"

typedef struct
{
    wavetable_slot_t slots[WAVETABLE_POOL_MAX_SLOTS];
    wavetable_result_t last_result;
    uint32_t generation_counter;
} wavetable_pool_state_t;

STORAGE_STATE_SDRAM static wavetable_pool_state_t g_wavetable_pool;
AUDIO_WARM ALIGN32 static uint8_t g_wavetable_pool_io[WAVETABLE_POOL_IO_BYTES];

static void wavetable_pool_set_last(wavetable_result_t result)
{
    g_wavetable_pool.last_result = result;
}

static uint32_t wavetable_pool_next_generation(void)
{
    g_wavetable_pool.generation_counter++;
    if (g_wavetable_pool.generation_counter == 0U)
    {
        g_wavetable_pool.generation_counter = 1U;
    }
    return g_wavetable_pool.generation_counter;
}

static uint8_t wavetable_pool_copy_path(char *dst, uint32_t dst_size, const char *src)
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

static uint16_t wavetable_pool_read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t wavetable_pool_read_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static void wavetable_pool_decode_header(const uint8_t *src,
                                         wavetable_file_header_t *out_header)
{
    out_header->magic = wavetable_pool_read_u32(&src[0]);
    out_header->version = wavetable_pool_read_u16(&src[4]);
    out_header->header_size = wavetable_pool_read_u16(&src[6]);
    out_header->frame_sample_count = wavetable_pool_read_u32(&src[8]);
    out_header->frame_count = wavetable_pool_read_u32(&src[12]);
    out_header->sample_format = wavetable_pool_read_u16(&src[16]);
    out_header->reserved0 = wavetable_pool_read_u16(&src[18]);
    out_header->data_offset = wavetable_pool_read_u32(&src[20]);
    out_header->data_size = wavetable_pool_read_u32(&src[24]);
    out_header->reserved1 = wavetable_pool_read_u32(&src[28]);
}

static void wavetable_pool_write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void wavetable_pool_write_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
    p[2] = (uint8_t)((value >> 16) & 0xFFU);
    p[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void wavetable_pool_encode_header(uint8_t *dst,
                                         uint32_t frame_count,
                                         uint16_t source_stamp,
                                         uint32_t source_size)
{
    memset(dst, 0, WAVETABLE_POOL_HEADER_SIZE);
    wavetable_pool_write_u32(&dst[0], WAVETABLE_POOL_FILE_MAGIC);
    wavetable_pool_write_u16(&dst[4], WAVETABLE_POOL_FILE_VERSION);
    wavetable_pool_write_u16(&dst[6], WAVETABLE_POOL_HEADER_SIZE);
    wavetable_pool_write_u32(&dst[8], WAVETABLE_FRAME_SAMPLE_COUNT);
    wavetable_pool_write_u32(&dst[12], frame_count);
    wavetable_pool_write_u16(&dst[16], (uint16_t)WAVETABLE_FILE_SAMPLE_S16);
    wavetable_pool_write_u16(&dst[18], source_stamp);
    wavetable_pool_write_u32(&dst[20], WAVETABLE_POOL_HEADER_SIZE);
    wavetable_pool_write_u32(&dst[24], frame_count * WAVETABLE_FRAME_SAMPLE_COUNT * sizeof(int16_t));
    wavetable_pool_write_u32(&dst[28], source_size);
}

static int16_t wavetable_pool_s16_from_le(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint16_t wavetable_pool_preview_abs_i16(int16_t value)
{
    return (value < 0) ? (uint16_t)(-value) : (uint16_t)value;
}

static int16_t wavetable_pool_float_to_s16(float value)
{
    if (value > 1.0f)
    {
        value = 1.0f;
    }
    else if (value < -1.0f)
    {
        value = -1.0f;
    }
    if (value != value)
    {
        return 0;
    }
    return (int16_t)(value * 32767.0f);
}

static void wavetable_pool_preview_clear(wavetable_preview_t *preview)
{
    if (preview == 0)
    {
        return;
    }
    memset(preview, 0, sizeof(*preview));
    preview->state = WAVETABLE_PREVIEW_EMPTY;
}

static void wavetable_pool_preview_build(wavetable_slot_t *slot)
{
    if ((slot == 0)
            || (slot->data == 0)
            || (slot->frame_count == 0U)
            || (slot->frame_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT))
    {
        return;
    }

    wavetable_preview_t *const preview = &slot->preview;
    memset(preview, 0, sizeof(*preview));
    preview->state = WAVETABLE_PREVIEW_READY;
    preview->generation = slot->generation;
    preview->frame_count = slot->frame_count;
    preview->columns = WAVETABLE_PREVIEW_COLUMNS;
    for (uint16_t col = 0U; col < WAVETABLE_PREVIEW_COLUMNS; ++col)
    {
        preview->min[col] = 32767;
        preview->max[col] = -32768;
    }

    for (uint32_t frame = 0U; frame < slot->frame_count; ++frame)
    {
        uint32_t col = (uint32_t)(((uint64_t)frame * WAVETABLE_PREVIEW_COLUMNS) / slot->frame_count);
        if (col >= WAVETABLE_PREVIEW_COLUMNS)
        {
            col = WAVETABLE_PREVIEW_COLUMNS - 1U;
        }

        const int16_t *const src = &slot->data[frame * WAVETABLE_FRAME_SAMPLE_COUNT];
        int16_t frame_min = 32767;
        int16_t frame_max = -32768;
        for (uint32_t i = 0U; i < WAVETABLE_FRAME_SAMPLE_COUNT; ++i)
        {
            const int16_t s = src[i];
            if (s < frame_min)
            {
                frame_min = s;
            }
            if (s > frame_max)
            {
                frame_max = s;
            }
        }

        if (frame_min < preview->min[col])
        {
            preview->min[col] = frame_min;
        }
        if (frame_max > preview->max[col])
        {
            preview->max[col] = frame_max;
        }
        const uint16_t min_peak = wavetable_pool_preview_abs_i16(frame_min);
        const uint16_t max_peak = wavetable_pool_preview_abs_i16(frame_max);
        if (min_peak > preview->global_peak)
        {
            preview->global_peak = min_peak;
        }
        if (max_peak > preview->global_peak)
        {
            preview->global_peak = max_peak;
        }
    }

    for (uint16_t col = 0U; col < WAVETABLE_PREVIEW_COLUMNS; ++col)
    {
        if (preview->min[col] > preview->max[col])
        {
            preview->min[col] = 0;
            preview->max[col] = 0;
        }
    }
}

static uint8_t wavetable_pool_header_valid(const wavetable_file_header_t *header)
{
    if ((header == 0)
        || (header->magic != WAVETABLE_POOL_FILE_MAGIC)
        || (header->version != WAVETABLE_POOL_FILE_VERSION)
        || (header->header_size < WAVETABLE_POOL_HEADER_SIZE)
        || (header->frame_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT)
        || (header->frame_count == 0U)
        || (header->data_offset < header->header_size))
    {
        return 0U;
    }

    if (header->sample_format != (uint16_t)WAVETABLE_FILE_SAMPLE_S16)
    {
        return 0U;
    }
    const uint32_t bytes_per_sample = sizeof(int16_t);

    if (header->frame_count > (UINT32_MAX / WAVETABLE_FRAME_SAMPLE_COUNT))
    {
        return 0U;
    }
    const uint32_t sample_count = header->frame_count * WAVETABLE_FRAME_SAMPLE_COUNT;
    if (sample_count > (UINT32_MAX / bytes_per_sample))
    {
        return 0U;
    }
    return (header->data_size == (sample_count * bytes_per_sample)) ? 1U : 0U;
}

static uint32_t wavetable_pool_path_hash(const char *path)
{
    uint32_t hash = 2166136261UL;
    if (path == 0)
    {
        return hash;
    }
    while (*path != '\0')
    {
        hash ^= (uint8_t)*path++;
        hash *= 16777619UL;
    }
    return hash;
}

static uint16_t wavetable_pool_source_stamp(const FILINFO *info)
{
    return (info != 0) ? (uint16_t)(info->fdate ^ info->ftime) : 0U;
}

static uint8_t wavetable_pool_make_cache_path(char *out,
                                              uint32_t out_size,
                                              const char *source_path,
                                              const FILINFO *source_info)
{
    if ((out == 0) || (out_size == 0U) || (source_path == 0) || (source_info == 0))
    {
        return 0U;
    }
    const int written = snprintf(out,
                                 out_size,
                                 "%s/H%08lX_%08lX_%04X%04X.B6WT",
                                 WAVETABLE_POOL_CACHE_DIR,
                                 (unsigned long)wavetable_pool_path_hash(source_path),
                                 (unsigned long)((uint32_t)source_info->fsize),
                                 (unsigned)source_info->fdate,
                                 (unsigned)source_info->ftime);
    return (uint8_t)((written >= 0) && ((uint32_t)written < out_size));
}

static uint8_t wavetable_pool_cache_valid(const char *cache_path,
                                          const FILINFO *source_info)
{
    FIL fp;
    UINT br = 0U;
    wavetable_file_header_t header;
    if ((cache_path == 0) || (source_info == 0) || (f_open(&fp, cache_path, FA_READ) != FR_OK))
    {
        return 0U;
    }
    const uint8_t ok =
        ((f_read(&fp, g_wavetable_pool_io, WAVETABLE_POOL_HEADER_SIZE, &br) == FR_OK)
         && (br == WAVETABLE_POOL_HEADER_SIZE));
    const FSIZE_t cache_size = f_size(&fp);
    (void)f_close(&fp);
    if (ok == 0U)
    {
        return 0U;
    }
    wavetable_pool_decode_header(g_wavetable_pool_io, &header);
    const FSIZE_t expected_size = (FSIZE_t)header.data_offset + (FSIZE_t)header.data_size;
    return (uint8_t)((wavetable_pool_header_valid(&header) != 0U)
                     && (cache_size >= expected_size)
                     && (header.reserved0 == wavetable_pool_source_stamp(source_info))
                     && (header.reserved1 == (uint32_t)source_info->fsize));
}

static uint8_t wavetable_pool_path_ext_is_wav(const char *path)
{
    const size_t len = (path != 0) ? strlen(path) : 0U;
    if (len < 4U)
    {
        return 0U;
    }
    return (uint8_t)((path[len - 4U] == '.')
                     && ((path[len - 3U] == 'w') || (path[len - 3U] == 'W'))
                     && ((path[len - 2U] == 'a') || (path[len - 2U] == 'A'))
                     && ((path[len - 1U] == 'v') || (path[len - 1U] == 'V')));
}

static void wavetable_pool_slot_error_at(uint16_t wavetable_slot,
                                         wavetable_result_t result,
                                         uint16_t forced_global_slot)
{
    if (wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
    {
        uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        g_wavetable_pool.slots[wavetable_slot].generation = wavetable_pool_next_generation();
        if (g_wavetable_pool.slots[wavetable_slot].page_count != 0U)
        {
            sample_page_cache_release_slot_pool_allocation(
                g_wavetable_pool.slots[wavetable_slot].first_page_slot,
                g_wavetable_pool.slots[wavetable_slot].page_count);
        }
        g_wavetable_pool.slots[wavetable_slot].format = WAVETABLE_FORMAT_NONE;
        g_wavetable_pool.slots[wavetable_slot].data = 0;
        g_wavetable_pool.slots[wavetable_slot].data_offset = 0U;
        g_wavetable_pool.slots[wavetable_slot].first_page_slot = UINT16_MAX;
        g_wavetable_pool.slots[wavetable_slot].page_count = 0U;
        g_wavetable_pool.slots[wavetable_slot].data_bytes = 0U;
        g_wavetable_pool.slots[wavetable_slot].cost_bytes_aligned = 0U;
        wavetable_pool_preview_clear(&g_wavetable_pool.slots[wavetable_slot].preview);
        g_wavetable_pool.slots[wavetable_slot].state = WAVETABLE_SLOT_ERROR;
        g_wavetable_pool.slots[wavetable_slot].error = result;
        const uint8_t registered =
            (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
                ? sample_global_pool_register_wavetable_error_at(
                    forced_global_slot,
                    wavetable_slot,
                    g_wavetable_pool.slots[wavetable_slot].path)
                : sample_global_pool_register_wavetable_error(
                    wavetable_slot,
                    g_wavetable_pool.slots[wavetable_slot].path,
                    &global_slot);
        if (registered != 0U)
        {
            g_wavetable_pool.slots[wavetable_slot].global_slot =
                (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
                    ? forced_global_slot
                    : global_slot;
        }
    }
    wavetable_pool_set_last(result);
}

void wavetable_pool_init(void)
{
    wavetable_pool_reset();
}

void wavetable_pool_reset(void)
{
    uint32_t generation_seed = g_wavetable_pool.generation_counter;
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        g_wavetable_pool.slots[i].generation = wavetable_pool_next_generation();
        if (g_wavetable_pool.slots[i].page_count != 0U)
        {
            sample_page_cache_release_slot_pool_allocation(
                g_wavetable_pool.slots[i].first_page_slot,
                g_wavetable_pool.slots[i].page_count);
        }
        sample_global_pool_clear_backend(SAMPLE_GLOBAL_KIND_WAVETABLE, i);
    }
    generation_seed = g_wavetable_pool.generation_counter;
    memset(&g_wavetable_pool, 0, sizeof(g_wavetable_pool));
    g_wavetable_pool.generation_counter = (generation_seed == 0U) ? 1U : generation_seed;
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        g_wavetable_pool.slots[i].state = WAVETABLE_SLOT_EMPTY;
        g_wavetable_pool.slots[i].global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        g_wavetable_pool.slots[i].first_page_slot = UINT16_MAX;
        g_wavetable_pool.slots[i].error = WAVETABLE_RESULT_OK;
        g_wavetable_pool.slots[i].generation = wavetable_pool_next_generation();
    }
    wavetable_pool_set_last(WAVETABLE_RESULT_OK);
}

uint16_t wavetable_pool_find_free_slot(void)
{
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        if (g_wavetable_pool.slots[i].state == WAVETABLE_SLOT_EMPTY)
        {
            return i;
        }
    }
    return WAVETABLE_POOL_INVALID_SLOT;
}

wavetable_result_t wavetable_pool_load_file_auto(const char *path,
                                                 uint16_t *out_wavetable_slot,
                                                 uint16_t *out_global_slot)
{
    const uint16_t wavetable_slot = wavetable_pool_find_free_slot();
    if (out_wavetable_slot != 0)
    {
        *out_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    }
    if (wavetable_slot == WAVETABLE_POOL_INVALID_SLOT)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_POOL_FULL);
        return WAVETABLE_RESULT_POOL_FULL;
    }

    const wavetable_result_t result =
        wavetable_pool_load_file(wavetable_slot, path, out_global_slot);
    if ((result == WAVETABLE_RESULT_OK) && (out_wavetable_slot != 0))
    {
        *out_wavetable_slot = wavetable_slot;
    }
    return result;
}

static wavetable_result_t wavetable_pool_load_file_impl(uint16_t wavetable_slot,
                                                        uint16_t forced_global_slot,
                                                        const char *path,
                                                        const char *register_path,
                                                        uint16_t *out_global_slot)
{
    FIL fp;
    UINT br = 0U;
    wavetable_file_header_t header;
    uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    sample_page_raw_allocation_t allocation;

    if (out_global_slot != 0)
    {
        *out_global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }
    if ((wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
        || (path == 0)
        || (path[0] == '\0')
        || (register_path == 0)
        || (register_path[0] == '\0'))
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_INVALID_ARG);
        return WAVETABLE_RESULT_INVALID_ARG;
    }
    if (wavetable_pool_copy_path(g_wavetable_pool.slots[wavetable_slot].path,
                                 sizeof(g_wavetable_pool.slots[wavetable_slot].path),
                                 register_path) == 0U)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_PATH_TOO_LONG);
        return WAVETABLE_RESULT_PATH_TOO_LONG;
    }
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_SD_BUSY);
        return WAVETABLE_RESULT_SD_BUSY;
    }
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_SD_MOUNT_FAIL);
        return WAVETABLE_RESULT_SD_MOUNT_FAIL;
    }
    if (f_open(&fp, path, FA_READ) != FR_OK)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_OPEN_FAIL);
        return WAVETABLE_RESULT_OPEN_FAIL;
    }
    if ((f_read(&fp, g_wavetable_pool_io, WAVETABLE_POOL_HEADER_SIZE, &br) != FR_OK)
        || (br != WAVETABLE_POOL_HEADER_SIZE))
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_BAD_FILE);
        return WAVETABLE_RESULT_BAD_FILE;
    }

    wavetable_pool_decode_header(g_wavetable_pool_io, &header);
    if (wavetable_pool_header_valid(&header) == 0U)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_UNSUPPORTED);
        return WAVETABLE_RESULT_UNSUPPORTED;
    }

    const uint32_t sample_count = header.frame_count * WAVETABLE_FRAME_SAMPLE_COUNT;
    if (sample_count > (UINT32_MAX / sizeof(int16_t)))
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_TOO_LARGE);
        return WAVETABLE_RESULT_TOO_LARGE;
    }
    const uint32_t data_bytes = sample_count * sizeof(int16_t);
    const uint32_t page_count =
        (data_bytes + SAMPLE_PAGE_BYTES - 1U) / SAMPLE_PAGE_BYTES;
    const uint32_t cost = page_count * SAMPLE_PAGE_BYTES;
    if ((cost == 0U) || (cost > sample_page_cache_slot_pool_total_bytes()))
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_TOO_LARGE);
        return WAVETABLE_RESULT_TOO_LARGE;
    }
    if ((forced_global_slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
        && (sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                               wavetable_slot,
                                               &global_slot) == 0U)
        && sample_global_pool_find_free_slot() >= SAMPLE_GLOBAL_POOL_MAX_SLOTS)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_GLOBAL_SLOT_FULL);
        return WAVETABLE_RESULT_GLOBAL_SLOT_FULL;
    }
    if (sample_global_pool_validate_budget(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                           wavetable_slot,
                                           cost) == 0U)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_GLOBAL_BUDGET_FULL);
        return WAVETABLE_RESULT_GLOBAL_BUDGET_FULL;
    }

    wavetable_pool_clear(wavetable_slot);
    memset(&allocation, 0, sizeof(allocation));
    if (sample_page_cache_alloc_slot_pool_bytes(data_bytes, &allocation) == 0U)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_RAM_POOL_FULL);
        return WAVETABLE_RESULT_RAM_POOL_FULL;
    }

    wavetable_slot_t *const slot = &g_wavetable_pool.slots[wavetable_slot];
    memset(slot, 0, sizeof(*slot));
    slot->state = WAVETABLE_SLOT_LOADING;
    slot->global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    (void)wavetable_pool_copy_path(slot->path, sizeof(slot->path), register_path);
    slot->format = WAVETABLE_FORMAT_S16_MONO;
    slot->frame_sample_count = WAVETABLE_FRAME_SAMPLE_COUNT;
    slot->frame_count = header.frame_count;
    slot->data_offset = (uint32_t)allocation.first_slot * SAMPLE_PAGE_BYTES;
    slot->first_page_slot = allocation.first_slot;
    slot->page_count = allocation.page_count;
    slot->generation = wavetable_pool_next_generation();
    slot->data_bytes = data_bytes;
    slot->cost_bytes_aligned = allocation.capacity_bytes;
    slot->data = (int16_t *)allocation.data;
    slot->error = WAVETABLE_RESULT_OK;
    wavetable_pool_preview_clear(&slot->preview);

    if (f_lseek(&fp, header.data_offset) != FR_OK)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_slot_error_at(wavetable_slot, WAVETABLE_RESULT_READ_FAIL, forced_global_slot);
        return WAVETABLE_RESULT_READ_FAIL;
    }

    const uint32_t source_bytes_per_sample = sizeof(int16_t);
    uint32_t samples_done = 0U;
    while (samples_done < sample_count)
    {
        uint32_t samples_chunk = WAVETABLE_POOL_IO_BYTES / source_bytes_per_sample;
        if (samples_chunk == 0U)
        {
            (void)f_close(&fp);
            sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
            wavetable_pool_slot_error_at(wavetable_slot, WAVETABLE_RESULT_READ_FAIL, forced_global_slot);
            return WAVETABLE_RESULT_READ_FAIL;
        }
        if (samples_chunk > (sample_count - samples_done))
        {
            samples_chunk = sample_count - samples_done;
        }

        const UINT bytes_to_read = (UINT)(samples_chunk * source_bytes_per_sample);
        if ((f_read(&fp, g_wavetable_pool_io, bytes_to_read, &br) != FR_OK)
            || (br != bytes_to_read))
        {
            (void)f_close(&fp);
            sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
            wavetable_pool_slot_error_at(wavetable_slot, WAVETABLE_RESULT_READ_FAIL, forced_global_slot);
            return WAVETABLE_RESULT_READ_FAIL;
        }

        int16_t *const dst = &slot->data[samples_done];
        const uint8_t *src = g_wavetable_pool_io;
        for (uint32_t i = 0U; i < samples_chunk; ++i)
        {
            dst[i] = wavetable_pool_s16_from_le(src);
            src += source_bytes_per_sample;
        }
        samples_done += samples_chunk;
    }

    (void)f_close(&fp);
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);

    const uint8_t registered =
        (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
            ? sample_global_pool_register_wavetable_at(forced_global_slot,
                                                       wavetable_slot,
                                                       register_path,
                                                       allocation.capacity_bytes)
            : sample_global_pool_register_wavetable(wavetable_slot,
                                                    register_path,
                                                    allocation.capacity_bytes,
                                                    &global_slot);
    if (registered == 0U)
    {
        wavetable_pool_clear(wavetable_slot);
        wavetable_pool_set_last(WAVETABLE_RESULT_REGISTER_FAIL);
        return WAVETABLE_RESULT_REGISTER_FAIL;
    }

    slot->global_slot = (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
                            ? forced_global_slot
                            : global_slot;
    wavetable_pool_preview_build(slot);
    slot->state = WAVETABLE_SLOT_READY;
    wavetable_pool_set_last(WAVETABLE_RESULT_OK);
    if (out_global_slot != 0)
    {
        *out_global_slot = slot->global_slot;
    }
    return WAVETABLE_RESULT_OK;
}

wavetable_result_t wavetable_pool_load_file(uint16_t wavetable_slot,
                                            const char *path,
                                            uint16_t *out_global_slot)
{
    return wavetable_pool_load_file_impl(wavetable_slot,
                                         SAMPLE_GLOBAL_POOL_INVALID_INDEX,
                                         path,
                                         path,
                                         out_global_slot);
}

static uint8_t wavetable_pool_wav_info_valid(const wav_info_t *info)
{
    if ((info == 0)
        || ((info->channels != 1U) && (info->channels != 2U))
        || ((info->bits_per_sample != 16U)
            && (info->bits_per_sample != 24U)
            && (info->bits_per_sample != 32U))
        || (info->block_align == 0U)
        || (info->data_size < info->block_align))
    {
        return 0U;
    }

    const uint16_t bytes_per_sample = (uint16_t)(info->bits_per_sample / 8U);
    if ((bytes_per_sample == 0U)
        || (info->block_align != (uint16_t)(info->channels * bytes_per_sample))
        || ((info->data_size % info->block_align) != 0U))
    {
        return 0U;
    }

    const uint32_t source_frames = info->data_size / info->block_align;
    return (uint8_t)((source_frames != 0U)
                     && ((source_frames % WAVETABLE_FRAME_SAMPLE_COUNT) == 0U));
}

static void wavetable_pool_write_cache_file(const char *cache_path,
                                            const wavetable_slot_t *slot,
                                            const FILINFO *source_info)
{
    FIL fp;
    UINT bw = 0U;
    if ((cache_path == 0)
        || (slot == 0)
        || (slot->state != WAVETABLE_SLOT_LOADING)
        || (slot->data == 0)
        || (slot->frame_count == 0U)
        || (source_info == 0))
    {
        return;
    }

    (void)f_mkdir("0:/WAVETABLES");
    (void)f_mkdir(WAVETABLE_POOL_CACHE_DIR);
    if (f_open(&fp, cache_path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        return;
    }

    wavetable_pool_encode_header(g_wavetable_pool_io,
                                 slot->frame_count,
                                 wavetable_pool_source_stamp(source_info),
                                 (uint32_t)source_info->fsize);
    if ((f_write(&fp, g_wavetable_pool_io, WAVETABLE_POOL_HEADER_SIZE, &bw) != FR_OK)
        || (bw != WAVETABLE_POOL_HEADER_SIZE))
    {
        (void)f_close(&fp);
        return;
    }

    const uint32_t data_bytes = slot->frame_count * WAVETABLE_FRAME_SAMPLE_COUNT * sizeof(int16_t);
    const uint8_t *src = (const uint8_t *)slot->data;
    uint32_t done = 0U;
    while (done < data_bytes)
    {
        uint32_t chunk = data_bytes - done;
        if (chunk > WAVETABLE_POOL_IO_BYTES)
        {
            chunk = WAVETABLE_POOL_IO_BYTES;
        }
        if ((f_write(&fp, &src[done], chunk, &bw) != FR_OK) || (bw != chunk))
        {
            (void)f_close(&fp);
            return;
        }
        done += chunk;
    }
    (void)f_sync(&fp);
    (void)f_close(&fp);
}

static wavetable_result_t wavetable_pool_load_wav_impl(uint16_t wavetable_slot,
                                                       uint16_t forced_global_slot,
                                                       const char *path,
                                                       uint16_t *out_global_slot)
{
    FIL fp;
    FILINFO source_info;
    wav_info_t wav_info;
    uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    sample_page_raw_allocation_t allocation;
    char cache_path[WAVETABLE_POOL_PATH_MAX];
    cache_path[0] = '\0';

    if (out_global_slot != 0)
    {
        *out_global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }
    if ((wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
        || ((forced_global_slot != SAMPLE_GLOBAL_POOL_INVALID_INDEX)
            && (forced_global_slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS))
        || (path == 0)
        || (path[0] == '\0'))
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_INVALID_ARG);
        return WAVETABLE_RESULT_INVALID_ARG;
    }
    if (wavetable_pool_copy_path(g_wavetable_pool.slots[wavetable_slot].path,
                                 sizeof(g_wavetable_pool.slots[wavetable_slot].path),
                                 path) == 0U)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_PATH_TOO_LONG);
        return WAVETABLE_RESULT_PATH_TOO_LONG;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_SD_BUSY);
        return WAVETABLE_RESULT_SD_BUSY;
    }
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_SD_MOUNT_FAIL);
        return WAVETABLE_RESULT_SD_MOUNT_FAIL;
    }
    if (f_stat(path, &source_info) != FR_OK)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_OPEN_FAIL);
        return WAVETABLE_RESULT_OPEN_FAIL;
    }
    if ((wavetable_pool_make_cache_path(cache_path, sizeof(cache_path), path, &source_info) != 0U)
        && (wavetable_pool_cache_valid(cache_path, &source_info) != 0U))
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        return wavetable_pool_load_file_impl(wavetable_slot,
                                             forced_global_slot,
                                             cache_path,
                                             path,
                                             out_global_slot);
    }

    if (f_open(&fp, path, FA_READ) != FR_OK)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_OPEN_FAIL);
        return WAVETABLE_RESULT_OPEN_FAIL;
    }
    if ((wav_parser_parse_info(&fp, &wav_info) == 0) || (wavetable_pool_wav_info_valid(&wav_info) == 0U))
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_UNSUPPORTED);
        return WAVETABLE_RESULT_UNSUPPORTED;
    }

    const uint32_t sample_count = wav_info.data_size / wav_info.block_align;
    if (sample_count > (UINT32_MAX / sizeof(int16_t)))
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_TOO_LARGE);
        return WAVETABLE_RESULT_TOO_LARGE;
    }
    const uint32_t data_bytes = sample_count * sizeof(int16_t);
    const uint32_t page_count = (data_bytes + SAMPLE_PAGE_BYTES - 1U) / SAMPLE_PAGE_BYTES;
    const uint32_t cost = page_count * SAMPLE_PAGE_BYTES;
    if ((cost == 0U) || (cost > sample_page_cache_slot_pool_total_bytes()))
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_TOO_LARGE);
        return WAVETABLE_RESULT_TOO_LARGE;
    }
    if ((forced_global_slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
        && (sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                               wavetable_slot,
                                               &global_slot) == 0U)
        && sample_global_pool_find_free_slot() >= SAMPLE_GLOBAL_POOL_MAX_SLOTS)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_GLOBAL_SLOT_FULL);
        return WAVETABLE_RESULT_GLOBAL_SLOT_FULL;
    }
    if (sample_global_pool_validate_budget(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                           wavetable_slot,
                                           cost) == 0U)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_GLOBAL_BUDGET_FULL);
        return WAVETABLE_RESULT_GLOBAL_BUDGET_FULL;
    }

    wavetable_pool_clear(wavetable_slot);
    memset(&allocation, 0, sizeof(allocation));
    if (sample_page_cache_alloc_slot_pool_bytes(data_bytes, &allocation) == 0U)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_set_last(WAVETABLE_RESULT_RAM_POOL_FULL);
        return WAVETABLE_RESULT_RAM_POOL_FULL;
    }

    wavetable_slot_t *const slot = &g_wavetable_pool.slots[wavetable_slot];
    memset(slot, 0, sizeof(*slot));
    slot->state = WAVETABLE_SLOT_LOADING;
    slot->global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    (void)wavetable_pool_copy_path(slot->path, sizeof(slot->path), path);
    slot->format = WAVETABLE_FORMAT_S16_MONO;
    slot->frame_sample_count = WAVETABLE_FRAME_SAMPLE_COUNT;
    slot->frame_count = sample_count / WAVETABLE_FRAME_SAMPLE_COUNT;
    slot->data_offset = (uint32_t)allocation.first_slot * SAMPLE_PAGE_BYTES;
    slot->first_page_slot = allocation.first_slot;
    slot->page_count = allocation.page_count;
    slot->generation = wavetable_pool_next_generation();
    slot->data_bytes = data_bytes;
    slot->cost_bytes_aligned = allocation.capacity_bytes;
    slot->data = (int16_t *)allocation.data;
    slot->error = WAVETABLE_RESULT_OK;
    wavetable_pool_preview_clear(&slot->preview);

    if (f_lseek(&fp, wav_info.data_offset) != FR_OK)
    {
        (void)f_close(&fp);
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        wavetable_pool_slot_error_at(wavetable_slot, WAVETABLE_RESULT_READ_FAIL, forced_global_slot);
        return WAVETABLE_RESULT_READ_FAIL;
    }

    uint32_t frames_done = 0U;
    while (frames_done < sample_count)
    {
        uint32_t frames_chunk = WAVETABLE_POOL_IO_BYTES / wav_info.block_align;
        if (frames_chunk == 0U)
        {
            (void)f_close(&fp);
            sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
            wavetable_pool_slot_error_at(wavetable_slot, WAVETABLE_RESULT_READ_FAIL, forced_global_slot);
            return WAVETABLE_RESULT_READ_FAIL;
        }
        if (frames_chunk > (sample_count - frames_done))
        {
            frames_chunk = sample_count - frames_done;
        }

        const UINT bytes_to_read = (UINT)(frames_chunk * wav_info.block_align);
        UINT br = 0U;
        if ((f_read(&fp, g_wavetable_pool_io, bytes_to_read, &br) != FR_OK)
            || (br != bytes_to_read))
        {
            (void)f_close(&fp);
            sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
            wavetable_pool_slot_error_at(wavetable_slot, WAVETABLE_RESULT_READ_FAIL, forced_global_slot);
            return WAVETABLE_RESULT_READ_FAIL;
        }

        const uint8_t *src = g_wavetable_pool_io;
        int16_t *const dst = &slot->data[frames_done];
        for (uint32_t i = 0U; i < frames_chunk; ++i)
        {
            float left = 0.0f;
            float right = 0.0f;
            wav_audio_codec_decode_stereo_frame(src,
                                                wav_info.channels,
                                                wav_info.bits_per_sample,
                                                &left,
                                                &right);
            dst[i] = wavetable_pool_float_to_s16(
                (wav_info.channels == 1U) ? left : ((left + right) * 0.5f));
            src += wav_info.block_align;
        }
        frames_done += frames_chunk;
    }

    (void)f_close(&fp);
    if (cache_path[0] != '\0')
    {
        wavetable_pool_write_cache_file(cache_path, slot, &source_info);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);

    const uint8_t registered =
        (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
            ? sample_global_pool_register_wavetable_at(forced_global_slot,
                                                       wavetable_slot,
                                                       path,
                                                       allocation.capacity_bytes)
            : sample_global_pool_register_wavetable(wavetable_slot,
                                                    path,
                                                    allocation.capacity_bytes,
                                                    &global_slot);
    if (registered == 0U)
    {
        wavetable_pool_clear(wavetable_slot);
        wavetable_pool_set_last(WAVETABLE_RESULT_REGISTER_FAIL);
        return WAVETABLE_RESULT_REGISTER_FAIL;
    }

    slot->global_slot = (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
                            ? forced_global_slot
                            : global_slot;
    wavetable_pool_preview_build(slot);
    slot->state = WAVETABLE_SLOT_READY;
    wavetable_pool_set_last(WAVETABLE_RESULT_OK);
    if (out_global_slot != 0)
    {
        *out_global_slot = slot->global_slot;
    }
    return WAVETABLE_RESULT_OK;
}

wavetable_result_t wavetable_pool_load_wav(uint16_t wavetable_slot,
                                           const char *path,
                                           uint16_t *out_global_slot)
{
    return wavetable_pool_load_wav_impl(wavetable_slot,
                                        SAMPLE_GLOBAL_POOL_INVALID_INDEX,
                                        path,
                                        out_global_slot);
}

wavetable_result_t wavetable_pool_load_file_at(uint16_t wavetable_slot,
                                               uint16_t global_slot,
                                               const char *path)
{
    if (global_slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_INVALID_ARG);
        return WAVETABLE_RESULT_INVALID_ARG;
    }
    if (wavetable_pool_path_ext_is_wav(path) != 0U)
    {
        uint16_t loaded_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const wavetable_result_t result =
            wavetable_pool_load_wav_impl(wavetable_slot, global_slot, path, &loaded_global);
        if ((result != WAVETABLE_RESULT_OK)
            && (wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
            && (path != 0)
            && (path[0] != '\0'))
        {
            (void)wavetable_pool_copy_path(g_wavetable_pool.slots[wavetable_slot].path,
                                           sizeof(g_wavetable_pool.slots[wavetable_slot].path),
                                           path);
            wavetable_pool_slot_error_at(wavetable_slot, result, global_slot);
        }
        return result;
    }

    uint16_t loaded_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    const wavetable_result_t result =
        wavetable_pool_load_file_impl(wavetable_slot, global_slot, path, path, &loaded_global);
    if ((result != WAVETABLE_RESULT_OK)
        && (wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
        && (path != 0)
        && (path[0] != '\0'))
    {
        (void)wavetable_pool_copy_path(g_wavetable_pool.slots[wavetable_slot].path,
                                       sizeof(g_wavetable_pool.slots[wavetable_slot].path),
                                       path);
        wavetable_pool_slot_error_at(wavetable_slot, result, global_slot);
    }
    return result;
}

void wavetable_pool_clear(uint16_t wavetable_slot)
{
    if (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
    {
        return;
    }

    const wavetable_slot_t *const old = &g_wavetable_pool.slots[wavetable_slot];
    const uint32_t generation = wavetable_pool_next_generation();
    g_wavetable_pool.slots[wavetable_slot].generation = generation;
    if (old->page_count != 0U)
    {
        sample_page_cache_release_slot_pool_allocation(old->first_page_slot,
                                                       old->page_count);
    }
    sample_global_pool_clear_backend(SAMPLE_GLOBAL_KIND_WAVETABLE, wavetable_slot);
    memset(&g_wavetable_pool.slots[wavetable_slot], 0, sizeof(g_wavetable_pool.slots[wavetable_slot]));
    g_wavetable_pool.slots[wavetable_slot].state = WAVETABLE_SLOT_EMPTY;
    g_wavetable_pool.slots[wavetable_slot].global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    g_wavetable_pool.slots[wavetable_slot].first_page_slot = UINT16_MAX;
    g_wavetable_pool.slots[wavetable_slot].generation = generation;
    wavetable_pool_preview_clear(&g_wavetable_pool.slots[wavetable_slot].preview);
}

const wavetable_slot_t *wavetable_pool_get_slot(uint16_t wavetable_slot)
{
    if (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
    {
        return 0;
    }
    return &g_wavetable_pool.slots[wavetable_slot];
}

wavetable_slot_state_t wavetable_pool_get_state(uint16_t wavetable_slot)
{
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(wavetable_slot);
    return (slot != 0) ? slot->state : WAVETABLE_SLOT_ERROR;
}

const int16_t *wavetable_pool_get_data(uint16_t wavetable_slot)
{
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(wavetable_slot);
    return ((slot != 0) && (slot->state == WAVETABLE_SLOT_READY)) ? slot->data : 0;
}

const wavetable_preview_t *wavetable_pool_get_preview(uint16_t wavetable_slot)
{
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(wavetable_slot);
    if ((slot == 0)
            || (slot->state != WAVETABLE_SLOT_READY)
            || (slot->preview.state != WAVETABLE_PREVIEW_READY)
            || (slot->preview.generation != slot->generation))
    {
        return 0;
    }
    return &slot->preview;
}

const wavetable_preview_t *wavetable_pool_get_preview_for_global(uint16_t global_slot)
{
    uint16_t wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    if (sample_global_pool_resolve_backend(global_slot,
                                           SAMPLE_GLOBAL_KIND_WAVETABLE,
                                           &wavetable_slot) == 0U)
    {
        return 0;
    }
    return wavetable_pool_get_preview(wavetable_slot);
}

uint32_t wavetable_pool_get_used_bytes(void)
{
    const uint32_t total = sample_page_cache_slot_pool_total_bytes();
    const uint32_t free_bytes = sample_page_cache_slot_pool_free_bytes();
    return (free_bytes >= total) ? 0U : (total - free_bytes);
}

uint32_t wavetable_pool_get_free_bytes(void)
{
    return sample_page_cache_slot_pool_free_bytes();
}

wavetable_result_t wavetable_pool_get_last_result(void)
{
    return g_wavetable_pool.last_result;
}

const char *wavetable_pool_result_label(wavetable_result_t result)
{
    switch (result)
    {
        case WAVETABLE_RESULT_OK:
            return "LOAD OK";
        case WAVETABLE_RESULT_POOL_FULL:
        case WAVETABLE_RESULT_GLOBAL_SLOT_FULL:
            return "SLOT FULL";
        case WAVETABLE_RESULT_GLOBAL_BUDGET_FULL:
        case WAVETABLE_RESULT_RAM_POOL_FULL:
            return "MEM FULL";
        case WAVETABLE_RESULT_TOO_LARGE:
            return "TOO LARGE";
        case WAVETABLE_RESULT_PATH_TOO_LONG:
            return "PATH LONG";
        case WAVETABLE_RESULT_SD_BUSY:
            return "SD BUSY";
        case WAVETABLE_RESULT_SD_MOUNT_FAIL:
            return "SD UNAVAILABLE";
        case WAVETABLE_RESULT_OPEN_FAIL:
            return "OPEN FAIL";
        case WAVETABLE_RESULT_BAD_FILE:
            return "BAD WT";
        case WAVETABLE_RESULT_UNSUPPORTED:
            return "UNSUPPORTED";
        case WAVETABLE_RESULT_READ_FAIL:
            return "SD READ FAIL";
        case WAVETABLE_RESULT_REGISTER_FAIL:
            return "REGISTER FAIL";
        case WAVETABLE_RESULT_INVALID_ARG:
        default:
            return "LOAD FAIL";
    }
}
