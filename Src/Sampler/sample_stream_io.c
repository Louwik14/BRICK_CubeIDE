#include "Sampler/sample_stream_io.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_stream_backend_physical.h"
#include "Sampler/sample_stream_decoder.h"
#include "Sampler/sample_stream_limits.h"
#include "Sampler/sample_stream_manager.h"
#include "SD/sd_block_device.h"
#include "SD/sd_scheduler_runtime.h"
#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#define SAMPLE_STREAM_IO_FILE_OPEN_COOKIE (0x5354524DU)
#ifndef BRICK6_STREAM_READ_CHUNK_KIB
#define BRICK6_STREAM_READ_CHUNK_KIB (32U)
#endif
#define SAMPLE_STREAM_IO_MAX_CHUNK_BYTES (32768U)
#define SAMPLE_STREAM_IO_SECTOR_BYTES (512U)
#define SAMPLE_STREAM_IO_READ_SCRATCH_BYTES \
    (((SAMPLE_PAGE_BYTES + SAMPLE_STREAM_IO_SECTOR_BYTES + 31U) / 32U) * 32U)
#define SAMPLE_STREAM_IO_SCRATCH_COUNT (2U)

_Static_assert(SAMPLE_STREAM_IO_READ_SCRATCH_BYTES >= (SAMPLE_PAGE_BYTES + 511U),
               "Physical reads require one page plus sector alignment headroom");

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
    uint32_t media_epoch;
    FSIZE_t current_file_offset;
    uint32_t file_open_cookie;
    uint8_t in_use;
    uint8_t file_open;
    sample_stream_physical_cursor_t physical_cursor;
    FIL file;
} sample_stream_io_reader_t;

SDRAM_STREAM_SERVICE static sample_stream_io_reader_t
    g_sample_stream_io_readers[SAMPLE_STREAM_IO_MAX_READERS];
SDRAM_STREAM_SERVICE static char
    g_sample_stream_io_paths[SAMPLE_STREAM_IO_MAX_READERS][SAMPLE_PAGE_CACHE_PATH_MAX];
typedef enum
{
    SAMPLE_STREAM_IO_SCRATCH_FREE = 0,
    SAMPLE_STREAM_IO_SCRATCH_DMA,
    SAMPLE_STREAM_IO_SCRATCH_RAW_READY,
    SAMPLE_STREAM_IO_SCRATCH_DECODING
} sample_stream_io_scratch_state_t;

SDRAM_STREAM_SCRATCH __attribute__((aligned(32))) static uint8_t
    g_sample_stream_io_read_scratch[SAMPLE_STREAM_IO_SCRATCH_COUNT]
                                   [SAMPLE_STREAM_IO_READ_SCRATCH_BYTES];
SDRAM_STREAM_SCRATCH static uint8_t g_sample_stream_io_page_scratch[SAMPLE_PAGE_BYTES];
typedef struct
{
    sample_stream_io_command_t command;
    sample_page_load_target_t target;
    sample_stream_io_result_t result;
    sample_stream_io_reader_t *reader;
    sample_stream_physical_cursor_t local_physical_cursor;
    sample_stream_backend_physical_async_t physical;
    const uint8_t *source;
    uint32_t media_epoch;
    uint32_t order;
    uint8_t *scratch;
    uint8_t scratch_index;
    uint8_t state;
    uint8_t active;
    uint8_t physical_active;
} sample_stream_io_async_t;
SDRAM_STREAM_SERVICE static sample_stream_io_async_t
    g_sample_stream_io_async[SAMPLE_STREAM_IO_SCRATCH_COUNT];
static uint32_t g_sample_stream_io_next_order;
static sample_audio_key_t g_sample_stream_io_cache_key;
static uint32_t g_sample_stream_io_cache_registration_epoch;
static FSIZE_t g_sample_stream_io_cache_offset;
static uint32_t g_sample_stream_io_cache_bytes;
static sample_stream_read_chunk_kib_t g_sample_stream_io_chunk_kib =
    (sample_stream_read_chunk_kib_t)BRICK6_STREAM_READ_CHUNK_KIB;

