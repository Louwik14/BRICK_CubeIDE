#include "Sampler/sample_stream_manager.h"

#include <string.h>

#include "Sampler/sample_page_cache.h"
#include "Storage/memory_layout.h"
#include "Storage/wav_audio_codec.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#define SAMPLE_STREAM_MAX_ACTIVE (16U)
#define SAMPLE_STREAM_PENDING_MAX SAMPLE_PAGE_MAX_COUNT
#define SAMPLE_STREAM_SERVICE_MAX_PAGES (2U)
#define SAMPLE_STREAM_SERVICE_MAX_FATFS_OPS (8U)
#define SAMPLE_STREAM_SERVICE_MAX_TICKS (1U)

typedef enum
{
    SAMPLE_STREAM_PRIORITY_PREFETCH = 0,
    SAMPLE_STREAM_PRIORITY_NORMAL,
    SAMPLE_STREAM_PRIORITY_URGENT
} sample_stream_priority_t;

typedef struct
{
    uint8_t in_use;
    uint8_t file_open;
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
    uint8_t priority;
    uint16_t sample_id;
    uint32_t page_index;
    uint32_t requested_at;
} sample_stream_pending_t;

static CTRL_STATE sample_stream_manager_diag_snapshot_t g_sample_stream_manager_diag;
static sample_stream_reader_t g_sample_stream_readers[SAMPLE_STREAM_MAX_ACTIVE];
static sample_stream_pending_t g_sample_stream_pending[SAMPLE_STREAM_PENDING_MAX];
static uint32_t g_sample_stream_request_clock;
static uint32_t g_sample_stream_service_fatfs_ops;
static uint16_t g_sample_stream_next_sample_id;

static void sample_stream_manager_diag_max_u32(uint32_t *field, uint32_t value)
{
    if ((field != 0) && (value > *field))
    {
        *field = value;
    }
}

static void sample_stream_manager_record_service_ticks(uint32_t start_tick)
{
    const uint32_t elapsed_ticks = HAL_GetTick() - start_tick;
    sample_stream_manager_diag_max_u32(&g_sample_stream_manager_diag.service_time_max_ticks,
                                       elapsed_ticks);
    sample_stream_manager_diag_max_u32(&g_sample_stream_manager_diag.gate_hold_time_max_ticks,
                                       elapsed_ticks);
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
    reader->last_page_index = UINT32_MAX;
}

static uint8_t sample_stream_manager_reader_matches(const sample_stream_reader_t *reader,
                                                    uint16_t sample_id,
                                                    const sample_page_stream_info_t *info)
{
    if ((reader == 0) || (info == 0) || (reader->in_use == 0U)
        || (reader->sample_id != sample_id))
    {
        return 0U;
    }

    return ((reader->data_offset == info->data_offset)
            && (reader->total_frames == info->total_frames)
            && (reader->bytes_per_frame == info->info.block_align)
            && (strncmp(reader->path, info->path, sizeof(reader->path)) == 0)) ? 1U : 0U;
}

static sample_stream_reader_t *sample_stream_manager_find_reader(uint16_t sample_id)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_MAX_ACTIVE; ++i)
    {
        if ((g_sample_stream_readers[i].in_use != 0U)
            && (g_sample_stream_readers[i].sample_id == sample_id))
        {
            return &g_sample_stream_readers[i];
        }
    }

    return 0;
}

static sample_stream_reader_t *sample_stream_manager_get_reader(
    uint16_t sample_id,
    const sample_page_stream_info_t *info)
{
    if ((sample_id >= SAMPLE_POOL_SIZE) || (info == 0) || (info->info.block_align == 0U))
    {
        return 0;
    }

    sample_stream_reader_t *reader = sample_stream_manager_find_reader(sample_id);
    if (reader != 0)
    {
        if (sample_stream_manager_reader_matches(reader, sample_id, info) == 0U)
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
            reader->sample_id = sample_id;
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

static void sample_stream_manager_clear_pending(uint16_t sample_id, uint32_t page_index)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        sample_stream_pending_t *const pending = &g_sample_stream_pending[i];
        if ((pending->active != 0U) && (pending->sample_id == sample_id)
            && (pending->page_index == page_index))
        {
            memset(pending, 0, sizeof(*pending));
            return;
        }
    }
}

