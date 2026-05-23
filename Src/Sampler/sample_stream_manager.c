#include "Sampler/sample_stream_manager.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_backend_contiguous.h"
#include "Storage/sd_access_gate.h"
#include "Storage/memory_layout.h"
#include "Storage/wav_audio_codec.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#define SAMPLE_STREAM_PENDING_MAX SAMPLE_PAGE_MAX_COUNT
#define SAMPLE_STREAM_SERVICE_MAX_PAGES (16U)
#define SAMPLE_STREAM_SERVICE_MAX_FATFS_OPS (16U)
#define SAMPLE_STREAM_SERVICE_MAX_TICKS (2U)
#define SAMPLE_STREAM_READER_FILE_OPEN_COOKIE (0x5354524DU)
#define SAMPLE_STREAM_DYNAMIC_PENDING_MAX \
    (SAMPLE_STREAM_MAX_ACTIVE * SAMPLE_PAGE_MULTI_WINDOW_PAGES * 2U)
#define SAMPLE_STREAM_PENDING_REASON_COMPLETE (1U)
#define SAMPLE_STREAM_PENDING_REASON_CANCEL (2U)
#define SAMPLE_STREAM_PENDING_REASON_RELEASE_KEY (3U)
#define SAMPLE_STREAM_PENDING_REASON_RELEASE_OWNER (4U)
#define SAMPLE_STREAM_PENDING_REASON_ORPHAN (6U)
#define SAMPLE_STREAM_PENDING_REASON_NO_SLOT (7U)
#define SAMPLE_STREAM_PENDING_REASON_BUDGET (8U)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_CACHE_HOT_SAMPLE_CAPACITY <= SAMPLE_PAGE_CACHE_ID_CAPACITY,
               "stream manager hot scan range must fit in page-cache ids");
_Static_assert(SAMPLE_STREAM_MAX_ACTIVE <= SAMPLE_CACHE_HOT_SAMPLE_CAPACITY,
               "active stream readers must be bounded below hot sample capacity");
#endif

typedef enum
{
    SAMPLE_STREAM_PRIORITY_PREFETCH = 0,
    SAMPLE_STREAM_PRIORITY_NORMAL,
    SAMPLE_STREAM_PRIORITY_URGENT
} sample_stream_priority_t;

typedef struct
{
    sample_audio_key_t key;
    uint32_t data_offset;
    uint32_t total_frames;
    uint32_t bytes_per_frame;
    FSIZE_t current_file_offset;
    uint32_t last_page_index;
    uint32_t file_open_cookie;
    uint16_t sample_id;
    uint8_t in_use;
    uint8_t file_open;
    FIL file;
} sample_stream_reader_t;

typedef struct
{
    sample_audio_key_t key;
    uint32_t page_index;
    uint32_t requested_at;
    uint32_t deadline_frames;
    uint32_t owner_generation;
    uint16_t sample_id;
    uint16_t reserved;
    uint8_t active;
    uint8_t priority;
    uint8_t owner_kind;
    uint8_t owner_id;
} sample_stream_pending_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert((offsetof(sample_stream_reader_t, key) % 4U) == 0U,
               "stream reader key must be 32-bit aligned");
_Static_assert((offsetof(sample_stream_pending_t, key) % 4U) == 0U,
               "stream pending key must be 32-bit aligned");
_Static_assert((offsetof(sample_stream_pending_t, page_index) % 4U) == 0U,
               "stream pending page_index must be 32-bit aligned");
_Static_assert((offsetof(sample_stream_pending_t, requested_at) % 4U) == 0U,
               "stream pending requested_at must be 32-bit aligned");
_Static_assert((offsetof(sample_stream_pending_t, deadline_frames) % 4U) == 0U,
               "stream pending deadline_frames must be 32-bit aligned");
_Static_assert((offsetof(sample_stream_pending_t, owner_generation) % 4U) == 0U,
               "stream pending owner_generation must be 32-bit aligned");
_Static_assert((sizeof(sample_stream_pending_t) % 4U) == 0U,
               "stream pending size must preserve array element alignment");
_Static_assert(_Alignof(sample_stream_pending_t) >= 4U,
               "stream pending array elements must be 32-bit aligned");
#endif

SDRAM_STREAM_SERVICE static sample_stream_reader_t g_sample_stream_readers[SAMPLE_STREAM_MAX_ACTIVE];
SDRAM_STREAM_SERVICE static char g_sample_stream_reader_paths[SAMPLE_STREAM_MAX_ACTIVE][SAMPLE_PAGE_CACHE_PATH_MAX];
SDRAM_STREAM_SERVICE static sample_stream_pending_t g_sample_stream_pending[SAMPLE_STREAM_PENDING_MAX];
SDRAM_STREAM_SCRATCH static uint8_t g_sample_stream_fatfs_io_buffer[4096U];
static uint32_t g_sample_stream_request_clock;
static uint32_t g_sample_stream_service_fatfs_ops;
static uint16_t g_sample_stream_next_sample_id;
static uint8_t g_sample_stream_next_owner_id;
static sample_stream_pending_t *g_sample_stream_selected_pending;
static uint8_t g_sample_stream_manager_initialized;