uint8_t sample_stream_io_command_init(sample_stream_io_command_t *out_command,
                                      const sample_page_load_token_t *token,
                                      const sample_page_load_target_t *target,
                                      const sample_page_stream_info_t *stream_info)
{
    if ((out_command == 0) || (token == 0) || (target == 0) || (stream_info == 0))
    {
        return 0U;
    }
    memset(out_command, 0, sizeof(*out_command));
    out_command->token = *token;
    out_command->target = (sample_stream_io_target_t){
        .key = target->key,
        .page_index = target->page_index,
        .start_frame = target->start_frame,
        .frame_count = target->frame_count,
        .frames_per_page = target->frames_per_page,
        .registration_epoch = target->registration_epoch,
        .page_generation = target->page_generation,
        .slot_index = target->slot_index,
        .format = target->format,
        .stride_floats = target->stride_floats,
    };
    out_command->stream_info = *stream_info;
    return 1U;
}

static uint8_t sample_stream_io_chunk_valid(sample_stream_read_chunk_kib_t chunk_kib)
{
    return ((chunk_kib == SAMPLE_STREAM_READ_CHUNK_4_KIB)
            || (chunk_kib == SAMPLE_STREAM_READ_CHUNK_8_KIB)
            || (chunk_kib == SAMPLE_STREAM_READ_CHUNK_16_KIB)
            || (chunk_kib == SAMPLE_STREAM_READ_CHUNK_32_KIB)) ? 1U : 0U;
}

static void sample_stream_io_invalidate_read_cache(void)
{
    memset(&g_sample_stream_io_cache_key, 0, sizeof(g_sample_stream_io_cache_key));
    g_sample_stream_io_cache_registration_epoch = 0U;
    g_sample_stream_io_cache_offset = 0U;
    g_sample_stream_io_cache_bytes = 0U;
}

static char *sample_stream_io_reader_path(sample_stream_io_reader_t *reader)
{
    if (reader == 0)
    {
        return 0;
    }
    const uint32_t index = (uint32_t)(reader - g_sample_stream_io_readers);
    return (index < SAMPLE_STREAM_IO_MAX_READERS) ? g_sample_stream_io_paths[index] : 0;
}

static const char *sample_stream_io_reader_path_const(const sample_stream_io_reader_t *reader)
{
    if (reader == 0)
    {
        return 0;
    }
    const uint32_t index = (uint32_t)(reader - g_sample_stream_io_readers);
    return (index < SAMPLE_STREAM_IO_MAX_READERS) ? g_sample_stream_io_paths[index] : 0;
}

static uint16_t sample_stream_io_close_reader(sample_stream_io_reader_t *reader)
{
    uint16_t operations = 0U;
    if (reader == 0)
    {
        return operations;
    }
    if ((reader->file_open != 0U)
        && (reader->file_open_cookie == SAMPLE_STREAM_IO_FILE_OPEN_COOKIE))
    {
        operations++;
        (void)f_close(&reader->file);
    }
    reader->file_open = 0U;
    reader->file_open_cookie = 0U;
    memset(&reader->file, 0, sizeof(reader->file));
    return operations;
}

static void sample_stream_io_clear_reader(sample_stream_io_reader_t *reader)
{
    if (reader == 0)
    {
        return;
    }
    (void)sample_stream_io_close_reader(reader);
    char *const path = sample_stream_io_reader_path(reader);
    if (path != 0)
    {
        path[0] = '\0';
    }
    memset(reader, 0, sizeof(*reader));
}

static uint8_t sample_stream_io_reader_matches(const sample_stream_io_reader_t *reader,
                                               const sample_stream_io_command_t *command)
{
    const sample_page_stream_info_t *const info = &command->stream_info;
    const char *const path = sample_stream_io_reader_path_const(reader);
    return ((reader != 0) && (reader->in_use != 0U) && (path != 0)
            && (sample_audio_key_equal(&reader->key, &command->target.key) != 0U)
            && (reader->data_offset == info->data_offset)
            && (reader->total_frames == info->total_frames)
            && (reader->bytes_per_frame == info->info.block_align)
            && (reader->format == info->format)
            && (reader->stride_floats == info->stride_floats)
            && (reader->frames_per_page == info->frames_per_page)
            && (reader->registration_epoch == info->registration_epoch)
            && (reader->media_epoch == sd_access_media_epoch())
            && (strncmp(path, info->path, SAMPLE_PAGE_CACHE_PATH_MAX) == 0)) ? 1U : 0U;
}

