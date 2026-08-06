#include "Sampler/sample_stream_io.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_stream_backend_contiguous.h"
#include "Sampler/sample_stream_decoder.h"
#include "Sampler/sample_stream_manager.h"
#include "Storage/memory_layout.h"
#include "ff.h"

#define SAMPLE_STREAM_IO_FILE_OPEN_COOKIE (0x5354524DU)
#define SAMPLE_STREAM_IO_CHUNK_BYTES (4096U)
#define SAMPLE_STREAM_IO_SECTOR_BYTES (512U)
#define SAMPLE_STREAM_IO_SCRATCH_BYTES \
    (SAMPLE_PAGE_BYTES + SAMPLE_STREAM_IO_SECTOR_BYTES)

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
    uint32_t file_open_cookie;
    uint8_t in_use;
    uint8_t file_open;
    FIL file;
} sample_stream_io_reader_t;

SDRAM_STREAM_SERVICE static sample_stream_io_reader_t
    g_sample_stream_io_readers[SAMPLE_STREAM_MAX_ACTIVE];
SDRAM_STREAM_SERVICE static char
    g_sample_stream_io_paths[SAMPLE_STREAM_MAX_ACTIVE][SAMPLE_PAGE_CACHE_PATH_MAX];
SDRAM_STREAM_SCRATCH static uint8_t g_sample_stream_io_scratch[SAMPLE_STREAM_IO_SCRATCH_BYTES];

static char *sample_stream_io_reader_path(sample_stream_io_reader_t *reader)
{
    if (reader == 0)
    {
        return 0;
    }
    const uint32_t index = (uint32_t)(reader - g_sample_stream_io_readers);
    return (index < SAMPLE_STREAM_MAX_ACTIVE) ? g_sample_stream_io_paths[index] : 0;
}

static const char *sample_stream_io_reader_path_const(const sample_stream_io_reader_t *reader)
{
    if (reader == 0)
    {
        return 0;
    }
    const uint32_t index = (uint32_t)(reader - g_sample_stream_io_readers);
    return (index < SAMPLE_STREAM_MAX_ACTIVE) ? g_sample_stream_io_paths[index] : 0;
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
            && (strncmp(path, info->path, SAMPLE_PAGE_CACHE_PATH_MAX) == 0)) ? 1U : 0U;
}

static sample_stream_io_reader_t *sample_stream_io_find_reader(sample_audio_key_t key)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_MAX_ACTIVE; ++i)
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

    for (uint32_t i = 0U; i < SAMPLE_STREAM_MAX_ACTIVE; ++i)
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
}

void sample_stream_io_reset(void)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_MAX_ACTIVE; ++i)
    {
        sample_stream_io_clear_reader(&g_sample_stream_io_readers[i]);
    }
}

void sample_stream_io_release_key(sample_audio_key_t key)
{
    sample_stream_io_reader_t *const reader = sample_stream_io_find_reader(key);
    if (reader != 0)
    {
        sample_stream_io_clear_reader(reader);
    }
}

uint32_t sample_stream_io_active_reader_count(void)
{
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_MAX_ACTIVE; ++i)
    {
        count += (g_sample_stream_io_readers[i].in_use != 0U) ? 1U : 0U;
    }
    return count;
}

void sample_stream_io_execute(const sample_stream_io_command_t *command,
                              sample_stream_io_result_t *out_result)
{
    if (out_result == 0)
    {
        return;
    }
    memset(out_result, 0, sizeof(*out_result));
    out_result->load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
    if (command == 0)
    {
        return;
    }
    out_result->token = command->token;
    const uint32_t source_bytes = command->target.frame_count
                                  * command->stream_info.info.block_align;
    out_result->source_bytes = source_bytes;
    if ((source_bytes == 0U) || (source_bytes > SAMPLE_PAGE_BYTES))
    {
        return;
    }

    const uint8_t *source = g_sample_stream_io_scratch;
    if ((command->stream_info.raw_pcm24 == 0U)
        && (command->stream_info.stream_safe.valid != 0U)
        && (command->stream_info.stream_safe.backend_kind
            == (uint8_t)SAMPLE_STREAM_BACKEND_SAFE_CONTIGUOUS))
    {
        out_result->load_result = sample_stream_backend_contiguous_read_page(
            &command->stream_info, &command->target, g_sample_stream_io_scratch,
            sizeof(g_sample_stream_io_scratch), &source, &out_result->source_bytes);
        if (out_result->load_result == SAMPLE_PAGE_LOAD_OK)
        {
            out_result->backend = 1U;
            out_result->physical_reads = 1U;
        }
    }

    if (out_result->backend == 0U)
    {
        sample_stream_io_reader_t *const reader = sample_stream_io_get_reader(command);
        if ((reader == 0) || (sample_stream_io_open_reader(reader, &out_result->fatfs_ops) == 0U))
        {
            out_result->load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
            return;
        }
        const FSIZE_t offset = (FSIZE_t)command->stream_info.data_offset
                              + ((FSIZE_t)command->target.start_frame
                                 * command->stream_info.info.block_align);
        if (reader->current_file_offset != offset)
        {
            out_result->fatfs_ops++;
            if (f_lseek(&reader->file, offset) != FR_OK)
            {
                out_result->fatfs_ops += sample_stream_io_close_reader(reader);
                out_result->load_result = SAMPLE_PAGE_LOAD_SEEK_FAILED;
                return;
            }
        }

        uint32_t bytes_read = 0U;
        while (bytes_read < source_bytes)
        {
            uint32_t request = source_bytes - bytes_read;
            if (request > SAMPLE_STREAM_IO_CHUNK_BYTES)
            {
                request = SAMPLE_STREAM_IO_CHUNK_BYTES;
            }
            UINT actual = 0U;
            out_result->fatfs_ops++;
            out_result->physical_reads++;
            if ((f_read(&reader->file, &g_sample_stream_io_scratch[bytes_read], request,
                        &actual) != FR_OK) || (actual != request))
            {
                out_result->fatfs_ops += sample_stream_io_close_reader(reader);
                out_result->load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
                return;
            }
            bytes_read += actual;
        }
        reader->current_file_offset = offset + source_bytes;
        out_result->load_result = SAMPLE_PAGE_LOAD_OK;
    }

    if (out_result->load_result == SAMPLE_PAGE_LOAD_OK)
    {
        out_result->load_result = sample_stream_decoder_decode_page(
            &command->stream_info, &command->target, source, out_result->source_bytes);
    }
}