static char *sample_stream_manager_reader_path(sample_stream_reader_t *reader)
{
    if (reader == 0)
    {
        return 0;
    }

    const uint32_t index = (uint32_t)(reader - g_sample_stream_readers);
    if (index >= SAMPLE_STREAM_MAX_ACTIVE)
    {
        return 0;
    }

    return g_sample_stream_reader_paths[index];
}

static const char *sample_stream_manager_reader_path_const(const sample_stream_reader_t *reader)
{
    if (reader == 0)
    {
        return 0;
    }

    const uint32_t index = (uint32_t)(reader - g_sample_stream_readers);
    if (index >= SAMPLE_STREAM_MAX_ACTIVE)
    {
        return 0;
    }

    return g_sample_stream_reader_paths[index];
}

static uint32_t sample_stream_manager_page_deadline_frames(const sample_stream_active_desc_t *desc,
                                                          uint32_t page_index)
{
    if (desc == 0)
    {
        return UINT32_MAX;
    }

    if (desc->direction < 0)
    {
        const uint32_t page_end = (page_index + 1U) * SAMPLE_PAGE_FRAMES;
        return (desc->current_frame >= page_end) ? (desc->current_frame - page_end) : 0U;
    }

    const uint32_t page_start = page_index * SAMPLE_PAGE_FRAMES;
    return (page_start > desc->current_frame) ? (page_start - desc->current_frame) : 0U;
}

static void sample_stream_manager_close_reader(sample_stream_reader_t *reader)
{
    if (reader == 0)
    {
        return;
    }

    if ((reader->file_open != 0U)
        && (reader->file_open_cookie == SAMPLE_STREAM_READER_FILE_OPEN_COOKIE))
    {
        g_sample_stream_service_fatfs_ops++;
        const FRESULT close_fr = f_close(&reader->file);
        if (close_fr != FR_OK)
        {
        }
    }
    else if ((reader->file_open != 0U) || (reader->file_open_cookie != 0U))
    {
    }

    reader->file_open = 0U;
    reader->file_open_cookie = 0U;
    memset(&reader->file, 0, sizeof(reader->file));
}

static void sample_stream_manager_clear_reader(sample_stream_reader_t *reader)
{
    if (reader == 0)
    {
        return;
    }

    sample_stream_manager_close_reader(reader);
    char *const path = sample_stream_manager_reader_path(reader);
    if (path != 0)
    {
        path[0] = '\0';
    }
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

    const char *const path = sample_stream_manager_reader_path_const(reader);
    if (path == 0)
    {
        return 0U;
    }

    return ((reader->data_offset == info->data_offset)
            && (reader->total_frames == info->total_frames)
            && (reader->bytes_per_frame == info->info.block_align)
            && (strncmp(path, info->path, SAMPLE_PAGE_CACHE_PATH_MAX) == 0)) ? 1U : 0U;
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
            char *const path = sample_stream_manager_reader_path(reader);
            if (path == 0)
            {
                return 0;
            }
            memcpy(path, info->path, SAMPLE_PAGE_CACHE_PATH_MAX);
            reader->in_use = 1U;
            reader->key = key;
            reader->sample_id = key.object_id;
            reader->data_offset = info->data_offset;
            reader->total_frames = info->total_frames;
            reader->bytes_per_frame = info->info.block_align;
            reader->last_page_index = UINT32_MAX;
            reader->current_file_offset = 0U;
            return reader;
        }
    }
    return 0;
}

static uint8_t sample_stream_manager_open_reader(sample_stream_reader_t *reader)
{
    if (reader == 0)
    {
        return 0U;
    }

    if ((reader->file_open != 0U)
        && (reader->file_open_cookie == SAMPLE_STREAM_READER_FILE_OPEN_COOKIE))
    {
        return 1U;
    }

    reader->file_open = 0U;
    reader->file_open_cookie = 0U;
    memset(&reader->file, 0, sizeof(reader->file));

    const char *const path = sample_stream_manager_reader_path_const(reader);
    if (path == 0)
    {
        return 0U;
    }

    g_sample_stream_service_fatfs_ops++;
    const FRESULT open_fr = f_open(&reader->file, path, FA_READ);
    if (open_fr != FR_OK)
    {
        return 0U;
    }

    reader->file_open = 1U;
    reader->file_open_cookie = SAMPLE_STREAM_READER_FILE_OPEN_COOKIE;
    reader->current_file_offset = 0U;
    reader->last_page_index = UINT32_MAX;
    return 1U;
}