static void sample_stream_manager_clear_pending_sample(uint16_t sample_id)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        sample_stream_pending_t *const pending = &g_sample_stream_pending[i];
        if ((pending->active != 0U) && (pending->sample_id == sample_id))
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

static sample_stream_priority_t sample_stream_manager_classify_request(uint16_t sample_id,
                                                                       uint32_t page_index)
{
    const sample_stream_reader_t *const reader = sample_stream_manager_find_reader(sample_id);
    if ((reader == 0) || (reader->in_use == 0U) || (reader->bytes_per_frame == 0U)
        || (reader->current_file_offset < (FSIZE_t)reader->data_offset))
    {
        return SAMPLE_STREAM_PRIORITY_NORMAL;
    }

    const uint32_t frames_from_start =
        (uint32_t)((reader->current_file_offset - (FSIZE_t)reader->data_offset)
                   / (FSIZE_t)reader->bytes_per_frame);
    const uint32_t current_page = frames_from_start / SAMPLE_PAGE_FRAMES;
    const uint32_t distance = (page_index > current_page) ? (page_index - current_page)
                                                          : (current_page - page_index);

    if (distance <= 1U)
    {
        return SAMPLE_STREAM_PRIORITY_URGENT;
    }
    if (distance <= 4U)
    {
        return SAMPLE_STREAM_PRIORITY_NORMAL;
    }
    return SAMPLE_STREAM_PRIORITY_PREFETCH;
}

static void sample_stream_manager_note_pending(uint16_t sample_id,
                                               uint32_t page_index,
                                               sample_stream_priority_t priority)
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

        if ((pending->sample_id == sample_id) && (pending->page_index == page_index))
        {
            if ((uint8_t)priority > pending->priority)
            {
                pending->priority = (uint8_t)priority;
            }
            return;
        }
    }

    if (free_slot != 0)
    {
        free_slot->active = 1U;
        free_slot->priority = (uint8_t)priority;
        free_slot->sample_id = sample_id;
        free_slot->page_index = page_index;
        free_slot->requested_at = ++g_sample_stream_request_clock;
    }
}

static void sample_stream_manager_note_requested_page(uint16_t sample_id,
                                                      uint32_t page_index,
                                                      sample_stream_priority_t priority)
{
    const sample_page_state_t state = sample_page_cache_get_page_state(sample_id, page_index);
    if (state == SAMPLE_PAGE_QUEUED)
    {
        g_sample_stream_manager_diag.pages_requested++;
        sample_stream_manager_note_pending(sample_id, page_index, priority);
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

static void sample_stream_manager_update_max_pending_age(void)
{
    uint32_t max_age = 0U;

    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        const sample_stream_pending_t *const pending = &g_sample_stream_pending[i];
        if (pending->active == 0U)
        {
            continue;
        }

        const uint32_t age = g_sample_stream_request_clock - pending->requested_at;
        if (age > max_age)
        {
            max_age = age;
        }
    }

    if (max_age > g_sample_stream_manager_diag.max_pending_age)
    {
        g_sample_stream_manager_diag.max_pending_age = max_age;
    }
}

static uint16_t sample_stream_manager_fair_distance(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return UINT16_MAX;
    }

    return (sample_id >= g_sample_stream_next_sample_id)
               ? (uint16_t)(sample_id - g_sample_stream_next_sample_id)
               : (uint16_t)(SAMPLE_POOL_SIZE - g_sample_stream_next_sample_id + sample_id);
}

static uint8_t sample_stream_manager_candidate_is_better(uint8_t have_best,
                                                         sample_stream_priority_t priority,
                                                         uint16_t fair_distance,
                                                         uint32_t age,
                                                         sample_stream_priority_t best_priority,
                                                         uint16_t best_fair_distance,
                                                         uint32_t best_age)
{
    if (have_best == 0U)
    {
        return 1U;
    }
    if (priority > best_priority)
    {
        return 1U;
    }
    if (priority < best_priority)
    {
        return 0U;
    }
    if (fair_distance < best_fair_distance)
    {
        return 1U;
    }
    if (fair_distance > best_fair_distance)
    {
        return 0U;
    }
    return (age > best_age) ? 1U : 0U;
}

