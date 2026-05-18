#include "Sampler/sample_stream_manager.h"

#include <string.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_sector_scratch.h"
#include "Storage/sd_block_device.h"
#include "Storage/memory_layout.h"
#include "Storage/wav_audio_codec.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#define SAMPLE_STREAM_PENDING_MAX SAMPLE_PAGE_MAX_COUNT
#define SAMPLE_STREAM_SERVICE_MAX_PAGES (2U)
#define SAMPLE_STREAM_SERVICE_MAX_FATFS_OPS (8U)
#define SAMPLE_STREAM_SERVICE_MAX_TICKS (1U)
#define SAMPLE_STREAM_MANAGER_SERVICE_BACKEND_NONE   (0U)
#define SAMPLE_STREAM_MANAGER_SERVICE_BACKEND_SECTOR (1U)
#define SAMPLE_STREAM_MANAGER_SERVICE_BACKEND_FATFS  (2U)

#define SAMPLE_STREAM_MANAGER_SERVICE_RESULT_OK       (1U)
#define SAMPLE_STREAM_MANAGER_SERVICE_RESULT_FAIL     (3U)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_CACHE_HOT_SAMPLE_CAPACITY <= SAMPLE_PAGE_CACHE_ID_CAPACITY,
               "stream manager hot scan range must fit in page-cache ids");
_Static_assert(SAMPLE_STREAM_MAX_ACTIVE <= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY,
               "active stream readers must be bounded below hot sample capacity");
#endif

typedef enum
{
    SAMPLE_STREAM_BACKEND_OK = 0,
    SAMPLE_STREAM_BACKEND_FALLBACK,
    SAMPLE_STREAM_BACKEND_READ_FAIL,
    SAMPLE_STREAM_BACKEND_DECODE_FAIL,
    SAMPLE_STREAM_BACKEND_INVALID_META,
    SAMPLE_STREAM_BACKEND_UNSUPPORTED_FORMAT,
    SAMPLE_STREAM_BACKEND_SCRATCH_TOO_SMALL,
    SAMPLE_STREAM_BACKEND_FATFS_OK
} sample_stream_backend_result_t;

typedef struct
{
    uint8_t in_use;
    uint8_t file_open;
    sample_audio_key_t key;
    uint16_t sample_id;
    FIL file;
    char path[SAMPLE_PAGE_CACHE_PATH_MAX];
    uint32_t data_offset;
    uint32_t total_frames;
    uint32_t bytes_per_frame;
    FSIZE_t current_file_offset;
    uint32_t last_page_index;
} sample_stream_reader_t;

typedef struct
{
    uint8_t active;
    sample_audio_key_t key;
    uint16_t sample_id;
    uint32_t page_index;
    uint32_t requested_at;
    uint16_t distance_pages;
} sample_stream_pending_t;

typedef struct
{
    uint32_t read_start_us;
    uint32_t read_end_us;
    uint32_t decode_end_us;
} sample_stream_page_timing_t;

typedef struct
{
    uint32_t request_page_calls;
    uint32_t request_range_calls;
    uint32_t service_calls;
    uint32_t has_pending_calls;
    uint32_t pages_requested;
    uint32_t pages_loaded;
    uint32_t pages_failed;
    uint32_t open_failures;
    uint32_t seek_failures;
    uint32_t read_failures;
    uint32_t close_failures;
    uint32_t reader_allocations;
    uint32_t reader_full;
    uint32_t reader_opens;
    uint32_t reader_closes;
    uint32_t sequential_reads;
    uint32_t seek_reads;
    uint32_t backend_sector_pages;
    uint32_t backend_fatfs_pages;
    uint32_t backend_fallback_pages;
    uint32_t backend_sector_pages_by_domain[SAMPLE_AUDIO_DOMAIN_COUNT];
    uint32_t backend_fatfs_pages_by_domain[SAMPLE_AUDIO_DOMAIN_COUNT];
    uint32_t backend_fallback_pages_by_domain[SAMPLE_AUDIO_DOMAIN_COUNT];
    uint32_t backend_sector_read_fail;
    uint32_t backend_sector_decode_fail;
    uint32_t backend_sector_invalid_meta;
    uint32_t backend_sector_scratch_too_small;
    uint32_t backend_sector_unsupported_format;
    uint32_t backend_sector_last_lba;
    uint32_t backend_sector_last_sector_count;
    uint32_t backend_sector_last_sector_offset;
    uint32_t backend_sector_last_payload_bytes;
    uint32_t last_backend_result;
    sample_audio_key_t last_backend_key;
    sample_audio_key_t last_service_key;
    uint32_t last_service_page;
    uint32_t pending_count;
    uint32_t last_service_pending_before;
    uint32_t last_service_pending_after;
    uint8_t last_service_backend;
    uint8_t last_service_result;
    uint8_t last_service_page_state;
    uint32_t selected_sample_id;
    uint32_t pick_scan_max;
    uint32_t pick_no_work;
    uint32_t pending_stale_dropped;
    uint32_t pending_invalid_sample;
    uint32_t pending_not_loadable;
    uint32_t pending_dropped_ready;
    uint32_t pending_dropped_loading;
    uint32_t pending_dropped_non_stream;
    uint32_t has_pending_stale_cleaned;
    uint32_t service_no_loadable_work;
    uint32_t service_time_max_ticks;
    uint32_t gate_hold_time_max_ticks;
    uint32_t service_budget_exhausted;
    uint32_t service_time_yield;
    uint32_t pages_per_call_max;
    uint64_t bytes_read;
} sample_stream_manager_counters_t;