static void sample_stream_manager_clear_pending_key(sample_audio_key_t key,
                                                    uint32_t page_index,
                                                    uint8_t reason)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        sample_stream_pending_t *const pending = &g_sample_stream_pending[i];
        if ((pending->active != 0U)
            && (sample_audio_key_equal(&pending->key, &key) != 0U)
            && (pending->page_index == page_index))
        {
            if (reason != SAMPLE_STREAM_PENDING_REASON_COMPLETE)
            {
                (void)sample_page_cache_cancel_queued_page_key(key, page_index, reason);
            }
            memset(pending, 0, sizeof(*pending));
            return;
        }
    }
}

static void sample_stream_manager_clear_pending_key_all(sample_audio_key_t key, uint8_t reason)
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
    (void)sample_page_cache_cancel_queued_key(key, reason);
}

static void sample_stream_manager_drop_pending_slot(sample_stream_pending_t *pending, uint8_t reason)
{
    if ((pending == 0) || (pending->active == 0U))
    {
        return;
    }

    const sample_audio_key_t key = pending->key;
    const uint32_t page_index = pending->page_index;
    (void)sample_page_cache_cancel_queued_page_key(key, page_index, reason);
    memset(pending, 0, sizeof(*pending));
}

static uint8_t sample_stream_manager_pending_budget_allows(
    sample_audio_key_t key,
    uint32_t page_index,
    const sample_stream_active_desc_t *owner)
{
    uint32_t active_owner_pending = 0U;
    uint32_t active_dynamic_pending = 0U;

    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        const sample_stream_pending_t *const pending = &g_sample_stream_pending[i];
        if (pending->active == 0U)
        {
            continue;
        }
        if ((sample_audio_key_equal(&pending->key, &key) != 0U)
            && (pending->page_index == page_index))
        {
            return 1U;
        }
        if (pending->owner_kind != (uint8_t)SAMPLE_STREAM_OWNER_NONE)
        {
            active_dynamic_pending++;
        }
        if ((owner != 0)
            && (pending->owner_kind == owner->owner_kind)
            && (pending->owner_id == owner->owner_id)
            && (pending->owner_generation == owner->owner_generation))
        {
            active_owner_pending++;
        }
    }

    if (owner == 0)
    {
        return 1U;
    }
    if (active_owner_pending >= SAMPLE_PAGE_MULTI_WINDOW_PAGES)
    {
        (void)key;
        (void)page_index;
        return 0U;
    }
    if (active_dynamic_pending >= SAMPLE_STREAM_DYNAMIC_PENDING_MAX)
    {
        (void)key;
        (void)page_index;
        return 0U;
    }
    return 1U;
}

static uint8_t sample_stream_manager_note_pending_key(sample_audio_key_t key,
                                                      uint32_t page_index,
                                                      sample_stream_priority_t priority,
                                                      uint32_t deadline_frames,
                                                      const sample_stream_active_desc_t *owner)
{
    sample_stream_pending_t *free_slot = 0;

    if (sample_stream_manager_pending_budget_allows(key, page_index, owner) == 0U)
    {
        return 0U;
    }

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
            const uint32_t old_deadline = pending->deadline_frames;
            if ((uint8_t)priority > pending->priority)
            {
                pending->priority = (uint8_t)priority;
            }
            if (deadline_frames < pending->deadline_frames)
            {
                pending->deadline_frames = deadline_frames;
            }
            if ((owner != 0)
                && ((pending->owner_kind == (uint8_t)SAMPLE_STREAM_OWNER_NONE)
                    || (deadline_frames <= old_deadline)))
            {
                pending->owner_kind = owner->owner_kind;
                pending->owner_id = owner->owner_id;
                pending->owner_generation = owner->owner_generation;
            }
            return 1U;
        }
    }

    if (free_slot != 0)
    {
        free_slot->active = 1U;
        free_slot->priority = (uint8_t)priority;
        free_slot->key = key;
        free_slot->sample_id = key.object_id;
        free_slot->page_index = page_index;
        free_slot->requested_at = ++g_sample_stream_request_clock;
        free_slot->deadline_frames = deadline_frames;
        if (owner != 0)
        {
            free_slot->owner_kind = owner->owner_kind;
            free_slot->owner_id = owner->owner_id;
            free_slot->owner_generation = owner->owner_generation;
        }
        return 1U;
    }
    return 0U;
}

static uint8_t sample_stream_manager_note_requested_page_key(sample_audio_key_t key,
                                                             uint32_t page_index,
                                                             sample_stream_priority_t priority,
                                                             uint32_t deadline_frames,
                                                             const sample_stream_active_desc_t *owner)
{
    const sample_page_state_t state = sample_page_cache_get_page_state_key(key, page_index);
    if (state == SAMPLE_PAGE_QUEUED)
    {
        return sample_stream_manager_note_pending_key(key, page_index, priority, deadline_frames, owner);
    }
    return 1U;
}

