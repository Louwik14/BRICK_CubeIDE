#include "Sampler/sample_stream_manager.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_multi_stream_diag.h"
#include "Sampler/sample_stream_backend_contiguous.h"
#include "Sampler/sample_stream_contract.h"
#include "Storage/sd_access_gate.h"
#include "Storage/memory_layout.h"
#include "Storage/wav_audio_codec.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#define SAMPLE_STREAM_PENDING_MAX SAMPLE_PAGE_MAX_COUNT
#define SAMPLE_STREAM_SERVICE_MAX_PAGES (16U)
#define SAMPLE_STREAM_SERVICE_MAX_FATFS_OPS (16U)
#define SAMPLE_STREAM_SERVICE_MAX_TICKS (2U)
#define SAMPLE_STREAM_SERVICE_MIN_URGENT_PAGES (2U)
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
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
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
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint32_t page_index;
    uint32_t requested_at;
    sample_stream_audio_frame_t created_audio_frame;
    sample_stream_audio_frame_t consume_deadline_audio_frame;
    uint32_t owner_generation;
    uint16_t sample_id;
    uint16_t reserved;
    uint8_t active;
    uint8_t priority;
    uint8_t owner_kind;
    uint8_t owner_id;
    uint8_t role;
#if BRICK6_STREAM_TRACE
    uint8_t trace_slot;
    uint8_t trace_valid;
#endif
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
_Static_assert((offsetof(sample_stream_pending_t, created_audio_frame) % 8U) == 0U,
               "stream pending creation time must be 64-bit aligned");
_Static_assert((offsetof(sample_stream_pending_t, consume_deadline_audio_frame) % 8U) == 0U,
               "stream pending deadline must be 64-bit aligned");
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
#if BRICK6_STREAM_TRACE
static uint32_t g_sample_stream_service_physical_reads;
volatile sample_stream_trace_snapshot_t g_sample_stream_trace;
static uint32_t g_sample_stream_last_service_cycle;
static sample_audio_key_t g_sample_stream_last_selected_key;
static uint8_t g_sample_stream_last_selected_key_valid;

static uint32_t sample_stream_trace_cycle(void)
{
    return DWT->CYCCNT;
}

static uint16_t sample_stream_trace_pending_count(void)
{
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        count += (g_sample_stream_pending[i].active != 0U) ? 1U : 0U;
    }
    return (count > UINT16_MAX) ? UINT16_MAX : (uint16_t)count;
}

static uint32_t sample_stream_trace_frames_until(sample_stream_audio_frame_t now,
                                                 sample_stream_audio_frame_t deadline)
{
    if (deadline <= now)
    {
        return 0U;
    }
    const uint64_t remaining = deadline - now;
    return (remaining > UINT32_MAX) ? UINT32_MAX : (uint32_t)remaining;
}

static sample_stream_trace_op_t *sample_stream_trace_begin_pending(
    sample_stream_pending_t *pending,
    const sample_stream_active_desc_t *owner)
{
    if ((pending == 0) || (g_sample_stream_trace.frozen != 0U))
    {
        return 0;
    }
    const uint32_t slot = g_sample_stream_trace.write_index % SAMPLE_STREAM_TRACE_CAPACITY;
    sample_stream_trace_op_t *const op =
        (sample_stream_trace_op_t *)&g_sample_stream_trace.operations[slot];
    memset(op, 0, sizeof(*op));
    op->key = pending->key;
    op->page_index = pending->page_index;
    op->reader_position = (owner != 0) ? owner->current_frame : 0U;
    op->created_audio_frame = pending->created_audio_frame;
    op->consume_deadline_audio_frame = pending->consume_deadline_audio_frame;
    op->frames_remaining = sample_stream_trace_frames_until(
        pending->created_audio_frame, pending->consume_deadline_audio_frame);
    op->deadline_frames = op->frames_remaining;
    op->request_cycle = sample_stream_trace_cycle();
    const uint64_t deadline_cycles =
        ((uint64_t)op->deadline_frames * (uint64_t)SystemCoreClock) / 48000ULL;
    op->deadline_cycle = op->request_cycle + (uint32_t)deadline_cycles;
    op->role = pending->role;
    op->priority = pending->priority;
    op->owner_kind = pending->owner_kind;
    op->owner_id = pending->owner_id;
    op->owner_generation = pending->owner_generation;
    pending->trace_slot = (uint8_t)slot;
    pending->trace_valid = 1U;
    g_sample_stream_trace.write_index++;
    if (g_sample_stream_trace.count < SAMPLE_STREAM_TRACE_CAPACITY)
    {
        g_sample_stream_trace.count++;
    }
    return op;
}

static sample_stream_trace_op_t *sample_stream_trace_pending_op(
    const sample_stream_pending_t *pending)
{
    if ((pending == 0) || (pending->trace_valid == 0U)
        || (pending->trace_slot >= SAMPLE_STREAM_TRACE_CAPACITY))
    {
        return 0;
    }
    return (sample_stream_trace_op_t *)&g_sample_stream_trace.operations[pending->trace_slot];
}

static void sample_stream_trace_trigger(sample_stream_trace_trigger_t trigger,
                                        sample_audio_key_t key,
                                        uint32_t page_index,
                                        uint32_t reader_position,
                                        uint32_t frames_remaining)
{
    if (g_sample_stream_trace.frozen != 0U)
    {
        return;
    }
    g_sample_stream_trace.trigger = (uint32_t)trigger;
    g_sample_stream_trace.trigger_key = key;
    g_sample_stream_trace.trigger_page = page_index;
    g_sample_stream_trace.trigger_reader_position = reader_position;
    g_sample_stream_trace.trigger_frames_remaining = frames_remaining;
    g_sample_stream_trace.trigger_audio_frame = sample_stream_time_now();
    g_sample_stream_trace.trigger_cycle = sample_stream_trace_cycle();
    g_sample_stream_trace.frozen = 1U;
}
#endif