static CTRL_STATE sample_stream_manager_counters_t g_sample_stream_manager_diag;
static sample_stream_reader_t g_sample_stream_readers[SAMPLE_STREAM_MAX_ACTIVE];
static sample_stream_pending_t g_sample_stream_pending[SAMPLE_STREAM_PENDING_MAX];
static uint32_t g_sample_stream_request_clock;
static uint32_t g_sample_stream_service_fatfs_ops;
static uint16_t g_sample_stream_next_sample_id;
static volatile uint8_t *g_sample_stream_sector_scratch_anchor;
static uint32_t sample_stream_manager_now_us(void)
{
    if (((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) && (SystemCoreClock != 0U))
    {
        return (uint32_t)(((uint64_t)DWT->CYCCNT * 1000000ULL)
                          / (uint64_t)SystemCoreClock);
    }

    return HAL_GetTick() * 1000U;
}

static void sample_stream_manager_diag_max_u32(uint32_t *field, uint32_t value)
{
    if ((field != 0) && (value > *field))
    {
        *field = value;
    }
}

static uint8_t sample_stream_manager_domain_index(sample_audio_key_t key, uint32_t *out_index)
{
    if ((out_index == 0) || ((uint32_t)key.domain >= SAMPLE_AUDIO_DOMAIN_COUNT))
    {
        return 0U;
    }

    *out_index = (uint32_t)key.domain;
    return 1U;
}

static void sample_stream_manager_diag_inc_domain(uint32_t counts[SAMPLE_AUDIO_DOMAIN_COUNT],
                                                  sample_audio_key_t key)
{
    uint32_t domain_index = 0U;
    if (sample_stream_manager_domain_index(key, &domain_index) != 0U)
    {
        counts[domain_index]++;
    }
}

static uint32_t sample_stream_manager_pending_count(void)
{
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        if (g_sample_stream_pending[i].active != 0U)
        {
            count++;
        }
    }
    return count;
}

static sample_stream_pending_t *sample_stream_manager_find_pending_key(sample_audio_key_t key,
                                                                       uint32_t page_index)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        sample_stream_pending_t *const pending = &g_sample_stream_pending[i];
        if ((pending->active != 0U)
            && (sample_audio_key_equal(&pending->key, &key) != 0U)
            && (pending->page_index == page_index))
        {
            return pending;
        }
    }

    return 0;
}

static void sample_stream_manager_note_multi_service(const sample_page_load_target_t *target,
                                                     uint8_t backend,
                                                     uint8_t result,
                                                     sample_page_state_t page_state,
                                                     uint32_t pending_before)
{
    if ((target == 0) || (target->key.domain != SAMPLE_AUDIO_DOMAIN_MULTI))
    {
        return;
    }

    g_sample_stream_manager_diag.last_service_key = target->key;
    g_sample_stream_manager_diag.last_service_page = target->page_index;
    g_sample_stream_manager_diag.last_service_backend = backend;
    g_sample_stream_manager_diag.last_service_result = result;
    g_sample_stream_manager_diag.last_service_page_state = (uint8_t)page_state;
    g_sample_stream_manager_diag.last_service_pending_before = pending_before;
    g_sample_stream_manager_diag.last_service_pending_after = sample_stream_manager_pending_count();
    g_sample_stream_manager_diag.pending_count =
        g_sample_stream_manager_diag.last_service_pending_after;
    (void)backend;
    (void)result;
}

static void sample_stream_manager_record_service_ticks(uint32_t start_tick)
{
    const uint32_t elapsed_ticks = HAL_GetTick() - start_tick;
    sample_stream_manager_diag_max_u32(&g_sample_stream_manager_diag.service_time_max_ticks,
                                       elapsed_ticks);
    sample_stream_manager_diag_max_u32(&g_sample_stream_manager_diag.gate_hold_time_max_ticks,
                                       elapsed_ticks);
}

static uint8_t sample_stream_manager_service_should_yield(uint32_t start_tick,
                                                          uint32_t pages_this_call)
{
    const uint32_t elapsed_ticks = HAL_GetTick() - start_tick;

    if (pages_this_call >= SAMPLE_STREAM_SERVICE_MAX_PAGES)
    {
        return 1U;
    }
    if (g_sample_stream_service_fatfs_ops >= SAMPLE_STREAM_SERVICE_MAX_FATFS_OPS)
    {
        return 1U;
    }
    return (elapsed_ticks >= SAMPLE_STREAM_SERVICE_MAX_TICKS) ? 1U : 0U;
}

static void sample_stream_manager_close_reader(sample_stream_reader_t *reader)
{
    if (reader == 0)
    {
        return;
    }

    if (reader->file_open != 0U)
    {
        g_sample_stream_service_fatfs_ops++;
        const FRESULT close_fr = f_close(&reader->file);
        if (close_fr != FR_OK)
        {
            g_sample_stream_manager_diag.close_failures++;
        }
        g_sample_stream_manager_diag.reader_closes++;
        reader->file_open = 0U;
    }
}

static void sample_stream_manager_clear_reader(sample_stream_reader_t *reader)
{
    if (reader == 0)
    {
        return;
    }

    sample_stream_manager_close_reader(reader);
    memset(reader, 0, sizeof(*reader));
    reader->sample_id = UINT16_MAX;
    reader->key = sample_audio_key_classic(UINT16_MAX);
    reader->last_page_index = UINT32_MAX;
}

static uint8_t sample_stream_manager_reader_matches(const sample_stream_reader_t *reader,
                                                    sample_audio_key_t key,
                                                    const sample_page_stream_info_t *info)
{
    if ((reader == 0) || (info == 0) || (reader->in_use == 0U)
        || (sample_audio_key_equal(&reader->key, &key) == 0U))
    {
        return 0U;
    }

    return ((reader->data_offset == info->data_offset)
            && (reader->total_frames == info->total_frames)
            && (reader->bytes_per_frame == info->info.block_align)
            && (strncmp(reader->path, info->path, sizeof(reader->path)) == 0)) ? 1U : 0U;
}

static sample_stream_reader_t *sample_stream_manager_find_reader_key(sample_audio_key_t key)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_MAX_ACTIVE; ++i)
    {
        if ((g_sample_stream_readers[i].in_use != 0U)
            && (sample_audio_key_equal(&g_sample_stream_readers[i].key, &key) != 0U))
        {
            return &g_sample_stream_readers[i];
        }
    }

    return 0;
}

static sample_stream_reader_t *sample_stream_manager_get_reader(
    sample_audio_key_t key,
    const sample_page_stream_info_t *info)
{
    if ((info == 0) || (info->info.block_align == 0U))
    {
        return 0;
    }

    sample_stream_reader_t *reader = sample_stream_manager_find_reader_key(key);
    if (reader != 0)
    {
        if (sample_stream_manager_reader_matches(reader, key, info) == 0U)
        {
            sample_stream_manager_clear_reader(reader);
        }
        else
        {
            return reader;
        }
    }

    for (uint32_t i = 0U; i < SAMPLE_STREAM_MAX_ACTIVE; ++i)
    {
        if (g_sample_stream_readers[i].in_use == 0U)
        {
            reader = &g_sample_stream_readers[i];
            memset(reader, 0, sizeof(*reader));
            reader->in_use = 1U;
            reader->key = key;
            reader->sample_id = key.object_id;
            memcpy(reader->path, info->path, sizeof(reader->path));
            reader->data_offset = info->data_offset;
            reader->total_frames = info->total_frames;
            reader->bytes_per_frame = info->info.block_align;
            reader->last_page_index = UINT32_MAX;
            reader->current_file_offset = 0U;
            g_sample_stream_manager_diag.reader_allocations++;
            return reader;
        }
    }

    g_sample_stream_manager_diag.reader_full++;
    return 0;
}