static sample_stream_io_reader_t *sample_stream_io_find_reader(sample_audio_key_t key)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_MAX_READERS; ++i)
    {
        if ((g_sample_stream_io_readers[i].in_use != 0U)
            && (sample_audio_key_equal(&g_sample_stream_io_readers[i].key, &key) != 0U))
        {
            return &g_sample_stream_io_readers[i];
        }
    }
    return 0;
}

static sample_stream_io_reader_t *sample_stream_io_get_reader(
    const sample_stream_io_command_t *command)
{
    sample_stream_io_reader_t *reader = sample_stream_io_find_reader(command->target.key);
    if ((reader != 0) && (sample_stream_io_reader_matches(reader, command) == 0U))
    {
        sample_stream_io_clear_reader(reader);
        reader = 0;
    }
    if (reader != 0)
    {
        return reader;
    }

    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_MAX_READERS; ++i)
    {
        if (g_sample_stream_io_readers[i].in_use == 0U)
        {
            reader = &g_sample_stream_io_readers[i];
            memset(reader, 0, sizeof(*reader));
            memcpy(g_sample_stream_io_paths[i], command->stream_info.path,
                   SAMPLE_PAGE_CACHE_PATH_MAX);
            reader->in_use = 1U;
            reader->key = command->target.key;
            reader->data_offset = command->stream_info.data_offset;
            reader->total_frames = command->stream_info.total_frames;
            reader->bytes_per_frame = command->stream_info.info.block_align;
            reader->format = command->stream_info.format;
            reader->stride_floats = command->stream_info.stride_floats;
            reader->frames_per_page = command->stream_info.frames_per_page;
            reader->registration_epoch = command->stream_info.registration_epoch;
            reader->media_epoch = sd_access_media_epoch();
            return reader;
        }
    }
    return 0;
}

static uint8_t sample_stream_io_open_reader(sample_stream_io_reader_t *reader,
                                            uint16_t *operations)
{
    if ((reader->file_open != 0U)
        && (reader->file_open_cookie == SAMPLE_STREAM_IO_FILE_OPEN_COOKIE))
    {
        return 1U;
    }
    const char *const path = sample_stream_io_reader_path_const(reader);
    if (path == 0)
    {
        return 0U;
    }
    (*operations)++;
    if (f_open(&reader->file, path, FA_READ) != FR_OK)
    {
        return 0U;
    }
    reader->file_open = 1U;
    reader->file_open_cookie = SAMPLE_STREAM_IO_FILE_OPEN_COOKIE;
    reader->current_file_offset = 0U;
    return 1U;
}

void sample_stream_io_init(void)
{
    memset(g_sample_stream_io_readers, 0, sizeof(g_sample_stream_io_readers));
    memset(g_sample_stream_io_paths, 0, sizeof(g_sample_stream_io_paths));
    memset(g_sample_stream_io_async, 0, sizeof(g_sample_stream_io_async));
    g_sample_stream_io_next_order = 1U;
    sd_block_device_async_init();
    sd_scheduler_runtime_init();
    sample_stream_io_invalidate_read_cache();
    if (sample_stream_io_chunk_valid(g_sample_stream_io_chunk_kib) == 0U)
    {
        g_sample_stream_io_chunk_kib = SAMPLE_STREAM_READ_CHUNK_32_KIB;
    }
}

void sample_stream_io_reset(void)
{
    sample_stream_io_cancel();
    sample_stream_io_invalidate_read_cache();
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_MAX_READERS; ++i)
    {
        sample_stream_io_clear_reader(&g_sample_stream_io_readers[i]);
    }
}

void sample_stream_io_release_key(sample_audio_key_t key)
{
    if (sample_audio_key_equal(&g_sample_stream_io_cache_key, &key) != 0U)
    {
        sample_stream_io_invalidate_read_cache();
    }
    sample_stream_io_reader_t *const reader = sample_stream_io_find_reader(key);
    if (reader != 0)
    {
        sample_stream_io_clear_reader(reader);
    }
}