#if defined(BRICK6_MULTI_STREAM_DIAG)
SDRAM_STREAM_SERVICE volatile sample_multi_stream_diag_snapshot_t g_sample_multi_stream_diag;
volatile uint32_t g_sample_multi_stream_diag_frozen;
static volatile uint32_t g_sample_multi_stream_diag_breakpoint_seen;
#endif


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

    uint32_t source_distance = 0U;
    const sample_audio_format_t format = sample_audio_format_or_stereo(desc->format);
    const uint32_t frames_per_page = sample_audio_format_frames_per_page(format);
    if (desc->direction < 0)
    {
        const uint64_t page_end = ((uint64_t)page_index + 1ULL) * frames_per_page;
        if ((uint64_t)desc->current_frame >= page_end)
        {
            source_distance = (uint32_t)((uint64_t)desc->current_frame - page_end);
        }
    }
    else
    {
        const uint64_t page_start = (uint64_t)page_index * frames_per_page;
        if (page_start > (uint64_t)desc->current_frame)
        {
            const uint64_t distance = page_start - (uint64_t)desc->current_frame;
            source_distance = (distance > UINT32_MAX) ? UINT32_MAX : (uint32_t)distance;
        }
    }

    if (source_distance == 0U)
    {
        return 0U;
    }
    if (desc->step_q16 == 0U)
    {
        return UINT32_MAX;
    }

    return sample_stream_time_source_to_output_frames(source_distance, desc->step_q16);
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
            && (reader->format == info->format)
            && (reader->stride_floats == info->stride_floats)
            && (reader->frames_per_page == info->frames_per_page)
            && (reader->registration_epoch == info->registration_epoch)
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
            reader->format = info->format;
            reader->stride_floats = info->stride_floats;
            reader->frames_per_page = info->frames_per_page;
            reader->registration_epoch = info->registration_epoch;
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
    if (sample_page_cache_get_page_state_key(key, page_index) == SAMPLE_PAGE_QUEUED)
    {
        pending->owner_kind = (uint8_t)SAMPLE_STREAM_OWNER_NONE;
        pending->owner_id = 0U;
        pending->owner_generation = 0U;
        return;
    }
    memset(pending, 0, sizeof(*pending));
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
    const uint32_t owner_window_pages = (owner != 0)
                                            ? sample_audio_format_window_pages(
                                                  sample_audio_format_or_stereo(owner->format))
                                            : SAMPLE_PAGE_MULTI_WINDOW_PAGES;
    if (active_owner_pending >= owner_window_pages)
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
    sample_page_load_target_t target;
    const uint8_t target_valid = sample_page_cache_get_load_target_key(key, page_index, &target);
    const sample_stream_audio_frame_t now_audio_frame = sample_stream_time_now();
    const sample_stream_audio_frame_t candidate_deadline =
        sample_stream_time_deadline_after(now_audio_frame, deadline_frames);

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
            if ((target_valid != 0U)
                && ((pending->format != target.format)
                    || (pending->stride_floats != target.stride_floats)
                    || (pending->frames_per_page != target.frames_per_page)
                    || ((pending->registration_epoch != 0U)
                        && (pending->registration_epoch != target.registration_epoch))))
            {
                sample_stream_manager_drop_pending_slot(pending,
                                                        SAMPLE_STREAM_PENDING_REASON_ORPHAN);
                if (free_slot == 0)
                {
                    free_slot = pending;
                }
                continue;
            }
            const sample_stream_audio_frame_t old_deadline =
                pending->consume_deadline_audio_frame;
            if ((uint8_t)priority > pending->priority)
            {
                pending->priority = (uint8_t)priority;
            }
            if (candidate_deadline < pending->consume_deadline_audio_frame)
            {
                pending->consume_deadline_audio_frame = candidate_deadline;
            }
            if ((owner != 0)
                && ((pending->owner_kind == (uint8_t)SAMPLE_STREAM_OWNER_NONE)
                    || (candidate_deadline <= old_deadline)))
            {
                pending->owner_kind = owner->owner_kind;
                pending->owner_id = owner->owner_id;
                pending->owner_generation = owner->owner_generation;
            }
#if BRICK6_STREAM_TRACE
            if (owner != 0)
            {
                pending->role = owner->role;
            }
            sample_stream_trace_op_t *const op = sample_stream_trace_pending_op(pending);
            if (op != 0)
            {
                op->consume_deadline_audio_frame = pending->consume_deadline_audio_frame;
                op->deadline_frames = sample_stream_trace_frames_until(
                    op->created_audio_frame, pending->consume_deadline_audio_frame);
                op->frames_remaining = sample_stream_trace_frames_until(
                    now_audio_frame, pending->consume_deadline_audio_frame);
                const uint64_t deadline_cycles =
                    ((uint64_t)op->frames_remaining * (uint64_t)SystemCoreClock) / 48000ULL;
                op->deadline_cycle = sample_stream_trace_cycle() + (uint32_t)deadline_cycles;
                if (owner != 0)
                {
                    op->reader_position = owner->current_frame;
                }
                op->priority = pending->priority;
                op->role = pending->role;
            }
#endif
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
        if (target_valid != 0U)
        {
            free_slot->format = target.format;
            free_slot->stride_floats = target.stride_floats;
            free_slot->frames_per_page = target.frames_per_page;
            free_slot->registration_epoch = target.registration_epoch;
        }
        free_slot->requested_at = ++g_sample_stream_request_clock;
        free_slot->created_audio_frame = now_audio_frame;
        free_slot->consume_deadline_audio_frame = candidate_deadline;
        if (owner != 0)
        {
            free_slot->owner_kind = owner->owner_kind;
            free_slot->owner_id = owner->owner_id;
            free_slot->owner_generation = owner->owner_generation;
            free_slot->role = owner->role;
        }
#if BRICK6_STREAM_TRACE
        (void)sample_stream_trace_begin_pending(free_slot, owner);
#endif
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