static uint8_t sample_stream_manager_open_reader(sample_stream_reader_t *reader)
{
    if (reader == 0)
    {
        return 0U;
    }

    if (reader->file_open != 0U)
    {
        return 1U;
    }

    g_sample_stream_service_fatfs_ops++;
    const FRESULT open_fr = f_open(&reader->file, reader->path, FA_READ);
    if (open_fr != FR_OK)
    {
        g_sample_stream_manager_diag.open_failures++;
        return 0U;
    }

    reader->file_open = 1U;
    reader->current_file_offset = 0U;
    reader->last_page_index = UINT32_MAX;
    g_sample_stream_manager_diag.reader_opens++;
    return 1U;
}

static void sample_stream_manager_clear_pending_key(sample_audio_key_t key, uint32_t page_index)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        sample_stream_pending_t *const pending = &g_sample_stream_pending[i];
        if ((pending->active != 0U)
            && (sample_audio_key_equal(&pending->key, &key) != 0U)
            && (pending->page_index == page_index))
        {
            memset(pending, 0, sizeof(*pending));
            return;
        }
    }
}

static void sample_stream_manager_clear_pending_key_all(sample_audio_key_t key)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        sample_stream_pending_t *const pending = &g_sample_stream_pending[i];
        if ((pending->active != 0U)
            && (sample_audio_key_equal(&pending->key, &key) != 0U))
        {
            memset(pending, 0, sizeof(*pending));
        }
    }
}

static void sample_stream_manager_drop_pending_slot(sample_stream_pending_t *pending)
{
    if ((pending == 0) || (pending->active == 0U))
    {
        return;
    }

    memset(pending, 0, sizeof(*pending));
    g_sample_stream_manager_diag.pending_stale_dropped++;
}

static uint8_t sample_stream_manager_pending_would_improve(
    const sample_stream_pending_t *pending,
    uint16_t distance_pages)
{
    if (pending == 0)
    {
        return 1U;
    }
    return (distance_pages < pending->distance_pages) ? 1U : 0U;
}

static void sample_stream_manager_note_pending_key(sample_audio_key_t key,
                                               uint32_t page_index,
                                               uint16_t distance_pages)
{
    sample_stream_pending_t *free_slot = 0;

    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        sample_stream_pending_t *const pending = &g_sample_stream_pending[i];
        if (pending->active == 0U)
        {
            if (free_slot == 0)
            {
                free_slot = pending;
            }
            continue;
        }

        if ((sample_audio_key_equal(&pending->key, &key) != 0U)
            && (pending->page_index == page_index))
        {
            if (sample_stream_manager_pending_would_improve(pending, distance_pages) != 0U)
            {
                pending->distance_pages = distance_pages;
            }
            return;
        }
    }

    if (free_slot != 0)
    {
        free_slot->active = 1U;
        free_slot->key = key;
        free_slot->sample_id = key.object_id;
        free_slot->page_index = page_index;
        free_slot->requested_at = ++g_sample_stream_request_clock;
        free_slot->distance_pages = distance_pages;
        return;
    }
}

static void sample_stream_manager_note_requested_page_key(sample_audio_key_t key,
                                                      uint32_t page_index,
                                                      uint16_t distance_pages)
{
    const sample_page_state_t state = sample_page_cache_get_page_state_key(key, page_index);
    if (state == SAMPLE_PAGE_QUEUED)
    {
        g_sample_stream_manager_diag.pages_requested++;
        sample_stream_manager_note_pending_key(key,
                                               page_index,
                                               distance_pages);
    }
    else if (state == SAMPLE_PAGE_READY)
    {
        g_sample_stream_manager_diag.pending_dropped_ready++;
    }
    else if (state == SAMPLE_PAGE_LOADING)
    {
        g_sample_stream_manager_diag.pending_dropped_loading++;
    }
    else
    {
        g_sample_stream_manager_diag.pending_not_loadable++;
    }
}

static uint8_t sample_stream_manager_candidate_is_better(uint8_t have_best,
                                                         uint16_t distance_pages,
                                                         uint32_t age,
                                                         uint16_t best_distance_pages,
                                                         uint32_t best_age)
{
    if (have_best == 0U)
    {
        return 1U;
    }
    if (distance_pages < best_distance_pages)
    {
        return 1U;
    }
    if (distance_pages > best_distance_pages)
    {
        return 0U;
    }
    return (age > best_age) ? 1U : 0U;
}

static uint8_t sample_stream_manager_pick_next(sample_page_load_target_t *out_target)
{
    uint8_t found = 0U;
    uint32_t scan_count = 0U;
    uint32_t best_age = 0U;
    uint16_t best_distance_pages = UINT16_MAX;
    sample_page_load_target_t best_target;
    uint8_t pending_seen = 0U;

    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        scan_count++;
        sample_stream_pending_t *const pending = &g_sample_stream_pending[i];
        if (pending->active == 0U)
        {
            continue;
        }
        pending_seen = 1U;

        sample_page_load_target_t target;
        if (sample_page_cache_get_load_target_key(pending->key,
                                              pending->page_index,
                                              &target) == 0U)
        {
            g_sample_stream_manager_diag.pending_not_loadable++;
            sample_stream_manager_drop_pending_slot(pending);
            continue;
        }

        const uint32_t age = g_sample_stream_request_clock - pending->requested_at;

        if (sample_stream_manager_candidate_is_better(found,
                                                      pending->distance_pages,
                                                      age,
                                                      best_distance_pages,
                                                      best_age) != 0U)
        {
            best_target = target;
            best_distance_pages = pending->distance_pages;
            best_age = age;
            found = 1U;
        }
    }

    sample_stream_manager_diag_max_u32(&g_sample_stream_manager_diag.pick_scan_max, scan_count);

    if (found != 0U)
    {
        if (out_target != 0)
        {
            *out_target = best_target;
        }
        g_sample_stream_next_sample_id =
            (uint16_t)((best_target.key.object_id + 1U) % SAMPLE_CACHE_HOT_SAMPLE_CAPACITY);
        return 1U;
    }

    if (pending_seen != 0U)
    {
        g_sample_stream_manager_diag.pick_no_work++;
        return 0U;
    }

    g_sample_stream_manager_diag.pick_no_work++;
    return 0U;
}