uint32_t sample_stream_io_active_reader_count(void)
{
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_MAX_READERS; ++i)
    {
        count += (g_sample_stream_io_readers[i].in_use != 0U) ? 1U : 0U;
    }
    return count;
}

uint8_t sample_stream_io_set_read_chunk_kib(sample_stream_read_chunk_kib_t chunk_kib)
{
    if (sample_stream_io_chunk_valid(chunk_kib) == 0U)
    {
        return 0U;
    }
    if (g_sample_stream_io_chunk_kib != chunk_kib)
    {
        g_sample_stream_io_chunk_kib = chunk_kib;
        sample_stream_io_invalidate_read_cache();
    }
    return 1U;
}

sample_stream_read_chunk_kib_t sample_stream_io_get_read_chunk_kib(void)
{
    return g_sample_stream_io_chunk_kib;
}

static void sample_stream_io_decode_async(void)
{
    sample_stream_io_async_t *async = 0;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_SCRATCH_COUNT; ++i)
    {
        if ((g_sample_stream_io_async[i].active != 0U)
            && (g_sample_stream_io_async[i].state == SAMPLE_STREAM_IO_SCRATCH_DECODING))
        {
            async = &g_sample_stream_io_async[i];
            break;
        }
    }
    if ((async == 0) || (async->result.load_result != SAMPLE_PAGE_LOAD_OK))
    {
        return;
    }
    sample_page_load_target_t current_target;
    if ((async->media_epoch != sd_access_media_epoch())
        || (sample_page_cache_resolve_loading_target(
                &async->command.token, &current_target) == 0U)
        || (current_target.frames_interleaved
            != async->target.frames_interleaved)
        || (current_target.page_generation
            != async->target.page_generation)
        || (current_target.registration_epoch
            != async->target.registration_epoch))
    {
        async->result.load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
        return;
    }
    async->target = current_target;
    const uint32_t decode_begin = DWT->CYCCNT;
    async->result.load_result = sample_stream_decoder_decode_page(
        &async->command.stream_info, &async->target, async->source,
        async->result.source_bytes);
    async->result.decode_cycles = DWT->CYCCNT - decode_begin;
}