static uint8_t sample_stream_manager_repair_queued_page(sample_audio_key_t key,
                                                        uint32_t page_index,
                                                        sample_stream_priority_t priority,
                                                        uint32_t deadline_frames)
{
    if (sample_page_cache_get_page_state_key(key, page_index) != SAMPLE_PAGE_QUEUED)
    {
        return 1U;
    }

    if (sample_stream_manager_find_pending_key(key, page_index) != 0)
    {
        return 1U;
    }

    sample_page_window_owner_t remaining_owner;
    if (sample_page_cache_find_window_owner_key(key, page_index, &remaining_owner) != 0U)
    {
        sample_stream_active_desc_t owner;
        memset(&owner, 0, sizeof(owner));
        owner.owner_kind = remaining_owner.owner_kind;
        owner.owner_id = remaining_owner.owner_id;
        owner.owner_generation = remaining_owner.owner_generation;
        return sample_stream_manager_note_pending_key(key,
                                                      page_index,
                                                      priority,
                                                      deadline_frames,
                                                      &owner);
    }

    (void)sample_page_cache_cancel_queued_page_key(key,
                                                   page_index,
                                                   SAMPLE_STREAM_PENDING_REASON_ORPHAN);
    if (sample_page_cache_get_page_state_key(key, page_index) != SAMPLE_PAGE_QUEUED)
    {
        return 1U;
    }

    return sample_stream_manager_note_pending_key(key,
                                                  page_index,
                                                  priority,
                                                  deadline_frames,
                                                  0);
}

static void sample_stream_manager_repair_queued_pages(void)
{
    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        const sample_page_desc_t *const page = sample_page_cache_get_page_desc(i);
        if ((page == 0) || (page->state != SAMPLE_PAGE_QUEUED)
            || (sample_stream_manager_find_pending_key(page->key, page->page_index) != 0))
        {
            continue;
        }

        (void)sample_stream_manager_repair_queued_page(page->key,
                                                       page->page_index,
                                                       SAMPLE_STREAM_PRIORITY_URGENT,
                                                       0U);
    }
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
                                                         sample_stream_audio_frame_t deadline_audio_frame,
                                                         uint16_t fair_distance,
                                                         uint32_t age,
                                                         sample_stream_priority_t best_priority,
                                                         sample_stream_audio_frame_t best_deadline_audio_frame,
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
    if (deadline_audio_frame < best_deadline_audio_frame)
    {
        return 1U;
    }
    if (deadline_audio_frame > best_deadline_audio_frame)
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
    sample_stream_audio_frame_t best_deadline_audio_frame = SAMPLE_STREAM_AUDIO_FRAME_NEVER;
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

        if ((pending->format != target.format)
            || (pending->stride_floats != target.stride_floats)
            || (pending->frames_per_page != target.frames_per_page)
            || ((pending->registration_epoch != 0U)
                && (pending->registration_epoch != target.registration_epoch)))
        {
            sample_stream_manager_drop_pending_slot(pending, SAMPLE_STREAM_PENDING_REASON_ORPHAN);
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
                                                      pending->consume_deadline_audio_frame,
                                                      fair_distance,
                                                      age,
                                                      best_priority,
                                                      best_deadline_audio_frame,
                                                      best_fair_distance,
                                                      best_age) != 0U)
        {
            best_target = target;
            best_priority = priority;
            best_deadline_audio_frame = pending->consume_deadline_audio_frame;
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
    const wav_audio_codec_decode_block_fn decode_block =
        (info->info.channels == 1U)
            ? wav_audio_codec_select_pcm_decode_mono_block(info->info.bits_per_sample)
            : wav_audio_codec_select_pcm_decode_block(info->info.channels,
                                                      info->info.bits_per_sample);
    const uint32_t expected_block_align =
        (uint32_t)info->info.channels * ((uint32_t)info->info.bits_per_sample / 8U);
    const sample_audio_format_t expected_format =
        sample_audio_format_from_channels(info->info.channels);
    if ((decode_block == 0) || (info->info.block_align != expected_block_align)
        || (target->format != expected_format)
        || (target->stride_floats != sample_audio_format_stride_floats(expected_format)))
    {
        return SAMPLE_PAGE_LOAD_DECODE_FAILED;
    }

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
#if BRICK6_STREAM_TRACE
        g_sample_stream_service_physical_reads++;
#endif
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

        const uint32_t decoded_frames = valid_bytes / info->info.block_align;
        decode_block(io_buffer,
                     &target->frames_interleaved[write_frame * target->stride_floats],
                     decoded_frames);
        write_frame += decoded_frames;
        remaining_frames -= decoded_frames;
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
#if BRICK6_STREAM_TRACE
        g_sample_stream_service_physical_reads++;
#endif
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
#if BRICK6_STREAM_TRACE
    g_sample_stream_service_physical_reads = 0U;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    memset((void *)&g_sample_stream_trace, 0, sizeof(g_sample_stream_trace));
    g_sample_stream_trace.magic = SAMPLE_STREAM_TRACE_MAGIC;
    g_sample_stream_trace.enabled = 1U;
    g_sample_stream_last_service_cycle = sample_stream_trace_cycle();
    memset(&g_sample_stream_last_selected_key, 0, sizeof(g_sample_stream_last_selected_key));
    g_sample_stream_last_selected_key_valid = 0U;
#endif
}

void sample_stream_manager_trace_consume_miss(sample_audio_key_t key,
                                              uint32_t page_index,
                                              uint32_t reader_position,
                                              uint32_t frames_remaining)
{
#if BRICK6_STREAM_TRACE
    sample_stream_trace_trigger(SAMPLE_STREAM_TRACE_TRIGGER_CONSUME_MISS,
                                key,
                                page_index,
                                reader_position,
                                frames_remaining);
#else
    (void)key;
    (void)page_index;
    (void)reader_position;
    (void)frames_remaining;
#endif
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
            sample_page_window_owner_t remaining_owner;
            if (sample_page_cache_find_window_owner_key(pending->key,
                                                        pending->page_index,
                                                        &remaining_owner) != 0U)
            {
                pending->owner_kind = remaining_owner.owner_kind;
                pending->owner_id = remaining_owner.owner_id;
                pending->owner_generation = remaining_owner.owner_generation;
            }
            else
            {
                (void)sample_page_cache_cancel_queued_page_key(
                    pending->key,
                    pending->page_index,
                    SAMPLE_STREAM_PENDING_REASON_RELEASE_OWNER);
                if (sample_page_cache_get_page_state_key(pending->key,
                                                         pending->page_index)
                    == SAMPLE_PAGE_QUEUED)
                {
                    pending->owner_kind = (uint8_t)SAMPLE_STREAM_OWNER_NONE;
                    pending->owner_id = 0U;
                    pending->owner_generation = 0U;
                }
                else
                {
                    memset(pending, 0, sizeof(*pending));
                }
            }
        }
    }
    sample_stream_manager_repair_queued_pages();
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
    sample_page_stream_info_t info;
    const sample_audio_format_t format =
        (sample_page_cache_get_stream_info_key(key, &info) != 0U)
            ? sample_audio_format_or_stereo(info.format)
            : SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED;
    const uint32_t first_page = sample_audio_format_page_index_from_frame(format, start_frame);
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