static uint16_t sample_stream_manager_fair_distance_pending(const sample_stream_pending_t *pending)
{
    if (pending == 0)
    {
        return UINT16_MAX;
    }

    if (pending->owner_kind != (uint8_t)SAMPLE_STREAM_OWNER_NONE)
    {
        return (pending->owner_id >= g_sample_stream_next_owner_id)
                   ? (uint16_t)(pending->owner_id - g_sample_stream_next_owner_id)
                   : (uint16_t)(SAMPLE_STREAM_MAX_ACTIVE
                                - g_sample_stream_next_owner_id
                                + pending->owner_id);
    }

    if (pending->key.domain != SAMPLE_AUDIO_DOMAIN_CLASSIC)
    {
        return UINT16_MAX;
    }

    return (pending->key.object_id >= g_sample_stream_next_sample_id)
               ? (uint16_t)(pending->key.object_id - g_sample_stream_next_sample_id)
               : (uint16_t)(SAMPLE_CACHE_HOT_SAMPLE_CAPACITY
                            - g_sample_stream_next_sample_id
                            + pending->key.object_id);
}

static uint8_t sample_stream_manager_candidate_is_better(uint8_t have_best,
                                                         sample_stream_priority_t priority,
                                                         uint32_t deadline_frames,
                                                         uint16_t fair_distance,
                                                         uint32_t age,
                                                         sample_stream_priority_t best_priority,
                                                         uint32_t best_deadline_frames,
                                                         uint16_t best_fair_distance,
                                                         uint32_t best_age)
{
    if (have_best == 0U)
    {
        return 1U;
    }
    if (deadline_frames < best_deadline_frames)
    {
        return 1U;
    }
    if (deadline_frames > best_deadline_frames)
    {
        return 0U;
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

static uint8_t sample_stream_manager_pick_next(sample_page_load_target_t *out_target,
                                               sample_stream_priority_t *out_priority)
{
    g_sample_stream_selected_pending = 0;

    uint8_t found = 0U;
    uint32_t scan_count = 0U;
    uint32_t best_age = 0U;
    uint32_t best_deadline_frames = UINT32_MAX;
    uint16_t best_fair_distance = UINT16_MAX;
    sample_stream_priority_t best_priority = SAMPLE_STREAM_PRIORITY_PREFETCH;
    sample_page_load_target_t best_target;
    uint8_t best_owner_kind = (uint8_t)SAMPLE_STREAM_OWNER_NONE;
    uint8_t best_owner_id = 0U;
    uint8_t pending_seen = 0U;
    sample_stream_pending_t *best_pending = 0;

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
            sample_stream_manager_drop_pending_slot(pending, SAMPLE_STREAM_PENDING_REASON_CANCEL);
            continue;
        }

        const sample_stream_priority_t priority =
            (sample_stream_priority_t)pending->priority;
        if (priority > SAMPLE_STREAM_PRIORITY_URGENT)
        {
            sample_stream_manager_drop_pending_slot(pending, SAMPLE_STREAM_PENDING_REASON_CANCEL);
            continue;
        }
        const uint16_t fair_distance =
            sample_stream_manager_fair_distance_pending(pending);
        const uint32_t age = g_sample_stream_request_clock - pending->requested_at;

        if (sample_stream_manager_candidate_is_better(found,
                                                      priority,
                                                      pending->deadline_frames,
                                                      fair_distance,
                                                      age,
                                                      best_priority,
                                                      best_deadline_frames,
                                                      best_fair_distance,
                                                      best_age) != 0U)
        {
            best_target = target;
            best_priority = priority;
            best_deadline_frames = pending->deadline_frames;
            best_fair_distance = fair_distance;
            best_age = age;
            best_owner_kind = pending->owner_kind;
            best_owner_id = pending->owner_id;
            best_pending = pending;
            found = 1U;
        }
    }

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
        if (best_owner_kind != (uint8_t)SAMPLE_STREAM_OWNER_NONE)
        {
            g_sample_stream_next_owner_id =
                (uint8_t)((best_owner_id + 1U) % SAMPLE_STREAM_MAX_ACTIVE);
        }
        else
        {
            g_sample_stream_next_sample_id =
                (uint16_t)((best_target.key.object_id + 1U) % SAMPLE_CACHE_HOT_SAMPLE_CAPACITY);
        }
        g_sample_stream_selected_pending = best_pending;
        return 1U;
    }

    (void)pending_seen;
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

static void sample_stream_manager_init_storage_once(void)
{
    if (g_sample_stream_manager_initialized != 0U)
    {
        return;
    }

    memset(g_sample_stream_readers, 0, sizeof(g_sample_stream_readers));
    memset(g_sample_stream_reader_paths, 0, sizeof(g_sample_stream_reader_paths));
    memset(g_sample_stream_pending, 0, sizeof(g_sample_stream_pending));
    g_sample_stream_manager_initialized = 1U;
}

void sample_stream_manager_init(void)
{
    sample_stream_manager_init_storage_once();
    sample_stream_manager_reset();
}