static float sample_stream_manager_decode_s24_le(const uint8_t *src)
{
    int32_t value = ((int32_t)src[0]) | (((int32_t)src[1]) << 8) | (((int32_t)src[2]) << 16);
    if ((value & 0x00800000L) != 0)
    {
        value |= (int32_t)0xFF000000L;
    }
    return (float)value * (1.0f / 8388608.0f);
}

static uint8_t sample_stream_manager_wav_format_supported_direct(
    const sample_page_stream_info_t *info)
{
    if (info == 0)
    {
        return 0U;
    }

    return (((info->info.audio_format == 1U) || (info->info.audio_format == 65534U))
            && ((info->info.channels == 1U) || (info->info.channels == 2U))
            && ((info->info.bits_per_sample == 16U)
                || (info->info.bits_per_sample == 24U)
                || (info->info.bits_per_sample == 32U))
            && (info->info.sample_rate == 48000U)
            && (info->info.block_align != 0U)) ? 1U : 0U;
}

static sample_stream_backend_result_t sample_stream_manager_load_contiguous_sector_page(
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target,
    uint8_t *sector_scratch,
    uint32_t sector_scratch_size,
    sample_stream_page_timing_t *out_timing)
{
    if (out_timing != 0)
    {
        memset(out_timing, 0, sizeof(*out_timing));
    }

    if ((info == 0) || (target == 0) || (target->frames_interleaved == 0)
        || (sector_scratch == 0))
    {
        return SAMPLE_STREAM_BACKEND_INVALID_META;
    }

    if ((info->raw_pcm24 != 0U)
        || (sample_stream_manager_wav_format_supported_direct(info) == 0U))
    {
        return SAMPLE_STREAM_BACKEND_UNSUPPORTED_FORMAT;
    }

    const sample_stream_source_meta_t *const source = &info->source;
    if ((source->valid == 0U)
        || (source->safe_state != (uint8_t)SAMPLE_STREAM_SAFE_CONTIGUOUS)
        || (source->contig == 0U)
        || (source->first_data_lba == 0U)
        || (source->block_align == 0U)
        || (source->bytes_per_frame == 0U)
        || (source->block_align != info->info.block_align)
        || (source->bytes_per_frame != info->info.block_align)
        || (source->channels != info->info.channels)
        || (source->bits_per_sample != info->info.bits_per_sample)
        || (source->sample_rate != info->info.sample_rate)
        || (source->total_frames != info->total_frames)
        || (source->data_offset_bytes != info->data_offset)
        || (source->data_size_bytes != info->info.data_size))
    {
        return SAMPLE_STREAM_BACKEND_INVALID_META;
    }

    if ((target->frame_count == 0U) || (target->start_frame >= source->total_frames)
        || (target->frame_count > (source->total_frames - target->start_frame)))
    {
        return SAMPLE_STREAM_BACKEND_INVALID_META;
    }

    const uint32_t block_align = info->info.block_align;
    if ((target->start_frame > (UINT32_MAX / block_align))
        || (target->frame_count > (UINT32_MAX / block_align)))
    {
        return SAMPLE_STREAM_BACKEND_INVALID_META;
    }

    const uint32_t page_data_byte = target->start_frame * block_align;
    const uint32_t payload_bytes = target->frame_count * block_align;
    if ((page_data_byte > source->data_size_bytes)
        || (payload_bytes > (source->data_size_bytes - page_data_byte))
        || (source->data_sector_offset >= SD_BLOCK_DEVICE_SECTOR_BYTES)
        || (page_data_byte > (UINT32_MAX - source->data_sector_offset)))
    {
        return SAMPLE_STREAM_BACKEND_INVALID_META;
    }

    const uint32_t absolute_data_byte = source->data_sector_offset + page_data_byte;
    const uint32_t first_lba =
        source->first_data_lba + (absolute_data_byte / SD_BLOCK_DEVICE_SECTOR_BYTES);
    const uint32_t sector_offset = absolute_data_byte % SD_BLOCK_DEVICE_SECTOR_BYTES;
    const uint32_t read_bytes = sector_offset + payload_bytes;
    const uint32_t sector_count =
        (read_bytes + SD_BLOCK_DEVICE_SECTOR_BYTES - 1U) / SD_BLOCK_DEVICE_SECTOR_BYTES;
    const uint32_t sector_bytes = sector_count * SD_BLOCK_DEVICE_SECTOR_BYTES;
    g_sample_stream_manager_diag.backend_sector_last_lba = first_lba;
    g_sample_stream_manager_diag.backend_sector_last_sector_count = sector_count;
    g_sample_stream_manager_diag.backend_sector_last_sector_offset = sector_offset;
    g_sample_stream_manager_diag.backend_sector_last_payload_bytes = payload_bytes;

    if ((sector_count == 0U) || (sector_bytes > sector_scratch_size))
    {
        return SAMPLE_STREAM_BACKEND_SCRATCH_TOO_SMALL;
    }

    const uint32_t read_start_us = sample_stream_manager_now_us();
    const sd_block_device_result_t read_result =
        sd_block_device_read_sectors(first_lba, sector_count, sector_scratch, sector_bytes);
    const uint32_t read_end_us = sample_stream_manager_now_us();
    if (out_timing != 0)
    {
        out_timing->read_start_us = read_start_us;
        out_timing->read_end_us = read_end_us;
    }
    if (read_result != SD_BLOCK_DEVICE_OK)
    {
        return SAMPLE_STREAM_BACKEND_READ_FAIL;
    }

    const uint8_t *src = &sector_scratch[sector_offset];
    if ((sector_offset + payload_bytes) > sector_bytes)
    {
        return SAMPLE_STREAM_BACKEND_DECODE_FAIL;
    }

    for (uint32_t frame = 0U; frame < target->frame_count; ++frame)
    {
        float left = 0.0f;
        float right = 0.0f;
        wav_audio_codec_decode_stereo_frame(&src[frame * block_align],
                                            info->info.channels,
                                            info->info.bits_per_sample,
                                            &left,
                                            &right);
        target->frames_interleaved[(frame * SAMPLE_PAGE_FRAME_STRIDE_FLOATS)] = left;
        target->frames_interleaved[(frame * SAMPLE_PAGE_FRAME_STRIDE_FLOATS) + 1U] = right;
    }

    if (out_timing != 0)
    {
        out_timing->decode_end_us = sample_stream_manager_now_us();
    }
    g_sample_stream_manager_diag.bytes_read += (uint64_t)sector_bytes;
    return SAMPLE_STREAM_BACKEND_OK;
}