static sample_stream_page_role_t sample_stream_manager_role_for_page(
    const sample_stream_active_desc_t *desc,
    uint32_t ahead)
{
    if (desc == 0)
    {
        return SAMPLE_STREAM_ROLE_SPECULATIVE;
    }
    if (desc->owner_kind == (uint8_t)SAMPLE_STREAM_OWNER_MULTI_LOOP)
    {
        return SAMPLE_STREAM_ROLE_LOOP;
    }
    if ((desc->role == (uint8_t)SAMPLE_STREAM_ROLE_START)
        || (desc->role == (uint8_t)SAMPLE_STREAM_ROLE_LOOP))
    {
        return (sample_stream_page_role_t)desc->role;
    }
    if (ahead == 0U)
    {
        return SAMPLE_STREAM_ROLE_CURRENT;
    }
    if (ahead == 1U)
    {
        return SAMPLE_STREAM_ROLE_NEIGHBOR;
    }
    return SAMPLE_STREAM_ROLE_ANTICIPATION;
}

static sample_stream_priority_t sample_stream_manager_priority_for_page(
    sample_stream_page_role_t role,
    uint32_t deadline_frames,
    uint32_t horizon_frames)
{
    if ((role == SAMPLE_STREAM_ROLE_START) || (role == SAMPLE_STREAM_ROLE_CURRENT))
    {
        return SAMPLE_STREAM_PRIORITY_URGENT;
    }
    if (role == SAMPLE_STREAM_ROLE_LOOP)
    {
        return SAMPLE_STREAM_PRIORITY_NORMAL;
    }
    const uint32_t horizon = (horizon_frames != 0U)
                                 ? horizon_frames
                                 : SAMPLE_AUDIO_FORMAT_STREAM_HORIZON_FRAMES;
    if (deadline_frames <= SAMPLE_AUDIO_FORMAT_STREAM_URGENT_FRAMES)
    {
        return SAMPLE_STREAM_PRIORITY_URGENT;
    }
    if (deadline_frames <= horizon)
    {
        return SAMPLE_STREAM_PRIORITY_NORMAL;
    }
    return SAMPLE_STREAM_PRIORITY_PREFETCH;
}

#if defined(BRICK6_MULTI_STREAM_DIAG)
static void sample_stream_manager_diag_capture_failure(
    const sample_stream_active_desc_t *desc,
    uint32_t failure_result,
    sample_multi_stream_diag_code_t code)
{
    if (desc == 0)
    {
        return;
    }

    const uint8_t is_loop =
        (desc->owner_kind == (uint8_t)SAMPLE_STREAM_OWNER_MULTI_LOOP) ? 1U : 0U;
    SAMPLE_MULTI_STREAM_DIAG_CAPTURE_FAILURE(
        desc->key,
        desc->key.object_id,
        desc->owner_id,
        1U,
        1U,
        3U,
        is_loop ? (uint8_t)SAMPLE_STREAM_OWNER_MULTI_VOICE : desc->owner_kind,
        desc->owner_id,
        desc->owner_generation,
        is_loop ? desc->owner_kind : (uint8_t)SAMPLE_STREAM_OWNER_MULTI_LOOP,
        desc->owner_id,
        desc->owner_generation,
        desc->format,
        desc->current_frame,
        is_loop ? UINT32_MAX : desc->current_frame,
        is_loop ? desc->current_frame : UINT32_MAX,
        failure_result,
        code);
}
#endif

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

    const sample_audio_format_t format = sample_audio_format_or_stereo(desc->format);
    const uint32_t current_page = sample_audio_format_page_index_from_frame(format, desc->current_frame);
    const uint32_t last_page = sample_audio_format_page_index_from_frame(format, desc->end_frame - 1U);
    const uint32_t first_ahead = (desc->request_current_page != 0U) ? 0U : 1U;
    uint8_t requested = 0U;
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
        const sample_stream_page_role_t role =
            sample_stream_manager_role_for_page(desc, ahead);
        const sample_stream_priority_t priority =
            sample_stream_manager_priority_for_page(role,
                                                    deadline_frames,
                                                    desc->horizon_frames);
        sample_stream_active_desc_t page_owner = *desc;
        page_owner.role = (uint8_t)role;

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

        if (sample_page_cache_get_page_state_key(desc->key, page_index) == SAMPLE_PAGE_QUEUED)
        {
            if (sample_stream_manager_note_pending_key(desc->key,
                                                       page_index,
                                                       priority,
                                                       deadline_frames,
                                                       &page_owner) != 0U)
            {
                sample_stream_manager_active_state_note(
                    desc->state, page_index, priority, desc->direction);
                requested = 1U;
            }
#if defined(BRICK6_MULTI_STREAM_DIAG)
            else
            {
                sample_stream_manager_diag_capture_failure(
                    desc,
                    (uint32_t)state_before,
                    SAMPLE_MULTI_STREAM_DIAG_QUEUE_PAGE_REQUEST);
            }
#endif
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
                                                                 &page_owner,
                                                                 SAMPLE_PAGE_ALLOC_VOICE_WINDOW);
        if (ok != 0U)
        {
            sample_stream_manager_active_state_note(
                desc->state, page_index, priority, desc->direction);
            requested = 1U;
        }