void sample_stream_manager_reset(void)
{
    sample_stream_manager_init_storage_once();
    for (uint32_t i = 0U; i < SAMPLE_STREAM_MAX_ACTIVE; ++i)
    {
        sample_stream_manager_clear_reader(&g_sample_stream_readers[i]);
    }
    memset(g_sample_stream_pending, 0, sizeof(g_sample_stream_pending));
    g_sample_stream_request_clock = 0U;
    g_sample_stream_next_sample_id = 0U;
    g_sample_stream_next_owner_id = 0U;
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
    sample_stream_manager_clear_pending_key_all(key, SAMPLE_STREAM_PENDING_REASON_RELEASE_KEY);
    sd_access_gate_set_streaming_critical(sample_page_cache_has_window_locks());
}

void sample_stream_manager_release_owner(uint8_t owner_kind,
                                         uint8_t owner_id,
                                         uint32_t owner_generation)
{
    if (owner_kind == (uint8_t)SAMPLE_STREAM_OWNER_NONE)
    {
        return;
    }
    sample_page_cache_release_window_owner(owner_kind, owner_id, owner_generation);
    sd_access_gate_set_streaming_critical(sample_page_cache_has_window_locks());
    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        sample_stream_pending_t *const pending = &g_sample_stream_pending[i];
        if ((pending->active != 0U)
            && (pending->owner_kind == owner_kind)
            && (pending->owner_id == owner_id)
            && (pending->owner_generation == owner_generation))
        {
            (void)sample_page_cache_cancel_queued_page_key(
                pending->key,
                pending->page_index,
                SAMPLE_STREAM_PENDING_REASON_RELEASE_OWNER);
            memset(pending, 0, sizeof(*pending));
        }
    }
}

uint8_t sample_stream_manager_request_page(uint16_t sample_id, uint32_t page_index)
{
    return sample_stream_manager_request_page_key(sample_audio_key_classic(sample_id), page_index);
}

static uint8_t sample_stream_manager_request_page_with_priority_key(
    sample_audio_key_t key,
    uint32_t page_index,
    sample_stream_priority_t priority,
    uint32_t deadline_frames,
    const sample_stream_active_desc_t *owner,
    sample_page_alloc_type_t alloc_type)
{
    const uint8_t ok = sample_page_cache_request_page_key_alloc(key, page_index, alloc_type);
    if (ok != 0U)
    {
        if (sample_stream_manager_note_requested_page_key(key, page_index, priority, deadline_frames, owner) == 0U)
        {
            (void)sample_page_cache_cancel_queued_page_key(
                key,
                page_index,
                SAMPLE_STREAM_PENDING_REASON_NO_SLOT);
            return 0U;
        }
    }
    return ok;
}

uint8_t sample_stream_manager_request_page_key(sample_audio_key_t key, uint32_t page_index)
{
    return sample_stream_manager_request_page_key_alloc(key,
                                                        page_index,
                                                        SAMPLE_PAGE_ALLOC_LEGACY_DEFAULT);
}

uint8_t sample_stream_manager_request_page_key_alloc(sample_audio_key_t key,
                                                     uint32_t page_index,
                                                     sample_page_alloc_type_t alloc_type)
{
    return sample_stream_manager_request_page_with_priority_key(
        key,
        page_index,
        SAMPLE_STREAM_PRIORITY_NORMAL,
        UINT32_MAX,
        0,
        alloc_type);
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
    return sample_stream_manager_request_range_key_alloc(
        key,
        start_frame,
        page_count,
        SAMPLE_PAGE_ALLOC_LEGACY_DEFAULT);
}