static uint8_t sample_stream_manager_pick_fallback(sample_page_load_target_t *out_target)
{
    if (sample_page_cache_find_queued_load_target(0U, SAMPLE_POOL_SIZE, out_target) == 0U)
    {
        return 0U;
    }

    sample_stream_manager_note_pending(out_target->sample_id,
                                       out_target->page_index,
                                       SAMPLE_STREAM_PRIORITY_NORMAL);
    return 1U;
}

static uint8_t sample_stream_manager_pick_next(sample_page_load_target_t *out_target,
                                               sample_stream_priority_t *out_priority)
{
    sample_stream_manager_update_max_pending_age();

    uint8_t found = 0U;
    uint32_t scan_count = 0U;
    uint32_t best_age = 0U;
    uint16_t best_fair_distance = UINT16_MAX;
    sample_stream_priority_t best_priority = SAMPLE_STREAM_PRIORITY_PREFETCH;
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

        if (pending->sample_id >= SAMPLE_POOL_SIZE)
        {
            g_sample_stream_manager_diag.pending_invalid_sample++;
            sample_stream_manager_drop_pending_slot(pending);
            continue;
        }

        sample_page_load_target_t target;
        if (sample_page_cache_get_load_target(pending->sample_id,
                                              pending->page_index,
                                              &target) == 0U)
        {
            g_sample_stream_manager_diag.pending_not_loadable++;
            sample_stream_manager_drop_pending_slot(pending);
            continue;
        }

        const sample_stream_priority_t priority =
            (sample_stream_priority_t)pending->priority;
        if (priority > SAMPLE_STREAM_PRIORITY_URGENT)
        {
            g_sample_stream_manager_diag.pending_not_loadable++;
            sample_stream_manager_drop_pending_slot(pending);
            continue;
        }
        const uint16_t fair_distance =
            sample_stream_manager_fair_distance(pending->sample_id);
        const uint32_t age = g_sample_stream_request_clock - pending->requested_at;

        if (sample_stream_manager_candidate_is_better(found,
                                                      priority,
                                                      fair_distance,
                                                      age,
                                                      best_priority,
                                                      best_fair_distance,
                                                      best_age) != 0U)
        {
            best_target = target;
            best_priority = priority;
            best_fair_distance = fair_distance;
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
        if (out_priority != 0)
        {
            *out_priority = best_priority;
        }
        g_sample_stream_next_sample_id = (uint16_t)((best_target.sample_id + 1U) % SAMPLE_POOL_SIZE);
        return 1U;
    }

    if (pending_seen != 0U)
    {
        g_sample_stream_manager_diag.pick_no_work++;
        return 0U;
    }

    if (sample_stream_manager_pick_fallback(out_target) != 0U)
    {
        if (out_priority != 0)
        {
            *out_priority = SAMPLE_STREAM_PRIORITY_NORMAL;
        }
        g_sample_stream_next_sample_id = (uint16_t)((out_target->sample_id + 1U) % SAMPLE_POOL_SIZE);
        return 1U;
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
    sample_stream_reader_t *const reader = sample_stream_manager_find_reader(sample_id);
    if (reader != 0)
    {
        sample_stream_manager_clear_reader(reader);
    }
    sample_stream_manager_clear_pending_sample(sample_id);
}

uint8_t sample_stream_manager_request_page(uint16_t sample_id, uint32_t page_index)
{
    g_sample_stream_manager_diag.request_page_calls++;
    const uint8_t ok = sample_page_cache_request_page(sample_id, page_index);
    if (ok != 0U)
    {
        sample_stream_manager_note_requested_page(
            sample_id,
            page_index,
            sample_stream_manager_classify_request(sample_id, page_index));
    }
    return ok;
}

uint8_t sample_stream_manager_request_range(uint16_t sample_id,
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
        const uint8_t request_ok = sample_page_cache_request_page(sample_id, page_index);
        if (request_ok != 0U)
        {
            sample_stream_manager_note_requested_page(
                sample_id,
                page_index,
                (i == 0U) ? SAMPLE_STREAM_PRIORITY_URGENT : SAMPLE_STREAM_PRIORITY_NORMAL);
        }
        if (request_ok == 0U)
        {
            ok = 0U;
            break;
        }
    }
    return ok;
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
        sample_stream_priority_t selected_priority = SAMPLE_STREAM_PRIORITY_NORMAL;
        sample_page_stream_info_t stream_info;
        if (sample_stream_manager_pick_next(&target, &selected_priority) == 0U)
        {
            g_sample_stream_manager_diag.service_no_loadable_work++;
            break;
        }
        g_sample_stream_manager_diag.selected_sample_id = target.sample_id;

        if (sample_page_cache_get_stream_info(target.sample_id, &stream_info) == 0U)
        {
            (void)sample_page_cache_set_page_state(target.sample_id,
                                                   target.page_index,
                                                   SAMPLE_PAGE_ERROR);
            sample_stream_manager_clear_pending(target.sample_id, target.page_index);
            g_sample_stream_manager_diag.pending_dropped_non_stream++;
            g_sample_stream_manager_diag.pages_failed++;
            sample_stream_manager_record_service_ticks(start_tick);
            return;
        }

        sample_stream_reader_t *const reader =
            sample_stream_manager_get_reader(target.sample_id, &stream_info);
        FIL fallback_file;
        FIL *fp = 0;
        uint8_t fallback_open = 0U;

        if (reader != 0)
        {
            if (sample_stream_manager_open_reader(reader) == 0U)
            {
                (void)sample_page_cache_set_page_state(target.sample_id,
                                                       target.page_index,
                                                       SAMPLE_PAGE_ERROR);
                sample_stream_manager_clear_pending(target.sample_id, target.page_index);
                g_sample_stream_manager_diag.pages_failed++;
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
                (void)sample_page_cache_set_page_state(target.sample_id,
                                                       target.page_index,
                                                       SAMPLE_PAGE_ERROR);
                g_sample_stream_manager_diag.open_failures++;
                g_sample_stream_manager_diag.pages_failed++;
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
                (void)sample_page_cache_set_page_state(target.sample_id,
                                                       target.page_index,
                                                       SAMPLE_PAGE_ERROR);
                sample_stream_manager_clear_pending(target.sample_id, target.page_index);
                g_sample_stream_manager_diag.seek_failures++;
                g_sample_stream_manager_diag.pages_failed++;
                sample_stream_manager_record_service_ticks(start_tick);
                return;
            }
        }

        (void)sample_page_cache_set_page_state(target.sample_id,
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
            (void)sample_page_cache_set_page_state(target.sample_id,
                                                   target.page_index,
                                                   SAMPLE_PAGE_ERROR);
            sample_stream_manager_clear_pending(target.sample_id, target.page_index);
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
            sample_stream_manager_record_service_ticks(start_tick);
            return;
        }

        (void)sample_page_cache_set_page_state(target.sample_id,
                                               target.page_index,
                                               SAMPLE_PAGE_READY);
        sample_stream_manager_clear_pending(target.sample_id, target.page_index);
        g_sample_stream_manager_diag.pages_loaded++;
        if (selected_priority == SAMPLE_STREAM_PRIORITY_URGENT)
        {
            g_sample_stream_manager_diag.pages_served_urgent++;
        }
        else if (selected_priority == SAMPLE_STREAM_PRIORITY_NORMAL)
        {
            g_sample_stream_manager_diag.pages_served_normal++;
        }
        else
        {
            g_sample_stream_manager_diag.pages_served_prefetch++;
        }
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

        const uint32_t elapsed_ticks = HAL_GetTick() - start_tick;
        if ((pages_this_call >= SAMPLE_STREAM_SERVICE_MAX_PAGES)
            || (g_sample_stream_service_fatfs_ops >= SAMPLE_STREAM_SERVICE_MAX_FATFS_OPS)
            || (elapsed_ticks >= SAMPLE_STREAM_SERVICE_MAX_TICKS))
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
    if (sample_page_cache_has_queued_range(0U, SAMPLE_POOL_SIZE) == 0U)
    {
        return 0U;
    }

    sample_page_load_target_t target;
    if (sample_page_cache_find_queued_load_target(0U, SAMPLE_POOL_SIZE, &target) == 0U)
    {
        g_sample_stream_manager_diag.has_pending_stale_cleaned++;
        return 0U;
    }

    return 1U;
}

void sample_stream_manager_diag_get_snapshot(sample_stream_manager_diag_snapshot_t *out_snapshot)
{
    if (out_snapshot == 0)
    {
        return;
    }

    *out_snapshot = g_sample_stream_manager_diag;
}