#if defined(BRICK6_MULTI_STREAM_DIAG)
        else
        {
            sample_stream_manager_diag_capture_failure(desc,
                                                       0U,
                                                       SAMPLE_MULTI_STREAM_DIAG_QUEUE_PAGE_REQUEST);
        }
#endif
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

    const sample_audio_format_t format = sample_audio_format_or_stereo(desc->format);
    const uint32_t current_page = sample_audio_format_page_index_from_frame(format, desc->current_frame);
    const uint32_t last_page = sample_audio_format_page_index_from_frame(format, desc->end_frame - 1U);
    const uint32_t first_ahead = (desc->request_current_page != 0U) ? 0U : 1U;

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
#if defined(BRICK6_MULTI_STREAM_DIAG)
            sample_stream_manager_diag_capture_failure(desc,
                                                       (uint32_t)state_before,
                                                       SAMPLE_MULTI_STREAM_DIAG_RESERVE_PAGE_REQUEST);
#endif
            sample_stream_manager_release_owner(desc->owner_kind,
                                                desc->owner_id,
                                                desc->owner_generation);
            return 0U;
        }
        const uint32_t deadline_frames =
            sample_stream_manager_page_deadline_frames(desc, page_index);
        const sample_stream_page_role_t role =
            sample_stream_manager_role_for_page(desc, ahead);
        const sample_stream_priority_t priority =
            sample_stream_manager_priority_for_page(role,
                                                    deadline_frames,
                                                    desc->horizon_frames);
        sample_stream_active_desc_t page_owner = *desc;
        page_owner.role = (uint8_t)role;

        if ((state_before != SAMPLE_PAGE_READY) && (state_before != SAMPLE_PAGE_QUEUED)
            && (state_before != SAMPLE_PAGE_IN_FLIGHT))
        {
            if (sample_stream_manager_request_page_with_priority_key(desc->key,
                                                                     page_index,
                                                                     priority,
                                                                     deadline_frames,
                                                                     &page_owner,
                                                                     SAMPLE_PAGE_ALLOC_VOICE_WINDOW) == 0U)
            {
#if defined(BRICK6_MULTI_STREAM_DIAG)
                sample_stream_manager_diag_capture_failure(
                    desc,
                    (uint32_t)state_before,
                    SAMPLE_MULTI_STREAM_DIAG_RESERVE_PAGE_REQUEST);
#endif
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
                                                              &page_owner) == 0U)
            {
#if defined(BRICK6_MULTI_STREAM_DIAG)
                sample_stream_manager_diag_capture_failure(
                    desc,
                    (uint32_t)state_before,
                    SAMPLE_MULTI_STREAM_DIAG_RESERVE_PAGE_REQUEST);
#endif
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
#if defined(BRICK6_MULTI_STREAM_DIAG)
            sample_stream_manager_diag_capture_failure(desc,
                                                       0U,
                                                       SAMPLE_MULTI_STREAM_DIAG_RESERVE_WINDOW_LOCK);
#endif
            sample_stream_manager_release_owner(desc->owner_kind,
                                                desc->owner_id,
                                                desc->owner_generation);
            return 0U;
        }
        sd_access_gate_set_streaming_critical(1U);
    }

    return 1U;
}

static uint8_t sample_stream_manager_has_urgent_pending(void)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        if ((g_sample_stream_pending[i].active != 0U)
            && (g_sample_stream_pending[i].priority
                == (uint8_t)SAMPLE_STREAM_PRIORITY_URGENT))
        {
            return 1U;
        }
    }
    return 0U;
}