static uint8_t sample_stream_manager_try_contiguous_sector_backend(
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target)
{
    if ((info == 0) || (target == 0)
        || (info->source.safe_state != (uint8_t)SAMPLE_STREAM_SAFE_CONTIGUOUS))
    {
        return 0U;
    }

    uint8_t *const scratch = sample_stream_sector_scratch_buffer();
    const uint32_t scratch_size = sample_stream_sector_scratch_size_bytes();
    sample_stream_page_timing_t timing;
    (void)sample_page_cache_set_page_state_key(target->key,
                                               target->page_index,
                                               SAMPLE_PAGE_LOADING);
    const sample_stream_backend_result_t result =
        sample_stream_manager_load_contiguous_sector_page(info,
                                                          target,
                                                          scratch,
                                                          scratch_size,
                                                          &timing);
    g_sample_stream_manager_diag.last_backend_result = (uint32_t)result;
    g_sample_stream_manager_diag.last_backend_key = target->key;

    if (result == SAMPLE_STREAM_BACKEND_OK)
    {
        g_sample_stream_manager_diag.backend_sector_pages++;
        sample_stream_manager_diag_inc_domain(
            g_sample_stream_manager_diag.backend_sector_pages_by_domain,
            target->key);
        return 1U;
    }

    g_sample_stream_manager_diag.backend_fallback_pages++;
    sample_stream_manager_diag_inc_domain(
        g_sample_stream_manager_diag.backend_fallback_pages_by_domain,
        target->key);
    switch (result)
    {
        case SAMPLE_STREAM_BACKEND_READ_FAIL:
            g_sample_stream_manager_diag.backend_sector_read_fail++;
            break;

        case SAMPLE_STREAM_BACKEND_DECODE_FAIL:
            g_sample_stream_manager_diag.backend_sector_decode_fail++;
            break;

        case SAMPLE_STREAM_BACKEND_INVALID_META:
            g_sample_stream_manager_diag.backend_sector_invalid_meta++;
            break;

        case SAMPLE_STREAM_BACKEND_UNSUPPORTED_FORMAT:
            g_sample_stream_manager_diag.backend_sector_unsupported_format++;
            break;

        case SAMPLE_STREAM_BACKEND_SCRATCH_TOO_SMALL:
            g_sample_stream_manager_diag.backend_sector_scratch_too_small++;
            break;

        default:
            break;
    }
    return 0U;
}

static sample_page_load_result_t sample_stream_manager_decode_wav_page(
    FIL *fp,
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target,
    uint8_t *io_buffer,
    uint32_t io_buffer_size)
{
    if ((fp == 0) || (info == 0) || (target == 0) || (target->frames_interleaved == 0)
        || (io_buffer == 0) || (io_buffer_size == 0U) || (info->info.block_align == 0U))
    {
        return SAMPLE_PAGE_LOAD_INVALID_ARG;
    }

    uint32_t remaining_frames = target->frame_count;
    uint32_t write_frame = 0U;

    while (remaining_frames != 0U)
    {
        uint32_t request_bytes = remaining_frames * info->info.block_align;
        if (request_bytes > io_buffer_size)
        {
            request_bytes = io_buffer_size - (io_buffer_size % info->info.block_align);
        }
        if (request_bytes == 0U)
        {
            return SAMPLE_PAGE_LOAD_INVALID_ARG;
        }

        UINT br = 0U;
        g_sample_stream_service_fatfs_ops++;
        const FRESULT fr = f_read(fp, io_buffer, request_bytes, &br);
        if (fr != FR_OK)
        {
            return SAMPLE_PAGE_LOAD_READ_FAILED;
        }

        g_sample_stream_manager_diag.bytes_read += (uint64_t)br;
        const uint32_t valid_bytes = br - (br % info->info.block_align);
        if (valid_bytes == 0U)
        {
            return SAMPLE_PAGE_LOAD_READ_FAILED;
        }

        uint32_t pos = 0U;
        while ((pos + info->info.block_align <= valid_bytes) && (remaining_frames != 0U))
        {
            float left = 0.0f;
            float right = 0.0f;
            wav_audio_codec_decode_stereo_frame(&io_buffer[pos],
                                                info->info.channels,
                                                info->info.bits_per_sample,
                                                &left,
                                                &right);
            target->frames_interleaved[(write_frame * SAMPLE_PAGE_FRAME_STRIDE_FLOATS)] = left;
            target->frames_interleaved[(write_frame * SAMPLE_PAGE_FRAME_STRIDE_FLOATS) + 1U] = right;
            write_frame++;
            remaining_frames--;
            pos += info->info.block_align;
        }
    }

    return SAMPLE_PAGE_LOAD_OK;
}

static sample_page_load_result_t sample_stream_manager_decode_raw_pcm24_page(
    FIL *fp,
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target,
    uint8_t *io_buffer,
    uint32_t io_buffer_size)
{
    if ((fp == 0) || (info == 0) || (target == 0) || (target->frames_interleaved == 0)
        || (io_buffer == 0) || (info->info.block_align < 6U)
        || (io_buffer_size < info->info.block_align))
    {
        return SAMPLE_PAGE_LOAD_INVALID_ARG;
    }

    uint32_t remaining_frames = target->frame_count;
    uint32_t write_frame = 0U;

    while (remaining_frames != 0U)
    {
        uint32_t request_bytes = remaining_frames * info->info.block_align;
        if (request_bytes > io_buffer_size)
        {
            request_bytes = io_buffer_size - (io_buffer_size % info->info.block_align);
        }
        if (request_bytes == 0U)
        {
            return SAMPLE_PAGE_LOAD_INVALID_ARG;
        }

        UINT br = 0U;
        g_sample_stream_service_fatfs_ops++;
        const FRESULT fr = f_read(fp, io_buffer, request_bytes, &br);
        if (fr != FR_OK)
        {
            return SAMPLE_PAGE_LOAD_READ_FAILED;
        }

        g_sample_stream_manager_diag.bytes_read += (uint64_t)br;
        const uint32_t valid_bytes = br - (br % info->info.block_align);
        if (valid_bytes == 0U)
        {
            return SAMPLE_PAGE_LOAD_READ_FAILED;
        }

        uint32_t pos = 0U;
        while ((pos + info->info.block_align <= valid_bytes)
               && (remaining_frames != 0U))
        {
            target->frames_interleaved[(write_frame * SAMPLE_PAGE_FRAME_STRIDE_FLOATS)] =
                sample_stream_manager_decode_s24_le(&io_buffer[pos]);
            target->frames_interleaved[(write_frame * SAMPLE_PAGE_FRAME_STRIDE_FLOATS) + 1U] =
                sample_stream_manager_decode_s24_le(&io_buffer[pos + 3U]);
            write_frame++;
            remaining_frames--;
            pos += info->info.block_align;
        }
    }

    return SAMPLE_PAGE_LOAD_OK;
}