uint8_t sample_stream_manager_request_range_key_alloc(sample_audio_key_t key,
                                                      uint32_t start_frame,
                                                      uint32_t page_count,
                                                      sample_page_alloc_type_t alloc_type)
{
    uint8_t ok = 1U;
    const uint32_t first_page = start_frame / SAMPLE_PAGE_FRAMES;
    for (uint32_t i = 0U; i < page_count; ++i)
    {
        const uint32_t page_index = first_page + i;
        const uint8_t request_ok = sample_page_cache_request_page_key_alloc(
            key,
            page_index,
            alloc_type);
        if (request_ok != 0U)
        {
            if (sample_stream_manager_note_requested_page_key(
                key,
                page_index,
                (i == 0U) ? SAMPLE_STREAM_PRIORITY_URGENT : SAMPLE_STREAM_PRIORITY_NORMAL,
                UINT32_MAX,
                0) == 0U)
            {
                (void)sample_page_cache_cancel_queued_page_key(
                    key,
                    page_index,
                    SAMPLE_STREAM_PENDING_REASON_NO_SLOT);
                ok = 0U;
                break;
            }
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

    state->last_urgent_page = SAMPLE_STREAM_ACTIVE_PAGE_NONE;
    state->last_normal_page = SAMPLE_STREAM_ACTIVE_PAGE_NONE;
}

static uint8_t sample_stream_manager_active_state_allows(
    const sample_stream_active_state_t *state,
    uint32_t page_index,
    sample_stream_priority_t priority,
    int8_t direction)
{
    if (state == 0)
    {
        return 1U;
    }

    if (priority == SAMPLE_STREAM_PRIORITY_URGENT)
    {
        return ((state->last_urgent_page == SAMPLE_STREAM_ACTIVE_PAGE_NONE)
                || ((direction < 0) ? (page_index < state->last_urgent_page)
                                    : (page_index > state->last_urgent_page)))
                   ? 1U
                   : 0U;
    }

    return ((state->last_normal_page == SAMPLE_STREAM_ACTIVE_PAGE_NONE)
            || ((direction < 0) ? (page_index < state->last_normal_page)
                                : (page_index > state->last_normal_page)))
               ? 1U
               : 0U;
}

static void sample_stream_manager_active_state_note(sample_stream_active_state_t *state,
                                                    uint32_t page_index,
                                                    sample_stream_priority_t priority,
                                                    int8_t direction)
{
    if (state == 0)
    {
        return;
    }

    if (priority == SAMPLE_STREAM_PRIORITY_URGENT)
    {
        if ((state->last_urgent_page == SAMPLE_STREAM_ACTIVE_PAGE_NONE)
            || ((direction < 0) ? (page_index < state->last_urgent_page)
                                : (page_index > state->last_urgent_page)))
        {
            state->last_urgent_page = page_index;
        }
        return;
    }

    if ((state->last_normal_page == SAMPLE_STREAM_ACTIVE_PAGE_NONE)
        || ((direction < 0) ? (page_index < state->last_normal_page)
                            : (page_index > state->last_normal_page)))
    {
        state->last_normal_page = page_index;
    }
}

uint8_t sample_stream_manager_queue_active_pages(const sample_stream_active_desc_t *desc)
{
    if ((desc == 0) || (desc->end_frame == 0U) || (desc->current_frame >= desc->end_frame))
    {
        return 0U;
    }
    if (desc->owner_kind == (uint8_t)SAMPLE_STREAM_OWNER_NONE)
    {
        return 0U;
    }

    const uint32_t current_page = desc->current_frame / SAMPLE_PAGE_FRAMES;
    const uint32_t last_page = (desc->end_frame - 1U) / SAMPLE_PAGE_FRAMES;
    const uint32_t first_ahead = (desc->request_current_page != 0U) ? 0U : 1U;
    uint8_t requested = 0U;
    uint8_t high_priority_assigned = 0U;
    uint32_t window_first = current_page;
    uint32_t window_last = current_page;

    if (desc->direction < 0)
    {
        window_first = (current_page > (uint32_t)desc->lookahead_pages)
                           ? (current_page - (uint32_t)desc->lookahead_pages)
                           : 0U;
        window_last = (current_page <= last_page) ? current_page : last_page;
    }
    else
    {
        window_first = (first_ahead == 0U) ? current_page : (current_page + 1U);
        if (window_first > last_page)
        {
            window_first = last_page;
        }
        window_last = current_page + (uint32_t)desc->lookahead_pages;
        if (window_last > last_page)
        {
            window_last = last_page;
        }
    }

    sample_page_cache_release_window_owner_outside_key(desc->owner_kind,
                                                       desc->owner_id,
                                                       desc->owner_generation,
                                                       desc->key,
                                                       window_first,
                                                       window_last);

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
        if (state_before == SAMPLE_PAGE_ERROR)
        {
            continue;
        }
        const uint32_t deadline_frames =
            sample_stream_manager_page_deadline_frames(desc, page_index);

        sample_stream_priority_t priority = SAMPLE_STREAM_PRIORITY_PREFETCH;
        if (state_before != SAMPLE_PAGE_READY)
        {
            if (high_priority_assigned == 0U)
            {
                priority = SAMPLE_STREAM_PRIORITY_URGENT;
            }
            else if (high_priority_assigned == 1U)
            {
                priority = SAMPLE_STREAM_PRIORITY_NORMAL;
            }
            if (high_priority_assigned < UINT8_MAX)
            {
                high_priority_assigned++;
            }
        }

        if (sample_page_cache_acquire_window_page_key(desc->key,
                                                      page_index,
                                                      desc->owner_kind,
                                                      desc->owner_id,
                                                      desc->owner_generation) == 0U)
        {
            continue;
        }
        sd_access_gate_set_streaming_critical(1U);

        if (state_before == SAMPLE_PAGE_READY)
        {
            continue;
        }

        if (sample_stream_manager_active_state_allows(
                desc->state, page_index, priority, desc->direction) == 0U)
        {
            continue;
        }

        const uint8_t ok =
            sample_stream_manager_request_page_with_priority_key(desc->key,
                                                                 page_index,
                                                                 priority,
                                                                 deadline_frames,
                                                                 desc,
                                                                 SAMPLE_PAGE_ALLOC_VOICE_WINDOW);
        if (ok != 0U)
        {
            sample_stream_manager_active_state_note(
                desc->state, page_index, priority, desc->direction);
            requested = 1U;
        }
    }

    return requested;
}

uint8_t sample_stream_manager_reserve_active_pages(const sample_stream_active_desc_t *desc)
{
    if ((desc == 0) || (desc->end_frame == 0U) || (desc->current_frame >= desc->end_frame))
    {
        return 0U;
    }
    if (desc->owner_kind == (uint8_t)SAMPLE_STREAM_OWNER_NONE)
    {
        return 0U;
    }

    const uint32_t current_page = desc->current_frame / SAMPLE_PAGE_FRAMES;
    const uint32_t last_page = (desc->end_frame - 1U) / SAMPLE_PAGE_FRAMES;
    const uint32_t first_ahead = (desc->request_current_page != 0U) ? 0U : 1U;
    uint8_t high_priority_assigned = 0U;

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
        if (state_before == SAMPLE_PAGE_ERROR)
        {
            sample_stream_manager_release_owner(desc->owner_kind,
                                                desc->owner_id,
                                                desc->owner_generation);
            return 0U;
        }
        const uint32_t deadline_frames =
            sample_stream_manager_page_deadline_frames(desc, page_index);

        sample_stream_priority_t priority = SAMPLE_STREAM_PRIORITY_PREFETCH;
        if (state_before != SAMPLE_PAGE_READY)
        {
            if (high_priority_assigned == 0U)
            {
                priority = SAMPLE_STREAM_PRIORITY_URGENT;
            }
            else if (high_priority_assigned == 1U)
            {
                priority = SAMPLE_STREAM_PRIORITY_NORMAL;
            }
            if (high_priority_assigned < UINT8_MAX)
            {
                high_priority_assigned++;
            }
        }

        if ((state_before != SAMPLE_PAGE_READY) && (state_before != SAMPLE_PAGE_QUEUED)
            && (state_before != SAMPLE_PAGE_LOADING))
        {
            if (sample_stream_manager_request_page_with_priority_key(desc->key,
                                                                     page_index,
                                                                     priority,
                                                                     deadline_frames,
                                                                     desc,
                                                                     SAMPLE_PAGE_ALLOC_VOICE_WINDOW) == 0U)
            {
                sample_stream_manager_release_owner(desc->owner_kind,
                                                    desc->owner_id,
                                                    desc->owner_generation);
                return 0U;
            }
        }
        else if (state_before != SAMPLE_PAGE_READY)
        {
            if (sample_stream_manager_note_requested_page_key(desc->key,
                                                              page_index,
                                                              priority,
                                                              deadline_frames,
                                                              desc) == 0U)
            {
                sample_stream_manager_release_owner(desc->owner_kind,
                                                    desc->owner_id,
                                                    desc->owner_generation);
                return 0U;
            }
        }

        if (sample_page_cache_acquire_window_page_key(desc->key,
                                                      page_index,
                                                      desc->owner_kind,
                                                      desc->owner_id,
                                                      desc->owner_generation) == 0U)
        {
            sample_stream_manager_release_owner(desc->owner_kind,
                                                desc->owner_id,
                                                desc->owner_generation);
            return 0U;
        }
        sd_access_gate_set_streaming_critical(1U);
    }

    return 1U;
}