void sample_stream_manager_service(uint32_t byte_budget)
{
    if (byte_budget == 0U)
    {
        return;
    }

    const uint32_t start_tick = HAL_GetTick();
#if BRICK6_STREAM_TRACE
    const uint32_t service_cycle = sample_stream_trace_cycle();
    const uint32_t service_interval = service_cycle - g_sample_stream_last_service_cycle;
    g_sample_stream_last_service_cycle = service_cycle;
    if (service_interval > g_sample_stream_trace.max_service_interval_cycles)
    {
        g_sample_stream_trace.max_service_interval_cycles = service_interval;
    }
#endif
    sample_stream_manager_repair_queued_pages();
    uint32_t pages_this_call = 0U;
    g_sample_stream_service_fatfs_ops = 0U;
#if BRICK6_STREAM_TRACE
    g_sample_stream_service_physical_reads = 0U;
    const uint16_t backlog = sample_stream_trace_pending_count();
    if (backlog > g_sample_stream_trace.max_backlog)
    {
        g_sample_stream_trace.max_backlog = backlog;
    }
#endif

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

        if ((sample_audio_key_equal(&target.key, &stream_info.key) == 0U)
            || (target.format != stream_info.format)
            || (target.stride_floats != stream_info.stride_floats)
            || (target.frames_per_page != stream_info.frames_per_page)
            || ((target.registration_epoch != 0U)
                && (target.registration_epoch != stream_info.registration_epoch)))
        {
            (void)sample_page_cache_set_page_state_key(target.key,
                                                       target.page_index,
                                                       SAMPLE_PAGE_ERROR);
            sample_stream_manager_clear_pending_key(target.key,
                                                    target.page_index,
                                                    SAMPLE_STREAM_PENDING_REASON_ORPHAN);
            return;
        }

        sample_stream_reader_t *reader = 0;
        FIL fallback_file;
        FIL *fp = 0;
        uint8_t fallback_open = 0U;
        uint8_t used_contiguous_backend = 0U;
        sample_page_load_token_t load_token;

        const FSIZE_t offset = (FSIZE_t)stream_info.data_offset
                              + ((FSIZE_t)target.start_frame
                                 * (FSIZE_t)stream_info.info.block_align);

        uint32_t delivered_pages = 1U;
        uint32_t consumed = target.frame_count * stream_info.info.block_align;
        sample_page_load_result_t load_result = SAMPLE_PAGE_LOAD_OK;
#if BRICK6_STREAM_TRACE
        sample_stream_pending_t *const traced_pending = g_sample_stream_selected_pending;
        sample_stream_trace_op_t *const traced_op =
            sample_stream_trace_pending_op(traced_pending);
        const uint32_t select_cycle = sample_stream_trace_cycle();
        const uint32_t physical_reads_before = g_sample_stream_service_physical_reads;
        if (traced_op != 0)
        {
            traced_op->selected_audio_frame = sample_stream_time_now();
            traced_op->select_cycle = select_cycle;
            traced_op->read_begin_cycle = select_cycle;
            traced_op->pending_global = sample_stream_trace_pending_count();
            traced_op->service_interval_cycles = service_interval;
            traced_op->source_bytes = consumed;
            const uint32_t wait_cycles = select_cycle - traced_op->request_cycle;
            if (wait_cycles > g_sample_stream_trace.max_wait_cycles)
            {
                g_sample_stream_trace.max_wait_cycles = wait_cycles;
            }
            if ((traced_pending != 0)
                && (traced_op->selected_audio_frame
                    > traced_pending->consume_deadline_audio_frame))
            {
                const uint64_t late_frames = traced_op->selected_audio_frame
                                             - traced_pending->consume_deadline_audio_frame;
                const uint64_t late_cycles_64 =
                    (late_frames * (uint64_t)SystemCoreClock) / 48000ULL;
                const uint32_t late_cycles = (late_cycles_64 > UINT32_MAX)
                                                 ? UINT32_MAX
                                                 : (uint32_t)late_cycles_64;
                if (late_frames > g_sample_stream_trace.max_deadline_late_frames)
                {
                    g_sample_stream_trace.max_deadline_late_frames = late_frames;
                }
                if (late_cycles > g_sample_stream_trace.max_deadline_late_cycles)
                {
                    g_sample_stream_trace.max_deadline_late_cycles = late_cycles;
                }
            }
        }
        if ((g_sample_stream_last_selected_key_valid != 0U)
            && (sample_audio_key_equal(&g_sample_stream_last_selected_key, &target.key) == 0U))
        {
            g_sample_stream_trace.file_changes++;
        }
        g_sample_stream_last_selected_key = target.key;
        g_sample_stream_last_selected_key_valid = 1U;
#endif
        if (sample_page_cache_begin_in_flight(&target, &load_token) == 0U)
        {
            sample_stream_manager_clear_pending_key(target.key,
                                                    target.page_index,
                                                    SAMPLE_STREAM_PENDING_REASON_ORPHAN);
            continue;
        }
#if BRICK6_STREAM_TRACE
        if (traced_op != 0)
        {
            traced_op->in_flight_audio_frame = sample_stream_time_now();
        }
#endif
        if ((stream_info.raw_pcm24 == 0U)
            && (stream_info.stream_safe.backend_kind
                == (uint8_t)SAMPLE_STREAM_BACKEND_SAFE_CONTIGUOUS)
            && (stream_info.stream_safe.valid != 0U))
        {
            load_result = sample_stream_backend_contiguous_load_page(&stream_info, &target);
            if (load_result == SAMPLE_PAGE_LOAD_OK)
            {
                used_contiguous_backend = 1U;
#if BRICK6_STREAM_TRACE
                g_sample_stream_service_physical_reads++;
#endif
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
                    (void)sample_page_cache_finish_in_flight(
                        &load_token, SAMPLE_PAGE_FINISH_ERROR);
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
                    (void)sample_page_cache_finish_in_flight(
                        &load_token, SAMPLE_PAGE_FINISH_ERROR);
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
                    (void)sample_page_cache_finish_in_flight(
                        &load_token, SAMPLE_PAGE_FINISH_ERROR);
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
#if BRICK6_STREAM_TRACE
            if (traced_op != 0)
            {
                traced_op->read_end_cycle = sample_stream_trace_cycle();
                traced_op->backend = used_contiguous_backend;
                traced_op->physical_reads = (uint8_t)(g_sample_stream_service_physical_reads
                                                       - physical_reads_before);
                traced_op->success = 0U;
            }
#endif
            (void)sample_page_cache_finish_in_flight(&load_token,
                                                     SAMPLE_PAGE_FINISH_ERROR);
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

        if (sample_page_cache_finish_in_flight(&load_token,
                                               SAMPLE_PAGE_FINISH_READY) == 0U)
        {
            sample_stream_manager_clear_pending_key(target.key,
                                                    target.page_index,
                                                    SAMPLE_STREAM_PENDING_REASON_ORPHAN);
            return;
        }
#if BRICK6_STREAM_TRACE
        if (traced_op != 0)
        {
            traced_op->read_end_cycle = sample_stream_trace_cycle();
            traced_op->ready_cycle = traced_op->read_end_cycle;
            traced_op->ready_audio_frame = sample_stream_time_now();
            traced_op->backend = used_contiguous_backend;
            traced_op->physical_reads = (uint8_t)(g_sample_stream_service_physical_reads
                                                   - physical_reads_before);
            traced_op->success = 1U;
            const uint32_t read_cycles = traced_op->read_end_cycle - traced_op->read_begin_cycle;
            if (read_cycles > g_sample_stream_trace.max_read_cycles)
            {
                g_sample_stream_trace.max_read_cycles = read_cycles;
            }
            if ((traced_pending != 0)
                && (traced_op->selected_audio_frame
                    > traced_pending->consume_deadline_audio_frame))
            {
                sample_stream_trace_trigger(SAMPLE_STREAM_TRACE_TRIGGER_LATE_SELECTION,
                                            target.key,
                                            target.page_index,
                                            traced_op->reader_position,
                                            traced_op->frames_remaining);
            }
        }
#endif
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
#if BRICK6_STREAM_TRACE
        if (pages_this_call > g_sample_stream_trace.max_pages_per_service)
        {
            g_sample_stream_trace.max_pages_per_service = pages_this_call;
        }
#endif

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
        if ((elapsed_ticks >= SAMPLE_STREAM_SERVICE_MAX_TICKS)
            && ((pages_this_call >= SAMPLE_STREAM_SERVICE_MIN_URGENT_PAGES)
                || (sample_stream_manager_has_urgent_pending() == 0U)))
        {
            break;
        }
    }
}

uint8_t sample_stream_manager_has_pending_sd_work(void)
{
    sample_stream_manager_repair_queued_pages();
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

#if defined(BRICK6_MULTI_STREAM_DIAG)
void sample_stream_manager_get_debug_stats(uint8_t current_owner_kind,
                                           uint8_t current_owner_id,
                                           uint32_t current_generation,
                                           uint8_t loop_owner_kind,
                                           uint8_t loop_owner_id,
                                           uint32_t loop_generation,
                                           uint32_t *out_pending_global,
                                           uint32_t *out_pending_current_owner,
                                           uint32_t *out_pending_loop_owner,
                                           uint32_t *out_readers_active)
{
    uint32_t pending_global = 0U;
    uint32_t pending_current = 0U;
    uint32_t pending_loop = 0U;
    uint32_t readers_active = 0U;

    for (uint32_t i = 0U; i < SAMPLE_STREAM_PENDING_MAX; ++i)
    {
        const sample_stream_pending_t *const pending = &g_sample_stream_pending[i];
        if (pending->active == 0U)
        {
            continue;
        }
        pending_global++;
        if ((pending->owner_kind == current_owner_kind)
            && (pending->owner_id == current_owner_id)
            && (pending->owner_generation == current_generation))
        {
            pending_current++;
        }
        if ((pending->owner_kind == loop_owner_kind)
            && (pending->owner_id == loop_owner_id)
            && (pending->owner_generation == loop_generation))
        {
            pending_loop++;
        }
    }

    for (uint32_t i = 0U; i < SAMPLE_STREAM_MAX_ACTIVE; ++i)
    {
        if (g_sample_stream_readers[i].in_use != 0U)
        {
            readers_active++;
        }
    }

    if (out_pending_global != 0)
    {
        *out_pending_global = pending_global;
    }
    if (out_pending_current_owner != 0)
    {
        *out_pending_current_owner = pending_current;
    }
    if (out_pending_loop_owner != 0)
    {
        *out_pending_loop_owner = pending_loop;
    }
    if (out_readers_active != 0)
    {
        *out_readers_active = readers_active;
    }
}

static void sample_multi_stream_diag_fill_pages(
    volatile sample_multi_stream_diag_page_t *out_pages,
    sample_audio_key_t key,
    uint32_t first_page,
    uint32_t page_count,
    uint8_t owner_kind,
    uint8_t owner_id,
    uint32_t owner_generation,
    uint32_t *out_acquired)
{
    uint32_t acquired = 0U;
    for (uint32_t i = 0U; i < SAMPLE_MULTI_STREAM_DIAG_MAX_WINDOW_PAGES; ++i)
    {
        memset((void *)&out_pages[i], 0, sizeof(out_pages[i]));
        out_pages[i].page_index = UINT32_MAX;
        if (i >= page_count)
        {
            continue;
        }

        sample_page_window_debug_t page;
        if (sample_page_cache_get_window_page_debug(key,
                                                    first_page + i,
                                                    owner_kind,
                                                    owner_id,
                                                    owner_generation,
                                                    &page) == 0U)
        {
            out_pages[i].page_index = first_page + i;
            continue;
        }
        out_pages[i].page_index = page.page_index;
        out_pages[i].generation = page.generation;
        out_pages[i].slot_index = page.slot_index;
        out_pages[i].frame_count = page.frame_count;
        out_pages[i].use_count = page.use_count;
        out_pages[i].window_pin_count = page.window_pin_count;
        out_pages[i].state = (uint8_t)page.state;
        out_pages[i].owner_lock_count = page.owner_lock_count;
        if (page.owner_lock_count != 0U)
        {
            acquired++;
        }
    }
    if (out_acquired != 0)
    {
        *out_acquired = acquired;
    }
}

__attribute__((noinline, used, externally_visible))
void sample_multi_stream_diag_capture_failure(
    sample_audio_key_t key,
    uint16_t sample_id,
    uint8_t voice_index,
    uint8_t voice_active,
    uint8_t reader_active,
    uint8_t source_kind,
    uint8_t current_owner_kind,
    uint8_t current_owner_id,
    uint32_t current_generation,
    uint8_t loop_owner_kind,
    uint8_t loop_owner_id,
    uint32_t loop_generation,
    sample_audio_format_t format,
    uint32_t position_frame,
    uint32_t current_frame,
    uint32_t loop_frame,
    uint32_t failure_result,
    sample_multi_stream_diag_code_t code)
{
    if (g_sample_multi_stream_diag_frozen != 0U)
    {
        return;
    }

    memset((void *)&g_sample_multi_stream_diag, 0, sizeof(g_sample_multi_stream_diag));
    g_sample_multi_stream_diag.magic = SAMPLE_MULTI_STREAM_DIAG_MAGIC;
    g_sample_multi_stream_diag.code = (uint32_t)code;
    g_sample_multi_stream_diag.failure_result = failure_result;
    g_sample_multi_stream_diag.sample_id = sample_id;
    g_sample_multi_stream_diag.key_domain = key.domain;
    g_sample_multi_stream_diag.key_object_id = key.object_id;
    g_sample_multi_stream_diag.voice_index = voice_index;
    g_sample_multi_stream_diag.voice_active = voice_active;
    g_sample_multi_stream_diag.reader_active = reader_active;
    g_sample_multi_stream_diag.source_kind = source_kind;
    g_sample_multi_stream_diag.current_owner_kind = current_owner_kind;
    g_sample_multi_stream_diag.current_owner_id = current_owner_id;
    g_sample_multi_stream_diag.current_generation = current_generation;
    g_sample_multi_stream_diag.loop_owner_kind = loop_owner_kind;
    g_sample_multi_stream_diag.loop_owner_id = loop_owner_id;
    g_sample_multi_stream_diag.loop_generation = loop_generation;
    g_sample_multi_stream_diag.format = (uint32_t)sample_audio_format_or_stereo(format);
    g_sample_multi_stream_diag.position_frame = position_frame;
    g_sample_multi_stream_diag.current_page = UINT32_MAX;
    g_sample_multi_stream_diag.loop_page = UINT32_MAX;

    const sample_audio_format_t safe_format = sample_audio_format_or_stereo(format);
    const uint32_t window_pages = sample_audio_format_window_pages(safe_format);
    uint32_t acquired = 0U;
    if (current_frame != UINT32_MAX)
    {
        g_sample_multi_stream_diag.current_page =
            sample_audio_format_page_index_from_frame(safe_format, current_frame);
        g_sample_multi_stream_diag.current_expected_pages = window_pages;
        sample_multi_stream_diag_fill_pages(g_sample_multi_stream_diag.current_pages,
                                             key,
                                             g_sample_multi_stream_diag.current_page,
                                             window_pages,
                                             current_owner_kind,
                                             current_owner_id,
                                             current_generation,
                                             &acquired);
        g_sample_multi_stream_diag.current_acquired_pages = acquired;
    }
    if (loop_frame != UINT32_MAX)
    {
        g_sample_multi_stream_diag.loop_page =
            sample_audio_format_page_index_from_frame(safe_format, loop_frame);
        g_sample_multi_stream_diag.loop_expected_pages = window_pages;
        sample_multi_stream_diag_fill_pages(g_sample_multi_stream_diag.loop_pages,
                                             key,
                                             g_sample_multi_stream_diag.loop_page,
                                             window_pages,
                                             loop_owner_kind,
                                             loop_owner_id,
                                             loop_generation,
                                             &acquired);
        g_sample_multi_stream_diag.loop_acquired_pages = acquired;
    }

    uint32_t pending_global = 0U;
    uint32_t pending_current = 0U;
    uint32_t pending_loop = 0U;
    uint32_t readers_active = 0U;
    sample_stream_manager_get_debug_stats(current_owner_kind,
                                           current_owner_id,
                                           current_generation,
                                           loop_owner_kind,
                                           loop_owner_id,
                                           loop_generation,
                                           &pending_global,
                                           &pending_current,
                                           &pending_loop,
                                           &readers_active);
    g_sample_multi_stream_diag.pending_global = pending_global;
    g_sample_multi_stream_diag.pending_current_owner = pending_current;
    g_sample_multi_stream_diag.pending_loop_owner = pending_loop;
    g_sample_multi_stream_diag.readers_active = readers_active;
    g_sample_multi_stream_diag.locks_used = sample_page_cache_debug_count_window_locks();
    g_sample_multi_stream_diag.pages_free = sample_page_cache_debug_count_free_pages();
    g_sample_multi_stream_diag.pc = (uint32_t)(uintptr_t)__builtin_return_address(0);
    uintptr_t link_register = 0U;
    __asm volatile("mov %0, lr" : "=r"(link_register));
    g_sample_multi_stream_diag.lr = (uint32_t)link_register;
    g_sample_multi_stream_diag.frozen = 1U;
    __DMB();
    g_sample_multi_stream_diag_frozen = 1U;
}

__attribute__((noinline, used, externally_visible))
void sample_multi_stream_diag_capture_fault(const uint32_t *stack_pointer,
                                            uint32_t exc_return,
                                            uint32_t fault_type)
{
    const uint32_t extended_words = ((exc_return & (1UL << 4U)) == 0U) ? 18U : 0U;
    const uintptr_t begin = (uintptr_t)stack_pointer;
    const uintptr_t end = begin + ((uintptr_t)(extended_words + 8U) * sizeof(uint32_t));

    g_sample_multi_stream_diag.fault_type = fault_type;
    g_sample_multi_stream_diag.exc_return = exc_return;
    g_sample_multi_stream_diag.cfsr = SCB->CFSR;
    g_sample_multi_stream_diag.hfsr = SCB->HFSR;
    g_sample_multi_stream_diag.bfar = SCB->BFAR;
    g_sample_multi_stream_diag.mmfar = SCB->MMFAR;
    if ((stack_pointer != 0)
        && (end >= begin)
        && (((begin >= 0x20000000UL) && (end <= 0x20020000UL))
            || ((begin >= 0x24000000UL) && (end <= 0x24080000UL))
            || ((begin >= 0x30000000UL) && (end <= 0x30048000UL))
            || ((begin >= 0x38000000UL) && (end <= 0x38010000UL))))
    {
        const uint32_t *const frame = stack_pointer + extended_words;
        g_sample_multi_stream_diag.stacked_r0 = frame[0];
        g_sample_multi_stream_diag.stacked_r1 = frame[1];
        g_sample_multi_stream_diag.stacked_r2 = frame[2];
        g_sample_multi_stream_diag.stacked_r3 = frame[3];
        g_sample_multi_stream_diag.stacked_r12 = frame[4];
        g_sample_multi_stream_diag.stacked_lr = frame[5];
        g_sample_multi_stream_diag.stacked_pc = frame[6];
        g_sample_multi_stream_diag.stacked_xpsr = frame[7];
        g_sample_multi_stream_diag.pc = frame[6];
        g_sample_multi_stream_diag.lr = frame[5];
    }

    if (g_sample_multi_stream_diag_frozen == 0U)
    {
        g_sample_multi_stream_diag.magic = SAMPLE_MULTI_STREAM_DIAG_MAGIC;
        g_sample_multi_stream_diag.code = (uint32_t)SAMPLE_MULTI_STREAM_DIAG_FAULT;
        g_sample_multi_stream_diag.failure_result = fault_type;
        g_sample_multi_stream_diag.frozen = 1U;
        __DMB();
        g_sample_multi_stream_diag_frozen = 1U;
    }
}

uint8_t sample_multi_stream_diag_breakpoint_pending(void)
{
    return ((g_sample_multi_stream_diag_frozen != 0U)
            && (g_sample_multi_stream_diag_breakpoint_seen == 0U)) ? 1U : 0U;
}

__attribute__((noinline, used, externally_visible))
void sample_multi_stream_diag_breakpoint(void)
{
    if (g_sample_multi_stream_diag_frozen != 0U)
    {
        g_sample_multi_stream_diag_breakpoint_seen = 1U;
    }
}
#endif