void sample_stream_manager_init(void)
{
    g_sample_stream_sector_scratch_anchor = sample_stream_sector_scratch_buffer();
    (void)sample_stream_sector_scratch_size_bytes();
    sample_stream_manager_reset();
}

void sample_stream_manager_reset(void)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_MAX_ACTIVE; ++i)
    {
        sample_stream_manager_clear_reader(&g_sample_stream_readers[i]);
    }
    memset(g_sample_stream_pending, 0, sizeof(g_sample_stream_pending));
    g_sample_stream_request_clock = 0U;
    g_sample_stream_next_sample_id = 0U;
    memset(&g_sample_stream_manager_diag, 0, sizeof(g_sample_stream_manager_diag));
    g_sample_stream_manager_diag.selected_sample_id = UINT32_MAX;
}

void sample_stream_manager_release_sample(uint16_t sample_id)
{
    sample_stream_manager_release_key(sample_audio_key_classic(sample_id));
}

void sample_stream_manager_release_key(sample_audio_key_t key)
{
    sample_stream_reader_t *const reader = sample_stream_manager_find_reader_key(key);
    if (reader != 0)
    {
        sample_stream_manager_clear_reader(reader);
    }
    sample_stream_manager_clear_pending_key_all(key);
}

uint8_t sample_stream_manager_request_page(uint16_t sample_id, uint32_t page_index)
{
    return sample_stream_manager_request_page_key(sample_audio_key_classic(sample_id), page_index);
}

static uint8_t sample_stream_manager_request_page_with_distance_key(
    sample_audio_key_t key,
    uint32_t page_index,
    uint16_t distance_pages)
{
    g_sample_stream_manager_diag.request_page_calls++;
    const sample_page_state_t state = sample_page_cache_get_page_state_key(key, page_index);
    if (state == SAMPLE_PAGE_QUEUED)
    {
        sample_stream_pending_t *const pending =
            sample_stream_manager_find_pending_key(key, page_index);
        if (sample_stream_manager_pending_would_improve(pending, distance_pages) == 0U)
        {
            if (distance_pages < BRICK6_STREAM_ACTIVE_WINDOW_PAGES)
            {
                (void)sample_page_cache_request_active_window_page_key(key, page_index);
            }
            return 1U;
        }
    }

    const uint8_t ok = (distance_pages < BRICK6_STREAM_ACTIVE_WINDOW_PAGES)
                           ? sample_page_cache_request_active_window_page_key(key, page_index)
                           : sample_page_cache_request_page_key(key, page_index);
    if (ok != 0U)
    {
        sample_stream_manager_note_requested_page_key(key,
                                                      page_index,
                                                      distance_pages);
    }
    return ok;
}

uint8_t sample_stream_manager_request_page_key(sample_audio_key_t key, uint32_t page_index)
{
    return sample_stream_manager_request_page_with_distance_key(key, page_index, UINT16_MAX);
}

uint8_t sample_stream_manager_request_presocle_page_key(sample_audio_key_t key, uint32_t page_index)
{
    g_sample_stream_manager_diag.request_page_calls++;
    const sample_page_state_t state = sample_page_cache_get_page_state_key(key, page_index);
    const uint8_t ok = sample_page_cache_request_presocle_page_key(key, page_index);
    if ((ok != 0U) && (state != SAMPLE_PAGE_READY))
    {
        sample_stream_manager_note_requested_page_key(key,
                                                      page_index,
                                                      0U);
    }
    return ok;
}

uint8_t sample_stream_manager_request_range(uint16_t sample_id,
                                            uint32_t start_frame,
                                            uint32_t page_count)
{
    return sample_stream_manager_request_range_key(sample_audio_key_classic(sample_id),
                                                   start_frame,
                                                   page_count);
}

uint8_t sample_stream_manager_request_range_key(sample_audio_key_t key,
                                                uint32_t start_frame,
                                                uint32_t page_count)
{
    g_sample_stream_manager_diag.request_range_calls++;
    uint8_t ok = 1U;
    const uint32_t first_page = start_frame / SAMPLE_PAGE_FRAMES;
    for (uint32_t i = 0U; i < page_count; ++i)
    {
        const uint32_t page_index = first_page + i;
        g_sample_stream_manager_diag.request_page_calls++;
        const uint8_t request_ok = sample_page_cache_request_page_key(key, page_index);
        if (request_ok != 0U)
        {
            sample_stream_manager_note_requested_page_key(
                key,
                page_index,
                (i > UINT16_MAX) ? UINT16_MAX : (uint16_t)i);
        }
        if (request_ok == 0U)
        {
            ok = 0U;
            break;
        }
    }
    return ok;
}

void sample_stream_manager_active_state_reset(sample_stream_active_state_t *state)
{
    if (state == 0)
    {
        return;
    }

    state->last_page = SAMPLE_STREAM_ACTIVE_PAGE_NONE;
}

static uint8_t sample_stream_manager_active_state_allows(
    const sample_stream_active_state_t *state,
    uint32_t page_index)
{
    if (state == 0)
    {
        return 1U;
    }

    return ((state->last_page == SAMPLE_STREAM_ACTIVE_PAGE_NONE)
            || (page_index > state->last_page))
               ? 1U
               : 0U;
}

static void sample_stream_manager_active_state_note(sample_stream_active_state_t *state,
                                                    uint32_t page_index)
{
    if (state == 0)
    {
        return;
    }

    if ((state->last_page == SAMPLE_STREAM_ACTIVE_PAGE_NONE)
        || (page_index > state->last_page))
    {
        state->last_page = page_index;
    }
}