static void sample_stream_io_run_fatfs_fallback(sample_stream_io_async_t *async)
{
    const sample_stream_io_command_t *const command = &async->command;
    sample_stream_io_result_t *const result = &async->result;
    sample_stream_io_reader_t *const reader = async->reader;
    const uint32_t source_bytes = result->source_bytes;
    async->source = g_sample_stream_io_page_scratch;
    sample_stream_io_invalidate_read_cache();

    if ((reader == 0) || (sample_stream_io_open_reader(reader, &result->fatfs_ops) == 0U))
    {
        result->load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        return;
    }
    if (result->fatfs_ops != 0U)
    {
        result->file_opens = 1U;
    }
    const FSIZE_t offset = (FSIZE_t)command->stream_info.data_offset
                          + ((FSIZE_t)async->target.start_frame
                             * command->stream_info.info.block_align);
    uint32_t bytes_read = 0U;
    while (bytes_read < source_bytes)
    {
        const FSIZE_t cursor = offset + bytes_read;
        const uint8_t cache_matches =
            (uint8_t)((g_sample_stream_io_cache_bytes != 0U)
                && (sample_audio_key_equal(&g_sample_stream_io_cache_key,
                                           &command->target.key) != 0U)
                && (g_sample_stream_io_cache_registration_epoch
                    == command->stream_info.registration_epoch)
                && (cursor >= g_sample_stream_io_cache_offset)
                && (cursor < (g_sample_stream_io_cache_offset
                              + g_sample_stream_io_cache_bytes)));
        if (cache_matches != 0U)
        {
            const uint32_t cache_position =
                (uint32_t)(cursor - g_sample_stream_io_cache_offset);
            uint32_t available = g_sample_stream_io_cache_bytes - cache_position;
            const uint32_t needed = source_bytes - bytes_read;
            if (available > needed)
            {
                available = needed;
            }
            memcpy(&g_sample_stream_io_page_scratch[bytes_read],
                   &async->scratch[cache_position], available);
            bytes_read += available;
            result->read_cache_hits++;
            continue;
        }

        if (reader->current_file_offset != cursor)
        {
            result->fatfs_ops++;
            result->seeks++;
            if (f_lseek(&reader->file, cursor) != FR_OK)
            {
                result->fatfs_ops += sample_stream_io_close_reader(reader);
                result->load_result = SAMPLE_PAGE_LOAD_SEEK_FAILED;
                return;
            }
        }

        uint32_t request = (uint32_t)g_sample_stream_io_chunk_kib * 1024U;
        const uint64_t source_end = (uint64_t)command->stream_info.data_offset
                                    + ((uint64_t)command->stream_info.total_frames
                                       * command->stream_info.info.block_align);
        if ((uint64_t)cursor >= source_end)
        {
            result->load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
            return;
        }
        const uint64_t remaining_file_bytes = source_end - (uint64_t)cursor;
        if ((uint64_t)request > remaining_file_bytes)
        {
            request = (uint32_t)remaining_file_bytes;
        }
        if (request == 0U)
        {
            result->load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
            return;
        }
        UINT actual = 0U;
        result->fatfs_ops++;
        result->physical_reads++;
        if ((f_read(&reader->file, async->scratch, request, &actual) != FR_OK)
            || (actual != request))
        {
            result->fatfs_ops += sample_stream_io_close_reader(reader);
            result->load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
            return;
        }
        g_sample_stream_io_cache_key = command->target.key;
        result->read_bytes += actual;
        g_sample_stream_io_cache_registration_epoch =
            command->stream_info.registration_epoch;
        g_sample_stream_io_cache_offset = cursor;
        g_sample_stream_io_cache_bytes = actual;
        reader->current_file_offset = cursor + actual;
    }
    result->load_result = SAMPLE_PAGE_LOAD_OK;
}

uint8_t sample_stream_io_begin(const sample_stream_io_command_t *command)
{
    sample_stream_io_async_t *async = 0;
    if (command == 0)
    {
        return 0U;
    }
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_SCRATCH_COUNT; ++i)
    {
        if ((g_sample_stream_io_async[i].active != 0U)
            && (g_sample_stream_io_async[i].state == SAMPLE_STREAM_IO_SCRATCH_DMA))
        {
            return 0U;
        }
    }
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_SCRATCH_COUNT; ++i)
    {
        if (g_sample_stream_io_async[i].active == 0U)
        {
            async = &g_sample_stream_io_async[i];
            memset(async, 0, sizeof(*async));
            async->scratch = g_sample_stream_io_read_scratch[i];
            async->scratch_index = (uint8_t)i;
            break;
        }
    }
    if (async == 0)
    {
        return 0U;
    }
    async->active = 1U;
    async->order = g_sample_stream_io_next_order++;
    async->media_epoch = sd_access_media_epoch();
    async->command = *command;
    async->result.token = command->token;
    async->result.load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
    if ((sample_page_cache_resolve_loading_target(&command->token, &async->target) == 0U)
        || (sample_audio_key_equal(&async->target.key, &command->target.key) == 0U)
        || (async->target.page_index != command->target.page_index)
        || (async->target.start_frame != command->target.start_frame)
        || (async->target.frame_count != command->target.frame_count)
        || (async->target.frames_per_page != command->target.frames_per_page)
        || (async->target.registration_epoch != command->target.registration_epoch)
        || (async->target.page_generation != command->target.page_generation)
        || (async->target.slot_index != command->target.slot_index)
        || (async->target.format != command->target.format)
        || (async->target.stride_floats != command->target.stride_floats))
    {
        async->state = SAMPLE_STREAM_IO_SCRATCH_RAW_READY;
        return 1U;
    }

    async->result.source_bytes = async->target.frame_count
                                 * command->stream_info.info.block_align;
    if ((async->result.source_bytes == 0U)
        || (async->result.source_bytes > SAMPLE_PAGE_BYTES))
    {
        async->state = SAMPLE_STREAM_IO_SCRATCH_RAW_READY;
        return 1U;
    }
    async->reader = sample_stream_io_get_reader(command);
    sample_stream_physical_cursor_t *const cursor = (async->reader != 0)
        ? &async->reader->physical_cursor : &async->local_physical_cursor;
    const uint8_t physical_expected = (uint8_t)(
        sample_stream_safe_metadata_backend(&command->stream_info.stream_safe)
            == SAMPLE_STREAM_BACKEND_PHYSICAL);
    if ((physical_expected != 0U)
        && (sample_stream_backend_physical_busy() != 0U))
    {
        memset(async, 0, sizeof(*async));
        return 0U;
    }
    if(physical_expected != 0U)
    {
        if(sample_stream_backend_physical_begin(
                    &async->physical,
                    &async->command.stream_info,
                    &async->target,
                    cursor,
                    async->scratch,
                    SAMPLE_STREAM_IO_READ_SCRATCH_BYTES,
                    command->deadline_margin_us) != 0U)
        {
            async->physical_active = 1U;
            async->state = SAMPLE_STREAM_IO_SCRATCH_DMA;
            return 1U;
        }
        if(command->stream_info.physical_only != 0U)
        {
            async->result.load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
            async->state = SAMPLE_STREAM_IO_SCRATCH_RAW_READY;
            return 1U;
        }
    }

    sample_stream_io_run_fatfs_fallback(async);
    async->state = SAMPLE_STREAM_IO_SCRATCH_RAW_READY;
    return 1U;
}