void sample_stream_manager_service(uint32_t byte_budget)
{
    if (byte_budget == 0U)
    {
        return;
    }

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
            break;
        }

        if (sample_page_cache_get_stream_info_key(target.key, &stream_info) == 0U)
        {
            (void)sample_page_cache_set_page_state_key(target.key,
                                                   target.page_index,
                                                   SAMPLE_PAGE_ERROR);
            sample_stream_manager_clear_pending_key(target.key,
                                                    target.page_index,
                                                    SAMPLE_STREAM_PENDING_REASON_CANCEL);
            return;
        }

        sample_stream_reader_t *reader = 0;
        FIL fallback_file;
        FIL *fp = 0;
        uint8_t fallback_open = 0U;
        uint8_t used_contiguous_backend = 0U;

        const FSIZE_t offset = (FSIZE_t)stream_info.data_offset
                              + ((FSIZE_t)target.start_frame
                                 * (FSIZE_t)stream_info.info.block_align);

        uint32_t delivered_pages = 1U;
        uint32_t consumed = target.frame_count * stream_info.info.block_align;
        sample_page_load_result_t load_result = SAMPLE_PAGE_LOAD_OK;
        (void)sample_page_cache_set_page_state_key(target.key,
                                                   target.page_index,
                                                   SAMPLE_PAGE_LOADING);
        if ((stream_info.raw_pcm24 == 0U)
            && (stream_info.stream_safe.backend_kind
                == (uint8_t)SAMPLE_STREAM_BACKEND_SAFE_CONTIGUOUS)
            && (stream_info.stream_safe.valid != 0U))
        {
            load_result = sample_stream_backend_contiguous_load_page(&stream_info, &target);
            if (load_result == SAMPLE_PAGE_LOAD_OK)
            {
                used_contiguous_backend = 1U;
            }
            else
            {
            }
        }

        if (used_contiguous_backend == 0U)
        {
            reader = sample_stream_manager_get_reader(target.key, &stream_info);
            if (reader != 0)
            {
                if (sample_stream_manager_open_reader(reader) == 0U)
                {
                    (void)sample_page_cache_set_page_state_key(target.key,
                                                               target.page_index,
                                                               SAMPLE_PAGE_ERROR);
                    sample_stream_manager_clear_pending_key(target.key,
                                                            target.page_index,
                                                            SAMPLE_STREAM_PENDING_REASON_CANCEL);
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
                    sample_stream_manager_clear_pending_key(target.key,
                                                            target.page_index,
                                                            SAMPLE_STREAM_PENDING_REASON_CANCEL);
                    return;
                }
                fp = &fallback_file;
                fallback_open = 1U;
            }

            if ((reader != 0) && (reader->current_file_offset == offset))
            {
            }
            else
            {
                g_sample_stream_service_fatfs_ops++;
                if (f_lseek(fp, offset) != FR_OK)
                {
                    if (fallback_open != 0U)
                    {
                        g_sample_stream_service_fatfs_ops++;
                        const FRESULT close_fr = f_close(fp);
                        if (close_fr != FR_OK)
                        {
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
                    sample_stream_manager_clear_pending_key(target.key,
                                                            target.page_index,
                                                            SAMPLE_STREAM_PENDING_REASON_CANCEL);
                    return;
                }
            }

            load_result =
                (stream_info.raw_pcm24 != 0U)
                    ? sample_stream_manager_decode_raw_pcm24_page(fp,
                                                                  &stream_info,
                                                                  &target,
                                                                  g_sample_stream_fatfs_io_buffer,
                                                                  sizeof(g_sample_stream_fatfs_io_buffer))
                    : sample_stream_manager_decode_wav_page(fp,
                                                            &stream_info,
                                                            &target,
                                                            g_sample_stream_fatfs_io_buffer,
                                                            sizeof(g_sample_stream_fatfs_io_buffer));
        }

        if (fallback_open != 0U)
        {
            g_sample_stream_service_fatfs_ops++;
            const FRESULT close_fr = f_close(fp);
            if (close_fr != FR_OK)
            {
            }
        }

        if (load_result != SAMPLE_PAGE_LOAD_OK)
        {
            (void)sample_page_cache_set_page_state_key(target.key,
                                                   target.page_index,
                                                   SAMPLE_PAGE_ERROR);
            sample_stream_manager_clear_pending_key(target.key,
                                                    target.page_index,
                                                    SAMPLE_STREAM_PENDING_REASON_CANCEL);
            if (load_result == SAMPLE_PAGE_LOAD_READ_FAILED)
            {
            }
            if (reader != 0)
            {
                sample_stream_manager_close_reader(reader);
                reader->current_file_offset = 0U;
                reader->last_page_index = UINT32_MAX;
            }
            return;
        }

        (void)sample_page_cache_set_page_state_key(target.key,
                                                   target.page_index,
                                                   SAMPLE_PAGE_READY);
        sample_stream_manager_clear_pending_key(target.key,
                                                target.page_index,
                                                SAMPLE_STREAM_PENDING_REASON_COMPLETE);
        for (uint32_t i = 0U; i < delivered_pages; ++i)
        {
        }
        if (selected_priority == SAMPLE_STREAM_PRIORITY_URGENT)
        {
        }
        else if (selected_priority == SAMPLE_STREAM_PRIORITY_NORMAL)
        {
        }
        else
        {
        }
        if ((reader != 0) && (used_contiguous_backend == 0U))
        {
            reader->current_file_offset =
                offset + ((FSIZE_t)target.frame_count * (FSIZE_t)stream_info.info.block_align);
            reader->last_page_index = target.page_index;
        }

        pages_this_call += delivered_pages;

        if (consumed >= byte_budget)
        {
            break;
        }
        byte_budget -= consumed;

        const uint32_t elapsed_ticks = HAL_GetTick() - start_tick;
        if (pages_this_call >= SAMPLE_STREAM_SERVICE_MAX_PAGES)
        {
            break;
        }
        if (g_sample_stream_service_fatfs_ops >= SAMPLE_STREAM_SERVICE_MAX_FATFS_OPS)
        {
            break;
        }
        if (elapsed_ticks >= SAMPLE_STREAM_SERVICE_MAX_TICKS)
        {
            break;
        }
    }
}

uint8_t sample_stream_manager_has_pending_sd_work(void)
{
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
        sample_stream_manager_drop_pending_slot(pending, SAMPLE_STREAM_PENDING_REASON_ORPHAN);
    }

    (void)pending_seen;
    (void)sample_page_cache_cancel_queued_domain(SAMPLE_AUDIO_DOMAIN_MULTI,
                                                 SAMPLE_STREAM_PENDING_REASON_ORPHAN);

    return 0U;
}