uint8_t sample_stream_manager_queue_active_pages(const sample_stream_active_desc_t *desc)
{
    if ((desc == 0) || (desc->end_frame == 0U) || (desc->current_frame >= desc->end_frame))
    {
        return 0U;
    }

    const uint32_t current_page = desc->current_frame / SAMPLE_PAGE_FRAMES;
    const uint32_t last_page = (desc->end_frame - 1U) / SAMPLE_PAGE_FRAMES;
    const uint32_t first_ahead = (desc->request_current_page != 0U) ? 0U : 1U;
    uint8_t requested = 0U;

    for (uint32_t ahead = first_ahead; ahead <= (uint32_t)desc->lookahead_pages; ++ahead)
    {
        uint32_t page_index = UINT32_MAX;
        if (desc->direction < 0)
        {
            if (current_page < ahead)
            {
                break;
            }
            page_index = current_page - ahead;
        }
        else
        {
            page_index = current_page + ahead;
            if (page_index > last_page)
            {
                break;
            }
        }

        const sample_page_state_t state_before =
            sample_page_cache_get_page_state_key(desc->key, page_index);
        if (state_before == SAMPLE_PAGE_READY)
        {
            continue;
        }

        if (sample_stream_manager_active_state_allows(desc->state, page_index) == 0U)
        {
            continue;
        }

        const uint16_t distance_pages =
            (ahead > UINT16_MAX) ? UINT16_MAX : (uint16_t)ahead;
        const uint8_t ok =
            sample_stream_manager_request_page_with_distance_key(desc->key,
                                                                 page_index,
                                                                 distance_pages);
        if (ok != 0U)
        {
            sample_stream_manager_active_state_note(desc->state, page_index);
            requested = 1U;
        }
    }

    return requested;
}