uint8_t sample_stream_io_poll(sample_stream_io_result_t *out_result)
{
    sample_stream_io_async_t *async = 0;
    if (out_result == 0)
    {
        return 0U;
    }
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_SCRATCH_COUNT; ++i)
    {
        sample_stream_io_async_t *const candidate = &g_sample_stream_io_async[i];
        if ((candidate->active != 0U)
            && (candidate->state == SAMPLE_STREAM_IO_SCRATCH_RAW_READY)
            && ((async == 0) || (candidate->order < async->order)))
        {
            async = candidate;
        }
    }
    if (async != 0)
    {
        async->state = SAMPLE_STREAM_IO_SCRATCH_DECODING;
        sample_stream_io_decode_async();
        *out_result = async->result;
        memset(async, 0, sizeof(*async));
        return 1U;
    }
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_SCRATCH_COUNT; ++i)
    {
        if ((g_sample_stream_io_async[i].active != 0U)
            && (g_sample_stream_io_async[i].state == SAMPLE_STREAM_IO_SCRATCH_DMA))
        {
            async = &g_sample_stream_io_async[i];
            break;
        }
    }
    if ((async != 0) && (async->physical_active != 0U))
    {
        sample_page_load_result_t physical_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        if (sample_stream_backend_physical_poll(
                &async->physical,
                &physical_result,
                &async->source,
                &async->result.source_bytes,
                &async->result.physical_reads) == 0U)
        {
            return 0U;
        }
        async->physical_active = 0U;
        async->result.load_result = physical_result;
        if (physical_result == SAMPLE_PAGE_LOAD_OK)
        {
            sample_stream_io_invalidate_read_cache();
            async->result.backend = 1U;
            async->result.read_bytes = async->result.source_bytes;
        }
        else if(async->command.stream_info.physical_only == 0U)
        {
            sample_stream_physical_cursor_t *const cursor = (async->reader != 0)
                ? &async->reader->physical_cursor : &async->local_physical_cursor;
            memset(cursor, 0, sizeof(*cursor));
            async->reader = sample_stream_io_get_reader(&async->command);
            sample_stream_io_run_fatfs_fallback(async);
        }
        async->state = SAMPLE_STREAM_IO_SCRATCH_RAW_READY;
        return 0U;
    }
    return 0U;
}

void sample_stream_io_cancel(void)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_SCRATCH_COUNT; ++i)
    {
        if (g_sample_stream_io_async[i].physical_active != 0U)
        {
            sample_stream_backend_physical_cancel(&g_sample_stream_io_async[i].physical);
        }
    }
    memset(g_sample_stream_io_async, 0, sizeof(g_sample_stream_io_async));
}