void sample_stream_manager_service(uint32_t byte_budget)
{
    g_sample_stream_manager_diag.service_calls++;
    if (byte_budget == 0U)
    {
        return;
    }

    uint8_t io_buffer[4096U];
    const uint32_t start_tick = HAL_GetTick();
    uint32_t pages_this_call = 0U;
    g_sample_stream_service_fatfs_ops = 0U;

    while (byte_budget != 0U)
    {
        sample_page_load_target_t target;
        sample_page_stream_info_t stream_info;
        uint32_t pending_before = 0U;
        if (sample_stream_manager_pick_next(&target) == 0U)
        {
            g_sample_stream_manager_diag.service_no_loadable_work++;
            break;
        }
        g_sample_stream_manager_diag.selected_sample_id = target.key.object_id;
        pending_before = sample_stream_manager_pending_count();
        g_sample_stream_manager_diag.pending_count = pending_before;

        if (sample_page_cache_get_stream_info_key(target.key, &stream_info) == 0U)
        {
            (void)sample_page_cache_set_page_state_key(target.key,
                                                   target.page_index,
                                                   SAMPLE_PAGE_ERROR);
            sample_stream_manager_clear_pending_key(target.key, target.page_index);
            g_sample_stream_manager_diag.pending_dropped_non_stream++;
            g_sample_stream_manager_diag.pages_failed++;
            sample_stream_manager_note_multi_service(
                &target,
                SAMPLE_STREAM_MANAGER_SERVICE_BACKEND_NONE,
                SAMPLE_STREAM_MANAGER_SERVICE_RESULT_FAIL,
                SAMPLE_PAGE_ERROR,
                pending_before);
            sample_stream_manager_record_service_ticks(start_tick);
            return;
        }

        if (sample_stream_manager_try_contiguous_sector_backend(&stream_info, &target) != 0U)
        {
            (void)sample_page_cache_set_page_state_key(target.key,
                                                       target.page_index,
                                                       SAMPLE_PAGE_READY);
            sample_stream_manager_clear_pending_key(target.key, target.page_index);
            sample_stream_manager_note_multi_service(
                &target,
                SAMPLE_STREAM_MANAGER_SERVICE_BACKEND_SECTOR,
                SAMPLE_STREAM_MANAGER_SERVICE_RESULT_OK,
                SAMPLE_PAGE_READY,
                pending_before);
            g_sample_stream_manager_diag.pages_loaded++;
            pages_this_call++;
            sample_stream_manager_diag_max_u32(&g_sample_stream_manager_diag.pages_per_call_max,
                                               pages_this_call);

            const uint32_t consumed = target.frame_count * stream_info.info.block_align;
            if (consumed >= byte_budget)
            {
                g_sample_stream_manager_diag.service_budget_exhausted++;
                break;
            }
            byte_budget -= consumed;

            if (sample_stream_manager_service_should_yield(start_tick,
                                                           pages_this_call) != 0U)
            {
                g_sample_stream_manager_diag.service_time_yield++;
                break;
            }
            continue;
        }

        sample_stream_reader_t *const reader =
            sample_stream_manager_get_reader(target.key, &stream_info);
        FIL fallback_file;
        FIL *fp = 0;
        uint8_t fallback_open = 0U;

        if (reader != 0)
        {
            if (sample_stream_manager_open_reader(reader) == 0U)
            {
                (void)sample_page_cache_set_page_state_key(target.key,
                                                           target.page_index,
                                                           SAMPLE_PAGE_ERROR);
                sample_stream_manager_clear_pending_key(target.key, target.page_index);
                g_sample_stream_manager_diag.pages_failed++;
                sample_stream_manager_note_multi_service(
                    &target,
                    SAMPLE_STREAM_MANAGER_SERVICE_BACKEND_FATFS,
                    SAMPLE_STREAM_MANAGER_SERVICE_RESULT_FAIL,
                    SAMPLE_PAGE_ERROR,
                    pending_before);
                sample_stream_manager_record_service_ticks(start_tick);
                return;
            }
            fp = &reader->file;
        }
        else
        {
            g_sample_stream_service_fatfs_ops++;
            const FRESULT open_fr = f_open(&fallback_file, stream_info.path, FA_READ);
            if (open_fr != FR_OK)
            {
                (void)sample_page_cache_set_page_state_key(target.key,
                                                           target.page_index,
                                                           SAMPLE_PAGE_ERROR);
                sample_stream_manager_clear_pending_key(target.key, target.page_index);
                g_sample_stream_manager_diag.open_failures++;
                g_sample_stream_manager_diag.pages_failed++;
                sample_stream_manager_note_multi_service(
                    &target,
                    SAMPLE_STREAM_MANAGER_SERVICE_BACKEND_FATFS,
                    SAMPLE_STREAM_MANAGER_SERVICE_RESULT_FAIL,
                    SAMPLE_PAGE_ERROR,
                    pending_before);
                sample_stream_manager_record_service_ticks(start_tick);
                return;
            }
            fp = &fallback_file;
            fallback_open = 1U;
        }

        const FSIZE_t offset = (FSIZE_t)stream_info.data_offset
                              + ((FSIZE_t)target.start_frame
                                 * (FSIZE_t)stream_info.info.block_align);
        if ((reader != 0) && (reader->current_file_offset == offset))
        {
            g_sample_stream_manager_diag.sequential_reads++;
        }
        else
        {
            g_sample_stream_manager_diag.seek_reads++;
            g_sample_stream_service_fatfs_ops++;
            if (f_lseek(fp, offset) != FR_OK)
            {
                if (fallback_open != 0U)
                {
                    g_sample_stream_service_fatfs_ops++;
                    const FRESULT close_fr = f_close(fp);
                    if (close_fr != FR_OK)
                    {
                        g_sample_stream_manager_diag.close_failures++;
                    }
                }
                else if (reader != 0)
                {
                    sample_stream_manager_close_reader(reader);
                    reader->current_file_offset = 0U;
                    reader->last_page_index = UINT32_MAX;
                }
                (void)sample_page_cache_set_page_state_key(target.key,
                                                       target.page_index,
                                                       SAMPLE_PAGE_ERROR);
                sample_stream_manager_clear_pending_key(target.key, target.page_index);
                g_sample_stream_manager_diag.seek_failures++;
                g_sample_stream_manager_diag.pages_failed++;
                sample_stream_manager_note_multi_service(
                    &target,
                    SAMPLE_STREAM_MANAGER_SERVICE_BACKEND_FATFS,
                    SAMPLE_STREAM_MANAGER_SERVICE_RESULT_FAIL,
                    SAMPLE_PAGE_ERROR,
                    pending_before);
                sample_stream_manager_record_service_ticks(start_tick);
                return;
            }
        }

        (void)sample_page_cache_set_page_state_key(target.key,
                                               target.page_index,
                                               SAMPLE_PAGE_LOADING);
        const sample_page_load_result_t load_result =
            (stream_info.raw_pcm24 != 0U)
                ? sample_stream_manager_decode_raw_pcm24_page(fp,
                                                              &stream_info,
                                                              &target,
                                                              io_buffer,
                                                              sizeof(io_buffer))
                : sample_stream_manager_decode_wav_page(fp,
                                                        &stream_info,
                                                        &target,
                                                        io_buffer,
                                                        sizeof(io_buffer));

        if (fallback_open != 0U)
        {
            g_sample_stream_service_fatfs_ops++;
            const FRESULT close_fr = f_close(fp);
            if (close_fr != FR_OK)
            {
                g_sample_stream_manager_diag.close_failures++;
            }
        }

        if (load_result != SAMPLE_PAGE_LOAD_OK)
        {
            (void)sample_page_cache_set_page_state_key(target.key,
                                                   target.page_index,
                                                   SAMPLE_PAGE_ERROR);
            sample_stream_manager_clear_pending_key(target.key, target.page_index);
            if (load_result == SAMPLE_PAGE_LOAD_READ_FAILED)
            {
                g_sample_stream_manager_diag.read_failures++;
            }
            if (reader != 0)
            {
                sample_stream_manager_close_reader(reader);
                reader->current_file_offset = 0U;
                reader->last_page_index = UINT32_MAX;
            }
            g_sample_stream_manager_diag.pages_failed++;
            sample_stream_manager_note_multi_service(
                &target,
                SAMPLE_STREAM_MANAGER_SERVICE_BACKEND_FATFS,
                SAMPLE_STREAM_MANAGER_SERVICE_RESULT_FAIL,
                SAMPLE_PAGE_ERROR,
                pending_before);
            sample_stream_manager_record_service_ticks(start_tick);
            return;
        }

        (void)sample_page_cache_set_page_state_key(target.key,
                                               target.page_index,
                                               SAMPLE_PAGE_READY);
        sample_stream_manager_clear_pending_key(target.key, target.page_index);
        g_sample_stream_manager_diag.backend_fatfs_pages++;
        sample_stream_manager_diag_inc_domain(
            g_sample_stream_manager_diag.backend_fatfs_pages_by_domain,
            target.key);
        g_sample_stream_manager_diag.last_backend_result = (uint32_t)SAMPLE_STREAM_BACKEND_FATFS_OK;
        g_sample_stream_manager_diag.last_backend_key = target.key;
        sample_stream_manager_note_multi_service(
            &target,
            SAMPLE_STREAM_MANAGER_SERVICE_BACKEND_FATFS,
            SAMPLE_STREAM_MANAGER_SERVICE_RESULT_OK,
            SAMPLE_PAGE_READY,
            pending_before);
        g_sample_stream_manager_diag.pages_loaded++;
        if (reader != 0)
        {
            reader->current_file_offset =
                offset + ((FSIZE_t)target.frame_count * (FSIZE_t)stream_info.info.block_align);
            reader->last_page_index = target.page_index;
        }

        pages_this_call++;
        sample_stream_manager_diag_max_u32(&g_sample_stream_manager_diag.pages_per_call_max,
                                           pages_this_call);

        const uint32_t consumed = target.frame_count * stream_info.info.block_align;
        if (consumed >= byte_budget)
        {
            g_sample_stream_manager_diag.service_budget_exhausted++;
            break;
        }
        byte_budget -= consumed;

        if (sample_stream_manager_service_should_yield(start_tick,
                                                       pages_this_call) != 0U)
        {
            g_sample_stream_manager_diag.service_time_yield++;
            break;
        }
    }

    sample_stream_manager_record_service_ticks(start_tick);
}

uint8_t sample_stream_manager_has_pending_sd_work(void)
{
    g_sample_stream_manager_diag.has_pending_calls++;
    uint8_t pending_seen = 0U;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        sample_stream_pending_t *const pending = &g_sample_stream_pending[i];
        if (pending->active == 0U)
        {
            continue;
        }

        pending_seen = 1U;
        sample_page_load_target_t target;
        if (sample_page_cache_get_load_target_key(pending->key,
                                                  pending->page_index,
                                                  &target) != 0U)
        {
            return 1U;
        }

        g_sample_stream_manager_diag.pending_not_loadable++;
        sample_stream_manager_drop_pending_slot(pending);
    }

    if (pending_seen != 0U)
    {
        g_sample_stream_manager_diag.has_pending_stale_cleaned++;
    }

    return 0U;
}
