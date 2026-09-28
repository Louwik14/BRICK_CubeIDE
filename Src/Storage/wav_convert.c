#include "Storage/wav_convert.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_cache.h"
#include "Sampler/sample_stream_fatfs_map.h"
#include "Platform/memory_layout.h"
#include "SD/sd_block_device.h"
#include "SD/sd_scheduler_runtime.h"
#include "Storage/audio_recorder.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/sd_access_gate.h"
#include "Storage/storage_shared_io.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/recorder_file_reservation.h"
#include "Storage/wav_audio_codec.h"
#include "Storage/wav_audio_stream.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#define WAV_CONVERT_TARGET_RATE 48000U
#define WAV_CONVERT_TARGET_CHANNELS 2U
#define WAV_CONVERT_TARGET_BITS 32U
#define WAV_CONVERT_TARGET_BYTES_PER_FRAME 8U
#define WAV_CONVERT_WAV_DATA_OFFSET_BYTES 512U
#define WAV_CONVERT_WAV_JUNK_BYTES 448U
#define WAV_CONVERT_PACK_FRAMES (STORAGE_SHARED_IO_BYTES / WAV_CONVERT_TARGET_BYTES_PER_FRAME)
#define WAV_CONVERT_SOURCE_IO_BYTES (32U * 1024U)
#define WAV_CONVERT_FAST_READ_BYTES (63U * 512U)
#define WAV_CONVERT_FAST_WRITE_BYTES SD_SCHEDULER_BULK_COPY_MAX_DATA_BYTES
#define WAV_CONVERT_PATH_MAX 160U

_Static_assert((WAV_CONVERT_PACK_FRAMES * WAV_CONVERT_TARGET_BYTES_PER_FRAME) == STORAGE_SHARED_IO_BYTES,
               "WAV convert pack chunk must be frame-aligned");
_Static_assert((WAV_CONVERT_SOURCE_IO_BYTES % 512U) == 0U,
               "WAV convert source reads must be sector-aligned");
_Static_assert((STORAGE_SHARED_IO_BYTES % 512U) == 0U,
               "WAV convert output buffer must be sector-aligned");
_Static_assert(sizeof(((wav_audio_stream_t *)0)->io_buf) == WAV_CONVERT_SOURCE_IO_BYTES,
               "WAV stream source buffer contract changed");
_Static_assert(sizeof(wav_convert_bench_t) == 256U, "WAV bench ABI size changed");
_Static_assert(offsetof(wav_convert_bench_t, active_cycles) == 0xA0U,
               "WAV bench ABI cycle offset changed");
_Static_assert(offsetof(wav_convert_bench_t, source_path_hash) == 0xF8U,
               "WAV bench ABI tail offset changed");

typedef enum
{
    WAV_CONVERT_COPY_RAW_FLOAT = 0,
    WAV_CONVERT_COPY_DECODE_48K,
    WAV_CONVERT_COPY_RESAMPLE
} wav_convert_copy_mode_t;

typedef enum
{
    WAV_CONVERT_PHASE_IDLE = 0,
    WAV_CONVERT_PHASE_OPEN,
    WAV_CONVERT_PHASE_WRITE_HEADER,
    WAV_CONVERT_PHASE_COPY,
    WAV_CONVERT_PHASE_SYNC,
    WAV_CONVERT_PHASE_CLOSE,
    WAV_CONVERT_PHASE_VERIFY,
    WAV_CONVERT_PHASE_REPLACE
} wav_convert_phase_t;

typedef enum
{
    WAV_CONVERT_FAST_READ_IDLE = 0,
    WAV_CONVERT_FAST_READ_PENDING,
    WAV_CONVERT_FAST_READ_READY,
    WAV_CONVERT_FAST_READ_FAILED
} wav_convert_fast_read_state_t;

typedef struct
{
    wav_convert_fast_read_state_t state;
    const sample_stream_physical_map_t *map;
    sample_stream_physical_cursor_t *cursor;
    uint8_t *buffer;
    uint64_t file_offset;
    uint32_t requested_bytes;
    uint32_t logical_done;
    uint32_t buffer_sectors;
    uint32_t owner_generation;
    uint32_t active_lba;
    uint32_t active_sectors;
    uint32_t active_logical_bytes;
    uint32_t active_started_cycles;
    uint16_t first_sector_skip;
    uint8_t dma_active;
    uint8_t background_held;
    uint8_t cancel_requested;
} wav_convert_fast_read_t;

typedef enum
{
    WAV_CONVERT_FAST_WRITE_IDLE = 0,
    WAV_CONVERT_FAST_WRITE_PENDING,
    WAV_CONVERT_FAST_WRITE_READY,
    WAV_CONVERT_FAST_WRITE_FAILED
} wav_convert_fast_write_state_t;

typedef struct
{
    wav_convert_fast_write_state_t state;
    const uint8_t *buffer;
    uint64_t file_offset;
    uint32_t requested_bytes;
    uint32_t logical_done;
    uint32_t owner_generation;
    uint32_t active_lba;
    uint32_t active_sectors;
    uint32_t active_logical_bytes;
    uint32_t active_started_cycles;
    uint32_t frame_count;
    uint8_t dma_active;
    uint8_t background_held;
    uint8_t cancel_requested;
} wav_convert_fast_write_t;

typedef struct
{
    wav_convert_state_t state;
    wav_convert_error_t error;
    wav_convert_phase_t phase;
    uint8_t gate_held;
    uint8_t src_open;
    uint8_t dst_open;
    uint8_t temp_created;
    uint8_t bak_created;
    uint8_t cancel_pending;
    uint8_t dst_reserved;
    uint32_t source_frames;
    uint32_t target_frames;
    uint32_t frames_done;
    uint32_t frames_written;
    uint32_t target_data_bytes;
    uint32_t pack_target_frames;
    uint32_t pack_frames;
    uint64_t source_byte_offset;
    wav_convert_copy_mode_t copy_mode;
    char source_path[WAV_CONVERT_PATH_MAX];
    char temp_path[WAV_CONVERT_PATH_MAX];
    char bak_path[WAV_CONVERT_PATH_MAX];
    wav_info_t source_info;
    FIL src;
    FIL dst;
    wav_audio_stream_t stream;
    sample_stream_safe_metadata_t source_metadata;
    sample_stream_physical_cursor_t source_cursor;
    wav_convert_fast_read_t fast_read;
    FF_BRICK_REC_STATE destination_state;
    FF_BRICK_REC_EXTENT destination_fs_extents[RECORDER_FILE_RESERVATION_MAX_EXTENTS];
    sample_stream_physical_extent_t destination_extents[RECORDER_FILE_RESERVATION_MAX_EXTENTS];
    recorder_file_reservation_map_snapshot_t destination_map;
    wav_convert_fast_write_t fast_write;
    ALIGN32 uint8_t wav_header[WAV_CONVERT_WAV_DATA_OFFSET_BYTES];
} wav_convert_ctx_t;

STORAGE_SCRATCH_SDRAM static wav_convert_ctx_t g_wav_convert;
__attribute__((used, externally_visible, aligned(8)))
volatile wav_convert_bench_t g_wav_convert_bench;

static uint32_t g_wav_convert_bench_last_service_end_ms;
static uint8_t g_wav_convert_bench_have_service_end;
static uint32_t g_wav_convert_fast_read_generation = 1U;
static uint32_t g_wav_convert_fast_write_generation = 1U;

static uint32_t wav_convert_bench_cycles(void)
{
    return DWT->CYCCNT;
}

static uint32_t wav_convert_bench_path_hash(const char *path)
{
    uint32_t hash = 2166136261UL;
    while ((path != 0) && (*path != '\0'))
    {
        hash ^= (uint8_t)*path++;
        hash *= 16777619UL;
    }
    return hash;
}

static uint32_t wav_convert_bench_cycles_to_ms(uint64_t cycles)
{
    const uint32_t hz = g_wav_convert_bench.core_clock_hz;
    return (hz != 0U) ? (uint32_t)((cycles * 1000ULL) / hz) : 0U;
}

static void wav_convert_bench_publish_times(void)
{
    g_wav_convert_bench.active_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.active_cycles);
    g_wav_convert_bench.open_parse_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.open_parse_cycles);
    g_wav_convert_bench.read_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.read_cycles);
    g_wav_convert_bench.decode_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.decode_cycles);
    g_wav_convert_bench.src_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.src_cycles);
    g_wav_convert_bench.write_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.write_cycles);
    g_wav_convert_bench.sync_close_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.sync_close_cycles);
    g_wav_convert_bench.verify_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.verify_cycles);
    g_wav_convert_bench.replace_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.replace_cycles);
    const uint64_t named = g_wav_convert_bench.open_parse_cycles
        + g_wav_convert_bench.read_cycles + g_wav_convert_bench.decode_cycles
        + g_wav_convert_bench.src_cycles + g_wav_convert_bench.write_cycles
        + g_wav_convert_bench.sync_close_cycles
        + g_wav_convert_bench.verify_cycles
        + g_wav_convert_bench.replace_cycles;
    g_wav_convert_bench.other_active_ms = wav_convert_bench_cycles_to_ms(
        (g_wav_convert_bench.active_cycles > named)
            ? (g_wav_convert_bench.active_cycles - named) : 0ULL);
}

static void wav_convert_bench_reset(const char *path)
{
    memset((void *)&g_wav_convert_bench, 0, sizeof(g_wav_convert_bench));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_wav_convert_bench.magic = WAV_CONVERT_BENCH_MAGIC;
    g_wav_convert_bench.version = WAV_CONVERT_BENCH_VERSION;
    g_wav_convert_bench.struct_size = sizeof(g_wav_convert_bench);
    g_wav_convert_bench.status = 1U;
    g_wav_convert_bench.core_clock_hz = SystemCoreClock;
    g_wav_convert_bench.min_read_size = UINT32_MAX;
    g_wav_convert_bench.min_write_size = UINT32_MAX;
    g_wav_convert_bench.wall_start_ms = HAL_GetTick();
    g_wav_convert_bench.source_path_hash = wav_convert_bench_path_hash(path);
    g_wav_convert_bench_have_service_end = 0U;
    g_wav_convert_bench_last_service_end_ms = 0U;
}

static void wav_convert_bench_finish(uint32_t status, wav_convert_error_t error)
{
    g_wav_convert_bench.wall_end_ms = HAL_GetTick();
    g_wav_convert_bench.total_ms =
        g_wav_convert_bench.wall_end_ms - g_wav_convert_bench.wall_start_ms;
    g_wav_convert_bench.result_error = (uint32_t)error;
    wav_convert_bench_publish_times();
    g_wav_convert_bench.wall_minus_active_ms =
        (g_wav_convert_bench.total_ms > g_wav_convert_bench.active_ms)
            ? (g_wav_convert_bench.total_ms - g_wav_convert_bench.active_ms) : 0U;
    if (g_wav_convert_bench.read_calls == 0U)
        g_wav_convert_bench.min_read_size = 0U;
    if (g_wav_convert_bench.write_calls == 0U)
        g_wav_convert_bench.min_write_size = 0U;
    g_wav_convert_bench.status = status;
}

static FRESULT wav_convert_bench_read(void *context, FIL *fp, void *buffer,
                                      UINT requested, UINT *actual)
{
    (void)context;
    const uint32_t started = wav_convert_bench_cycles();
    const FRESULT result = f_read(fp, buffer, requested, actual);
    g_wav_convert_bench.read_cycles +=
        (uint32_t)(wav_convert_bench_cycles() - started);
    g_wav_convert_bench.read_calls++;
    g_wav_convert_bench.bytes_read += (actual != 0) ? *actual : 0U;
    if (requested < g_wav_convert_bench.min_read_size)
        g_wav_convert_bench.min_read_size = requested;
    if (requested > g_wav_convert_bench.max_read_size)
        g_wav_convert_bench.max_read_size = requested;
    return result;
}

static FRESULT wav_convert_bench_seek(void *context, FIL *fp, FSIZE_t offset)
{
    (void)context;
    g_wav_convert_bench.seek_calls++;
    return f_lseek(fp, offset);
}

static FRESULT wav_convert_bench_open(FIL *fp, const char *path, BYTE mode)
{
    g_wav_convert_bench.open_calls++;
    return f_open(fp, path, mode);
}

static FRESULT wav_convert_bench_close(FIL *fp)
{
    g_wav_convert_bench.close_calls++;
    return f_close(fp);
}

static void wav_convert_fast_read_reset(wav_convert_fast_read_t *read)
{
    if (read != 0)
    {
        memset(read, 0, sizeof(*read));
        read->state = WAV_CONVERT_FAST_READ_IDLE;
    }
}

static uint8_t wav_convert_fast_read_begin(
    wav_convert_fast_read_t *read,
    const sample_stream_physical_map_t *map,
    sample_stream_physical_cursor_t *cursor,
    uint64_t file_offset,
    uint8_t *buffer,
    uint32_t logical_bytes)
{
    if ((read == 0) || (map == 0) || (cursor == 0) || (buffer == 0)
        || (logical_bytes == 0U) || (logical_bytes > WAV_CONVERT_FAST_READ_BYTES)
        || (sample_stream_physical_map_is_current(map) == 0U))
    {
        return 0U;
    }
    wav_convert_fast_read_reset(read);
    read->map = map;
    read->cursor = cursor;
    read->buffer = buffer;
    read->file_offset = file_offset;
    read->requested_bytes = logical_bytes;
    read->owner_generation = g_wav_convert_fast_read_generation++;
    if (read->owner_generation == 0U)
    {
        read->owner_generation = g_wav_convert_fast_read_generation++;
    }
    read->state = WAV_CONVERT_FAST_READ_PENDING;
    return 1U;
}

static void wav_convert_fast_read_fail(wav_convert_fast_read_t *read)
{
    if (read == 0) return;
    if (read->background_held != 0U)
    {
        sd_scheduler_runtime_background_end();
        read->background_held = 0U;
    }
    read->dma_active = 0U;
    read->state = WAV_CONVERT_FAST_READ_FAILED;
}

static wav_convert_fast_read_state_t wav_convert_fast_read_service(
    wav_convert_fast_read_t *read)
{
    if ((read == 0) || (read->state != WAV_CONVERT_FAST_READ_PENDING))
    {
        return (read != 0) ? read->state : WAV_CONVERT_FAST_READ_FAILED;
    }

    if (read->dma_active != 0U)
    {
        sd_block_device_async_completion_t completion;
        if (sd_block_device_async_take_completion(&completion) == 0U)
        {
            return read->state;
        }
        sd_scheduler_runtime_background_end();
        read->background_held = 0U;
        read->dma_active = 0U;
        g_wav_convert_bench.read_cycles +=
            (uint32_t)(wav_convert_bench_cycles() - read->active_started_cycles);
        g_wav_convert_bench.read_calls++;
        g_wav_convert_bench.bytes_read += read->active_logical_bytes;
        if (read->active_logical_bytes < g_wav_convert_bench.min_read_size)
            g_wav_convert_bench.min_read_size = read->active_logical_bytes;
        if (read->active_logical_bytes > g_wav_convert_bench.max_read_size)
            g_wav_convert_bench.max_read_size = read->active_logical_bytes;
        if ((completion.result != SD_BLOCK_DEVICE_OK)
            || (completion.operation != SD_BLOCK_DEVICE_OPERATION_READ)
            || (completion.lba != read->active_lba)
            || (completion.sector_count != read->active_sectors)
            || (completion.dst != &read->buffer[
                    read->buffer_sectors * SD_SCHEDULER_SECTOR_BYTES])
            || (completion.owner_generation != read->owner_generation)
            || (completion.media_epoch != read->map->media_epoch)
            || (read->cancel_requested != 0U))
        {
            wav_convert_fast_read_fail(read);
            return read->state;
        }
        read->logical_done += read->active_logical_bytes;
        read->buffer_sectors += read->active_sectors;
        if (read->logical_done >= read->requested_bytes)
        {
            read->state = WAV_CONVERT_FAST_READ_READY;
            return read->state;
        }
    }

    sample_stream_physical_span_t span;
    const uint32_t remaining = read->requested_bytes - read->logical_done;
    if (sample_stream_physical_map_resolve(
            read->map, read->file_offset + read->logical_done, remaining,
            read->cursor, &span) == 0U)
    {
        wav_convert_fast_read_fail(read);
        return read->state;
    }
    const uint64_t buffer_end = ((uint64_t)read->buffer_sectors
        + span.sector_count) * SD_SCHEDULER_SECTOR_BYTES;
    if ((span.sector_count == 0U)
        || (span.sector_count > (SD_SCHEDULER_BULK_COPY_MAX_DATA_BYTES
                                 / SD_SCHEDULER_SECTOR_BYTES))
        || (buffer_end > WAV_CONVERT_SOURCE_IO_BYTES)
        || ((read->logical_done != 0U) && (span.first_sector_skip != 0U)))
    {
        wav_convert_fast_read_fail(read);
        return read->state;
    }
    const sd_scheduler_background_request_t request = {
        .byte_count = span.sector_count * SD_SCHEDULER_SECTOR_BYTES,
        .media_epoch = read->map->media_epoch,
        .kind = SD_SCHEDULER_BACKGROUND_DATA,
    };
    const sd_scheduler_background_admission_t admission =
        sd_scheduler_runtime_background_try_begin(&request);
    if (admission == SD_SCHEDULER_BACKGROUND_NOT_NOW)
    {
        return read->state;
    }
    if (admission != SD_SCHEDULER_BACKGROUND_GO)
    {
        wav_convert_fast_read_fail(read);
        return read->state;
    }
    read->background_held = 1U;
    uint8_t *const destination = &read->buffer[
        read->buffer_sectors * SD_SCHEDULER_SECTOR_BYTES];
    const sd_block_device_result_t submit = sd_block_device_async_read_submit(
        span.lba, span.sector_count, destination, read->owner_generation);
    if ((submit == SD_BLOCK_DEVICE_BUSY) || (submit == SD_BLOCK_DEVICE_QUEUE_FULL))
    {
        sd_scheduler_runtime_background_end();
        read->background_held = 0U;
        return read->state;
    }
    if (submit != SD_BLOCK_DEVICE_OK)
    {
        wav_convert_fast_read_fail(read);
        return read->state;
    }
    if (read->logical_done == 0U)
    {
        read->first_sector_skip = span.first_sector_skip;
    }
    read->active_lba = span.lba;
    read->active_sectors = span.sector_count;
    read->active_logical_bytes = span.logical_bytes;
    read->active_started_cycles = wav_convert_bench_cycles();
    read->dma_active = 1U;
    return read->state;
}

static void wav_convert_fast_read_cancel(wav_convert_fast_read_t *read)
{
    if ((read == 0) || (read->state != WAV_CONVERT_FAST_READ_PENDING)) return;
    read->cancel_requested = 1U;
    if (read->dma_active != 0U)
    {
        (void)sd_block_device_async_abort_generation(read->owner_generation);
    }
    else
    {
        wav_convert_fast_read_fail(read);
    }
}

static void wav_convert_fast_write_reset(wav_convert_fast_write_t *write)
{
    if (write != 0)
    {
        memset(write, 0, sizeof(*write));
        write->state = WAV_CONVERT_FAST_WRITE_IDLE;
    }
}

static uint8_t wav_convert_fast_write_begin(
    wav_convert_fast_write_t *write,
    uint64_t file_offset,
    const uint8_t *buffer,
    uint32_t logical_bytes,
    uint32_t frame_count)
{
    if ((write == 0) || (buffer == 0) || (logical_bytes == 0U)
        || ((((uintptr_t)buffer) & 31U) != 0U)
        || ((file_offset & (SD_SCHEDULER_SECTOR_BYTES - 1U)) != 0U)
        || (file_offset + logical_bytes
            > g_wav_convert.destination_map.reserved_file_bytes))
    {
        return 0U;
    }
    wav_convert_fast_write_reset(write);
    write->buffer = buffer;
    write->file_offset = file_offset;
    write->requested_bytes = logical_bytes;
    write->frame_count = frame_count;
    write->owner_generation = g_wav_convert_fast_write_generation++;
    if (write->owner_generation == 0U)
    {
        write->owner_generation = g_wav_convert_fast_write_generation++;
    }
    write->state = WAV_CONVERT_FAST_WRITE_PENDING;
    return 1U;
}

static void wav_convert_fast_write_fail(wav_convert_fast_write_t *write)
{
    if (write == 0) return;
    if (write->background_held != 0U)
    {
        sd_scheduler_runtime_background_end();
        write->background_held = 0U;
    }
    write->dma_active = 0U;
    write->state = WAV_CONVERT_FAST_WRITE_FAILED;
}

static wav_convert_fast_write_state_t wav_convert_fast_write_service(
    wav_convert_fast_write_t *write)
{
    if ((write == 0) || (write->state != WAV_CONVERT_FAST_WRITE_PENDING))
    {
        return (write != 0) ? write->state : WAV_CONVERT_FAST_WRITE_FAILED;
    }

    if (write->dma_active != 0U)
    {
        sd_block_device_async_completion_t completion;
        if (sd_block_device_async_take_completion(&completion) == 0U)
        {
            return write->state;
        }
        sd_scheduler_runtime_background_end();
        write->background_held = 0U;
        write->dma_active = 0U;
        g_wav_convert_bench.write_cycles +=
            (uint32_t)(wav_convert_bench_cycles() - write->active_started_cycles);
        g_wav_convert_bench.write_calls++;
        g_wav_convert_bench.bytes_written += write->active_logical_bytes;
        if (write->active_logical_bytes < g_wav_convert_bench.min_write_size)
            g_wav_convert_bench.min_write_size = write->active_logical_bytes;
        if (write->active_logical_bytes > g_wav_convert_bench.max_write_size)
            g_wav_convert_bench.max_write_size = write->active_logical_bytes;
        if ((completion.result != SD_BLOCK_DEVICE_OK)
            || (completion.operation != SD_BLOCK_DEVICE_OPERATION_WRITE)
            || (completion.lba != write->active_lba)
            || (completion.sector_count != write->active_sectors)
            || (completion.src != &write->buffer[write->logical_done])
            || (completion.owner_generation != write->owner_generation)
            || (completion.media_epoch != g_wav_convert.destination_map.media_epoch)
            || (write->cancel_requested != 0U))
        {
            wav_convert_fast_write_fail(write);
            return write->state;
        }
        write->logical_done += write->active_logical_bytes;
        if (write->logical_done >= write->requested_bytes)
        {
            write->state = WAV_CONVERT_FAST_WRITE_READY;
            return write->state;
        }
    }

    sample_stream_physical_span_t span;
    uint32_t remaining = write->requested_bytes - write->logical_done;
    if (remaining > WAV_CONVERT_FAST_WRITE_BYTES)
        remaining = WAV_CONVERT_FAST_WRITE_BYTES;
    if (recorder_file_reservation_map_resolve(
            &g_wav_convert.destination_map,
            write->file_offset + write->logical_done,
            remaining, &span) == 0U)
    {
        wav_convert_fast_write_fail(write);
        return write->state;
    }
    if ((span.first_sector_skip != 0U) || (span.sector_count == 0U)
        || (span.sector_count > SD_BLOCK_DEVICE_MAX_SECTORS_PER_TRANSFER)
        || (span.sector_count * SD_SCHEDULER_SECTOR_BYTES
            > WAV_CONVERT_FAST_WRITE_BYTES))
    {
        wav_convert_fast_write_fail(write);
        return write->state;
    }
    const sd_scheduler_background_request_t request = {
        .byte_count = span.sector_count * SD_SCHEDULER_SECTOR_BYTES,
        .media_epoch = g_wav_convert.destination_map.media_epoch,
        .kind = SD_SCHEDULER_BACKGROUND_DATA,
    };
    const sd_scheduler_background_admission_t admission =
        sd_scheduler_runtime_background_try_begin(&request);
    if (admission == SD_SCHEDULER_BACKGROUND_NOT_NOW)
    {
        return write->state;
    }
    if (admission != SD_SCHEDULER_BACKGROUND_GO)
    {
        wav_convert_fast_write_fail(write);
        return write->state;
    }
    write->background_held = 1U;
    const uint8_t *const source = &write->buffer[write->logical_done];
    const sd_block_device_result_t submit = sd_block_device_async_write_submit(
        span.lba, span.sector_count, source, write->owner_generation);
    if ((submit == SD_BLOCK_DEVICE_BUSY) || (submit == SD_BLOCK_DEVICE_QUEUE_FULL))
    {
        sd_scheduler_runtime_background_end();
        write->background_held = 0U;
        return write->state;
    }
    if (submit != SD_BLOCK_DEVICE_OK)
    {
        wav_convert_fast_write_fail(write);
        return write->state;
    }
    write->active_lba = span.lba;
    write->active_sectors = span.sector_count;
    write->active_logical_bytes = span.logical_bytes;
    write->active_started_cycles = wav_convert_bench_cycles();
    write->dma_active = 1U;
    return write->state;
}

static void wav_convert_fast_write_cancel(wav_convert_fast_write_t *write)
{
    if ((write == 0) || (write->state != WAV_CONVERT_FAST_WRITE_PENDING)) return;
    write->cancel_requested = 1U;
    if (write->dma_active != 0U)
    {
        (void)sd_block_device_async_abort_generation(write->owner_generation);
    }
    else
    {
        wav_convert_fast_write_fail(write);
    }
}

void wav_convert_init(void)
{
    memset(&g_wav_convert, 0, sizeof(g_wav_convert));
    g_wav_convert.state = WAV_CONVERT_STATE_IDLE;
    g_wav_convert.phase = WAV_CONVERT_PHASE_IDLE;
    memset((void *)&g_wav_convert_bench, 0, sizeof(g_wav_convert_bench));
    g_wav_convert_bench.magic = WAV_CONVERT_BENCH_MAGIC;
    g_wav_convert_bench.version = WAV_CONVERT_BENCH_VERSION;
    g_wav_convert_bench.struct_size = sizeof(g_wav_convert_bench);
}

static void wav_convert_write_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void wav_convert_write_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFUL);
    dst[1] = (uint8_t)((value >> 8) & 0xFFUL);
    dst[2] = (uint8_t)((value >> 16) & 0xFFUL);
    dst[3] = (uint8_t)((value >> 24) & 0xFFUL);
}

static uint16_t wav_convert_read_le16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t wav_convert_read_le32(const uint8_t *src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8)
           | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static void wav_convert_build_wav_header(uint8_t *header,
                                         uint32_t data_bytes,
                                         uint32_t frame_count)
{
    const uint16_t block_align = WAV_CONVERT_TARGET_BYTES_PER_FRAME;
    const uint32_t byte_rate = WAV_CONVERT_TARGET_RATE * (uint32_t)block_align;

    memset(header, 0, WAV_CONVERT_WAV_DATA_OFFSET_BYTES);
    memcpy(&header[0], "RIFF", 4U);
    wav_convert_write_le32(&header[4], (WAV_CONVERT_WAV_DATA_OFFSET_BYTES - 8U) + data_bytes);
    memcpy(&header[8], "WAVE", 4U);
    memcpy(&header[12], "fmt ", 4U);
    wav_convert_write_le32(&header[16], 16U);
    wav_convert_write_le16(&header[20], 3U);
    wav_convert_write_le16(&header[22], WAV_CONVERT_TARGET_CHANNELS);
    wav_convert_write_le32(&header[24], WAV_CONVERT_TARGET_RATE);
    wav_convert_write_le32(&header[28], byte_rate);
    wav_convert_write_le16(&header[32], block_align);
    wav_convert_write_le16(&header[34], WAV_CONVERT_TARGET_BITS);
    memcpy(&header[36], "fact", 4U);
    wav_convert_write_le32(&header[40], 4U);
    wav_convert_write_le32(&header[44], frame_count);
    memcpy(&header[48], "JUNK", 4U);
    wav_convert_write_le32(&header[52], WAV_CONVERT_WAV_JUNK_BYTES);
    memcpy(&header[504], "data", 4U);
    wav_convert_write_le32(&header[508], data_bytes);
}

static uint8_t wav_convert_copy_path(char *dst, const char *src)
{
    if ((dst == 0) || (src == 0) || (src[0] == '\0'))
    {
        return 0U;
    }

    uint32_t i = 0U;
    while ((i + 1U) < WAV_CONVERT_PATH_MAX)
    {
        dst[i] = src[i];
        if (src[i] == '\0')
        {
            return 1U;
        }
        i++;
    }

    dst[0] = '\0';
    return 0U;
}

static uint8_t wav_convert_make_side_paths(const char *path, char *temp_path, char *bak_path)
{
    if ((path == 0) || (temp_path == 0) || (bak_path == 0))
    {
        return 0U;
    }
    if ((wav_convert_copy_path(temp_path, path) == 0U)
        || (wav_convert_copy_path(bak_path, path) == 0U))
    {
        return 0U;
    }

    const uint32_t len = (uint32_t)strlen(path);
    if ((len < 4U) || (path[len - 4U] != '.'))
    {
        return 0U;
    }

    temp_path[len - 3U] = 'B';
    temp_path[len - 2U] = '6';
    temp_path[len - 1U] = 'T';
    bak_path[len - 3U] = 'B';
    bak_path[len - 2U] = '6';
    bak_path[len - 1U] = 'B';
    return 1U;
}

static uint8_t wav_convert_format_convertible(const wav_info_t *info)
{
    if (info == 0)
    {
        return 0U;
    }

    return wav_parser_format_supported(info);
}

static uint8_t wav_convert_format_already_target(const wav_info_t *info)
{
    if (info == 0)
    {
        return 0U;
    }

    return wav_parser_is_canonical_brick_float(info);
}

static uint8_t wav_convert_parse_path_locked(const char *path, wav_info_t *out_info)
{
    FIL fp;
    FRESULT fr = f_open(&fp, path, FA_READ);
    if (fr != FR_OK)
    {
        return 0U;
    }

    const uint8_t ok = (wav_parser_parse_info(&fp, out_info) != 0) ? 1U : 0U;
    (void)f_close(&fp);
    return ok;
}

wav_convert_path_status_t wav_convert_path_canonical_status(
    const char *path, wav_info_t *out_info)
{
    wav_info_t info;
    char temp_path[WAV_CONVERT_PATH_MAX];
    char bak_path[WAV_CONVERT_PATH_MAX];
    if ((path == 0) || (path[0] == '\0'))
    {
        return WAV_CONVERT_PATH_INVALID;
    }

    if (audio_recorder_is_active() != 0U)
    {
        return WAV_CONVERT_PATH_BUSY;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_WAV_CONVERT) == 0U)
    {
        return WAV_CONVERT_PATH_BUSY;
    }

    wav_convert_path_status_t status = WAV_CONVERT_PATH_INVALID;
    if ((sd_access_fs_mount_if_needed() != 0U)
        && (wav_convert_make_side_paths(path, temp_path, bak_path) != 0U)
        && (persistent_fatfs_recover_replace(path, temp_path, bak_path) == FR_OK)
        && (wav_convert_parse_path_locked(path, &info) != 0U)
        && (wav_convert_format_convertible(&info) != 0U))
    {
        if (out_info != 0)
        {
            *out_info = info;
        }
        status = (wav_convert_format_already_target(&info) != 0U)
                     ? WAV_CONVERT_PATH_CANONICAL
                     : WAV_CONVERT_PATH_NEEDS_CANONICAL;
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_WAV_CONVERT);
    return status;
}

uint8_t wav_convert_path_needs_canonical(const char *path, wav_info_t *out_info)
{
    return (wav_convert_path_canonical_status(path, out_info)
            == WAV_CONVERT_PATH_NEEDS_CANONICAL) ? 1U : 0U;
}

static void wav_convert_close_files(void)
{
    if (g_wav_convert.src_open != 0U)
    {
        (void)wav_convert_bench_close(&g_wav_convert.src);
        g_wav_convert.src_open = 0U;
    }
    if (g_wav_convert.dst_open != 0U)
    {
        if (g_wav_convert.dst_reserved != 0U)
        {
            g_wav_convert_bench.close_calls++;
            if (f_brick_rec_close_synced(&g_wav_convert.dst) != FR_OK)
            {
                (void)f_close(&g_wav_convert.dst);
            }
        }
        else
        {
            (void)wav_convert_bench_close(&g_wav_convert.dst);
        }
        g_wav_convert.dst_open = 0U;
    }
}

static FRESULT wav_convert_reserve_destination(void)
{
    FATFS *const fs = g_wav_convert.dst.obj.fs;
    if ((fs == 0) || (g_wav_convert.dst.obj.objsize != 0U)) return FR_INT_ERR;

    memset(&g_wav_convert.destination_state, 0,
           sizeof(g_wav_convert.destination_state));
    g_wav_convert.destination_state.cluster_bytes =
        (DWORD)fs->csize * SD_SCHEDULER_SECTOR_BYTES;
    g_wav_convert.destination_state.fs_type = fs->fs_type;
    g_wav_convert.destination_state.chain_status = g_wav_convert.dst.obj.stat;

    UINT extent_count = 0U;
    const FSIZE_t exact_bytes = (FSIZE_t)WAV_CONVERT_WAV_DATA_OFFSET_BYTES
        + g_wav_convert.target_data_bytes;
    const FRESULT fr = f_brick_rec_reserve(
        &g_wav_convert.dst, exact_bytes, &g_wav_convert.destination_state,
        g_wav_convert.destination_fs_extents,
        RECORDER_FILE_RESERVATION_MAX_EXTENTS, &extent_count, 0);
    if (fr != FR_OK) return fr;
    if (g_wav_convert.destination_state.reserved_bytes < exact_bytes)
    {
        return (extent_count >= RECORDER_FILE_RESERVATION_MAX_EXTENTS)
            ? FR_NOT_ENOUGH_CORE : FR_DENIED;
    }
    if ((extent_count == 0U)
        || (extent_count > RECORDER_FILE_RESERVATION_MAX_EXTENTS))
    {
        return FR_NOT_ENOUGH_CORE;
    }
    for (UINT i = 0U; i < extent_count; ++i)
    {
        const FF_BRICK_REC_EXTENT *const src =
            &g_wav_convert.destination_fs_extents[i];
        if ((src->file_sector_start > UINT32_MAX)
            || (src->sector_count == 0U))
        {
            return FR_INT_ERR;
        }
        g_wav_convert.destination_extents[i].file_sector_start =
            (uint32_t)src->file_sector_start;
        g_wav_convert.destination_extents[i].lba_start = src->lba_start;
        g_wav_convert.destination_extents[i].sector_count = src->sector_count;
    }
    g_wav_convert.destination_map.extents = g_wav_convert.destination_extents;
    g_wav_convert.destination_map.reserved_file_bytes =
        g_wav_convert.destination_state.reserved_bytes;
    g_wav_convert.destination_map.valid_file_bytes = 0U;
    g_wav_convert.destination_map.media_epoch = sd_access_media_epoch();
    g_wav_convert.destination_map.extent_count = (uint16_t)extent_count;
    g_wav_convert.destination_map.sector_size = SD_SCHEDULER_SECTOR_BYTES;
    g_wav_convert.dst_reserved = 1U;
    wav_convert_fast_write_reset(&g_wav_convert.fast_write);
    return FR_OK;
}

static void wav_convert_release_gate(void)
{
    if (g_wav_convert.gate_held != 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_WAV_CONVERT);
        g_wav_convert.gate_held = 0U;
    }
}

static uint8_t wav_convert_try_hold_gate(void)
{
    if (g_wav_convert.gate_held != 0U) return 1U;
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_WAV_CONVERT) == 0U)
    {
        return 0U;
    }
    g_wav_convert.gate_held = 1U;
    return 1U;
}

static void wav_convert_fail(wav_convert_error_t error)
{
    sample_stream_physical_map_release(
        &g_wav_convert.source_metadata.physical_map);
    wav_convert_close_files();
    if ((g_wav_convert.temp_created != 0U) && (g_wav_convert.bak_created == 0U))
    {
        (void)f_unlink(g_wav_convert.temp_path);
        g_wav_convert.temp_created = 0U;
    }
    wav_convert_release_gate();
    g_wav_convert.error = error;
    g_wav_convert.state = WAV_CONVERT_STATE_FAILED;
    if (g_wav_convert_bench.status == 1U)
        wav_convert_bench_finish(3U, error);
}

static uint8_t wav_convert_start_internal(const char *path,
                                          uint8_t allow_project_replacement)
{
    if ((allow_project_replacement == 0U)
        && (project_replacement_is_active() != 0U)) return 0U;
    if ((path == 0) || (path[0] == '\0'))
    {
        return 0U;
    }
    if ((g_wav_convert.state == WAV_CONVERT_STATE_ACTIVE)
        || (audio_recorder_is_active() != 0U)
        || (sample_cache_has_pending_sd_work() != 0U))
    {
        return 0U;
    }

    memset(&g_wav_convert, 0, sizeof(g_wav_convert));
    if ((wav_convert_copy_path(g_wav_convert.source_path, path) == 0U)
        || (wav_convert_make_side_paths(path,
                                        g_wav_convert.temp_path,
                                        g_wav_convert.bak_path) == 0U))
    {
        g_wav_convert.state = WAV_CONVERT_STATE_FAILED;
        g_wav_convert.error = WAV_CONVERT_ERROR_INVALID_ARG;
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_WAV_CONVERT) == 0U)
    {
        g_wav_convert.state = WAV_CONVERT_STATE_FAILED;
        g_wav_convert.error = WAV_CONVERT_ERROR_BUSY;
        return 0U;
    }
    g_wav_convert.gate_held = 1U;

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_MOUNT_FAIL);
        return 0U;
    }

    if (persistent_fatfs_recover_replace(g_wav_convert.source_path,
                                         g_wav_convert.temp_path,
                                         g_wav_convert.bak_path) != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_REPLACE_FAIL);
        return 0U;
    }

    wav_convert_bench_reset(g_wav_convert.source_path);
    g_wav_convert.state = WAV_CONVERT_STATE_ACTIVE;
    g_wav_convert.error = WAV_CONVERT_ERROR_NONE;
    g_wav_convert.phase = WAV_CONVERT_PHASE_OPEN;
    return 1U;
}

uint8_t wav_convert_start_destructive_canonical(const char *path)
{
    return wav_convert_start_internal(path, 0U);
}

uint8_t wav_convert_start_destructive_canonical_project(const char *path)
{
    return wav_convert_start_internal(path, 1U);
}

static uint8_t wav_convert_open_phase(void)
{
    FRESULT fr = wav_convert_bench_open(&g_wav_convert.src,
                                        g_wav_convert.source_path, FA_READ);
    if (fr != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_OPEN_FAIL);
        return 0U;
    }
    g_wav_convert.src_open = 1U;

    const wav_parser_io_hooks_t parser_hooks = {
        .read_fn = wav_convert_bench_read,
        .seek_fn = wav_convert_bench_seek,
        .context = 0,
    };
    if ((wav_parser_parse_info_with_io(&g_wav_convert.src,
                                       &g_wav_convert.source_info,
                                       &parser_hooks) == 0)
        || (wav_convert_format_convertible(&g_wav_convert.source_info) == 0U)
        || (wav_convert_format_already_target(&g_wav_convert.source_info) != 0U))
    {
        wav_convert_fail(WAV_CONVERT_ERROR_UNSUPPORTED);
        return 0U;
    }

    g_wav_convert.source_frames =
        g_wav_convert.source_info.data_size / g_wav_convert.source_info.block_align;
    const uint64_t target_frames =
        ((uint64_t)g_wav_convert.source_frames * WAV_CONVERT_TARGET_RATE
         + (uint64_t)g_wav_convert.source_info.sample_rate - 1ULL)
        / (uint64_t)g_wav_convert.source_info.sample_rate;
    if ((target_frames == 0ULL) || (target_frames > UINT32_MAX)
        || ((target_frames * WAV_CONVERT_TARGET_BYTES_PER_FRAME)
            > (uint64_t)(UINT32_MAX - WAV_CONVERT_WAV_DATA_OFFSET_BYTES)))
    {
        wav_convert_fail(WAV_CONVERT_ERROR_UNSUPPORTED);
        return 0U;
    }
    g_wav_convert.target_frames = (uint32_t)target_frames;
    g_wav_convert.target_data_bytes =
        g_wav_convert.target_frames * WAV_CONVERT_TARGET_BYTES_PER_FRAME;

    sample_stream_safe_metadata_init_fatfs(
        (sample_audio_key_t){0}, &g_wav_convert.source_info,
        g_wav_convert.source_frames, g_wav_convert.source_info.data_offset,
        &g_wav_convert.source_metadata);
    if (sample_stream_fatfs_map_build_from_file(
            &g_wav_convert.src, &g_wav_convert.source_metadata) == 0U)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_READ_FAIL);
        return 0U;
    }
    memset(&g_wav_convert.source_cursor, 0,
           sizeof(g_wav_convert.source_cursor));
    wav_convert_fast_read_reset(&g_wav_convert.fast_read);
    g_wav_convert.source_byte_offset = g_wav_convert.source_info.data_offset;

    FILINFO bak_info;
    fr = f_stat(g_wav_convert.bak_path, &bak_info);
    if (fr == FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_REPLACE_FAIL);
        return 0U;
    }

    fr = wav_convert_bench_open(&g_wav_convert.dst,
                                g_wav_convert.temp_path,
                                FA_CREATE_ALWAYS | FA_WRITE | FA_READ);
    if (fr != FR_OK)
    {
        wav_convert_fail((fr == FR_DENIED) ? WAV_CONVERT_ERROR_NO_SPACE : WAV_CONVERT_ERROR_OPEN_FAIL);
        return 0U;
    }
    g_wav_convert.dst_open = 1U;
    g_wav_convert.temp_created = 1U;
    fr = wav_convert_reserve_destination();
    if (fr != FR_OK)
    {
        wav_convert_fail((fr == FR_DENIED)
            ? WAV_CONVERT_ERROR_NO_SPACE : WAV_CONVERT_ERROR_WRITE_FAIL);
        return 0U;
    }

    if ((g_wav_convert.source_info.encoding == WAV_SAMPLE_ENCODING_IEEE_FLOAT)
        && (g_wav_convert.source_info.channels == WAV_CONVERT_TARGET_CHANNELS)
        && (g_wav_convert.source_info.bits_per_sample == WAV_CONVERT_TARGET_BITS)
        && (g_wav_convert.source_info.sample_rate == WAV_CONVERT_TARGET_RATE))
    {
        g_wav_convert.copy_mode = WAV_CONVERT_COPY_RAW_FLOAT;
    }
    else if (g_wav_convert.source_info.sample_rate == WAV_CONVERT_TARGET_RATE)
    {
        g_wav_convert.copy_mode = WAV_CONVERT_COPY_DECODE_48K;
    }
    else
    {
        g_wav_convert.copy_mode = WAV_CONVERT_COPY_RESAMPLE;
    }

    if (g_wav_convert.copy_mode == WAV_CONVERT_COPY_RESAMPLE)
    {
        wav_audio_stream_init(&g_wav_convert.stream,
                              &g_wav_convert.src,
                              &g_wav_convert.source_info,
                              WAV_CONVERT_TARGET_RATE);
        wav_audio_stream_enable_external_input(&g_wav_convert.stream);
    }
    if ((g_wav_convert.copy_mode == WAV_CONVERT_COPY_RESAMPLE)
        && (wav_audio_stream_start(&g_wav_convert.stream,
                g_wav_convert.source_info.data_offset) == 0U))
    {
        wav_convert_fail(WAV_CONVERT_ERROR_READ_FAIL);
        return 0U;
    }

    g_wav_convert.phase = WAV_CONVERT_PHASE_WRITE_HEADER;
    g_wav_convert_bench.source_encoding =
        (uint32_t)g_wav_convert.source_info.encoding;
    g_wav_convert_bench.source_bits_per_sample =
        g_wav_convert.source_info.bits_per_sample;
    g_wav_convert_bench.source_channels = g_wav_convert.source_info.channels;
    g_wav_convert_bench.source_sample_rate =
        g_wav_convert.source_info.sample_rate;
    g_wav_convert_bench.source_frames = g_wav_convert.source_frames;
    g_wav_convert_bench.copy_mode = (uint32_t)g_wav_convert.copy_mode;
    g_wav_convert_bench.has_src =
        (g_wav_convert.copy_mode == WAV_CONVERT_COPY_RESAMPLE) ? 1U : 0U;
    g_wav_convert_bench.target_frames = g_wav_convert.target_frames;
    return 1U;
}

static uint8_t wav_convert_write_header_phase(void)
{
    wav_convert_fast_write_t *const write = &g_wav_convert.fast_write;
    if (write->state == WAV_CONVERT_FAST_WRITE_IDLE)
    {
        wav_convert_build_wav_header(g_wav_convert.wav_header,
            g_wav_convert.target_data_bytes, g_wav_convert.target_frames);
        wav_convert_release_gate();
        if (wav_convert_fast_write_begin(write, 0U,
                g_wav_convert.wav_header, sizeof(g_wav_convert.wav_header),
                0U) == 0U)
        {
            write->state = WAV_CONVERT_FAST_WRITE_FAILED;
        }
    }
    const wav_convert_fast_write_state_t state =
        wav_convert_fast_write_service(write);
    if (state == WAV_CONVERT_FAST_WRITE_PENDING) return 0U;
    if (wav_convert_try_hold_gate() == 0U) return 0U;
    if (state != WAV_CONVERT_FAST_WRITE_READY)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_WRITE_FAIL);
        return 0U;
    }
    wav_convert_fast_write_reset(write);
    g_wav_convert.phase = WAV_CONVERT_PHASE_COPY;
    return 1U;
}

static uint8_t wav_convert_write_output(uint32_t frame_count)
{
    const uint32_t bytes = frame_count * WAV_CONVERT_TARGET_BYTES_PER_FRAME;
    const uint32_t dma_bytes = (bytes + SD_SCHEDULER_SECTOR_BYTES - 1U)
        & ~(SD_SCHEDULER_SECTOR_BYTES - 1U);
    if ((bytes == 0U) || (dma_bytes > STORAGE_SHARED_IO_BYTES))
    {
        wav_convert_fail(WAV_CONVERT_ERROR_WRITE_FAIL);
        return 0U;
    }
    if (dma_bytes > bytes)
    {
        memset(&g_storage_shared_io[bytes], 0, dma_bytes - bytes);
    }
    const uint64_t file_offset = WAV_CONVERT_WAV_DATA_OFFSET_BYTES
        + (uint64_t)g_wav_convert.frames_written
            * WAV_CONVERT_TARGET_BYTES_PER_FRAME;
    wav_convert_release_gate();
    if (wav_convert_fast_write_begin(&g_wav_convert.fast_write,
            file_offset, g_storage_shared_io, bytes, frame_count) == 0U)
    {
        g_wav_convert.fast_write.state = WAV_CONVERT_FAST_WRITE_FAILED;
        return 0U;
    }
    (void)wav_convert_fast_write_service(&g_wav_convert.fast_write);
    return 0U;
}

static uint8_t wav_convert_fast_read_consume(uint8_t **source,
                                             uint32_t *source_bytes)
{
    wav_convert_fast_read_t *const read = &g_wav_convert.fast_read;
    const wav_convert_fast_read_state_t state =
        wav_convert_fast_read_service(read);
    if (state == WAV_CONVERT_FAST_READ_FAILED)
    {
        if (wav_convert_try_hold_gate() == 0U) return 0U;
        wav_convert_fail(WAV_CONVERT_ERROR_READ_FAIL);
        return 0U;
    }
    if (state != WAV_CONVERT_FAST_READ_READY)
    {
        return 0U;
    }
    if (wav_convert_try_hold_gate() == 0U)
    {
        return 0U;
    }
    *source = &read->buffer[read->first_sector_skip];
    *source_bytes = read->requested_bytes;
    return 1U;
}

static uint8_t wav_convert_fast_read_start(uint32_t logical_bytes)
{
    wav_convert_release_gate();
    const uint8_t started = wav_convert_fast_read_begin(
        &g_wav_convert.fast_read,
        &g_wav_convert.source_metadata.physical_map,
        &g_wav_convert.source_cursor,
        g_wav_convert.source_byte_offset,
        g_wav_convert.stream.io_buf,
        logical_bytes);
    if (started == 0U)
    {
        g_wav_convert.fast_read.state = WAV_CONVERT_FAST_READ_FAILED;
    }
    else
    {
        (void)wav_convert_fast_read_service(&g_wav_convert.fast_read);
    }
    return started;
}

static uint8_t wav_convert_copy_48k_service(void)
{
    uint8_t *source = 0;
    uint32_t source_bytes = 0U;
    if (g_wav_convert.fast_read.state != WAV_CONVERT_FAST_READ_IDLE)
    {
        if (wav_convert_fast_read_consume(&source, &source_bytes) == 0U)
        {
            return 0U;
        }
        const uint32_t read_frames =
            source_bytes / g_wav_convert.source_info.block_align;
        if ((read_frames == 0U)
            || (g_wav_convert.pack_frames + read_frames
                > g_wav_convert.pack_target_frames))
        {
            wav_convert_fail(WAV_CONVERT_ERROR_READ_FAIL);
            return 0U;
        }
        uint8_t *const output = &g_storage_shared_io[
            g_wav_convert.pack_frames * WAV_CONVERT_TARGET_BYTES_PER_FRAME];
        if (g_wav_convert.copy_mode == WAV_CONVERT_COPY_RAW_FLOAT)
        {
            memcpy(output, source, source_bytes);
        }
        else
        {
            const uint32_t started = wav_convert_bench_cycles();
            wav_audio_codec_decode_stereo_block(
                source,
                g_wav_convert.source_info.encoding,
                g_wav_convert.source_info.channels,
                g_wav_convert.source_info.bits_per_sample,
                (float *)(void *)output,
                read_frames);
            g_wav_convert_bench.decode_cycles +=
                (uint32_t)(wav_convert_bench_cycles() - started);
        }
        g_wav_convert.pack_frames += read_frames;
        g_wav_convert.source_byte_offset += source_bytes;
        wav_convert_fast_read_reset(&g_wav_convert.fast_read);
    }

    if (g_wav_convert.pack_frames >= g_wav_convert.pack_target_frames)
    {
        return wav_convert_write_output(g_wav_convert.pack_frames);
    }

    uint32_t read_frames =
        g_wav_convert.pack_target_frames - g_wav_convert.pack_frames;
    const uint32_t source_capacity =
        WAV_CONVERT_FAST_READ_BYTES / g_wav_convert.source_info.block_align;
    if (read_frames > source_capacity) read_frames = source_capacity;
    const uint32_t bytes = read_frames * g_wav_convert.source_info.block_align;
    if ((bytes == 0U) || (wav_convert_fast_read_start(bytes) == 0U))
    {
        return 0U;
    }
    return 0U;
}

static uint8_t wav_convert_copy_resample_service(void)
{
    uint8_t *source = 0;
    uint32_t source_bytes = 0U;
    if (g_wav_convert.fast_read.state != WAV_CONVERT_FAST_READ_IDLE)
    {
        if (wav_convert_fast_read_consume(&source, &source_bytes) == 0U)
        {
            return 0U;
        }
        const uint16_t skip = g_wav_convert.fast_read.first_sector_skip;
        (void)source;
        if (wav_audio_stream_external_input_commit(
                &g_wav_convert.stream, skip, source_bytes) == 0U)
        {
            wav_convert_fail(WAV_CONVERT_ERROR_READ_FAIL);
            return 0U;
        }
        g_wav_convert.source_byte_offset += source_bytes;
        wav_convert_fast_read_reset(&g_wav_convert.fast_read);
    }

    const uint32_t remaining =
        g_wav_convert.pack_target_frames - g_wav_convert.pack_frames;
    if (remaining != 0U)
    {
        const uint32_t started = wav_convert_bench_cycles();
        const uint32_t produced = wav_audio_stream_read_frames(
            &g_wav_convert.stream,
            &((float *)(void *)g_storage_shared_io)[
                g_wav_convert.pack_frames * 2U],
            remaining);
        g_wav_convert_bench.src_cycles +=
            (uint32_t)(wav_convert_bench_cycles() - started);
        g_wav_convert.pack_frames += produced;
    }

    if (g_wav_convert.pack_frames >= g_wav_convert.pack_target_frames)
    {
        return wav_convert_write_output(g_wav_convert.pack_frames);
    }

    uint32_t request = 0U;
    if (wav_audio_stream_external_input_request(
            &g_wav_convert.stream, WAV_CONVERT_FAST_READ_BYTES,
            &request) != 0U)
    {
        if (wav_convert_fast_read_start(request) == 0U)
        {
            return 0U;
        }
        return 0U;
    }
    wav_convert_fail((g_wav_convert.stream.io_error != 0U)
        ? WAV_CONVERT_ERROR_READ_FAIL : WAV_CONVERT_ERROR_UNSUPPORTED);
    return 0U;
}

static uint8_t wav_convert_copy_phase(uint32_t byte_budget)
{
    if (g_wav_convert.fast_write.state != WAV_CONVERT_FAST_WRITE_IDLE)
    {
        const wav_convert_fast_write_state_t state =
            wav_convert_fast_write_service(&g_wav_convert.fast_write);
        if (state == WAV_CONVERT_FAST_WRITE_PENDING) return 0U;
        if (wav_convert_try_hold_gate() == 0U) return 0U;
        if (state != WAV_CONVERT_FAST_WRITE_READY)
        {
            wav_convert_fail(WAV_CONVERT_ERROR_WRITE_FAIL);
            return 0U;
        }
        const uint32_t completed = g_wav_convert.fast_write.frame_count;
        g_wav_convert.frames_done += completed;
        g_wav_convert.frames_written += completed;
        g_wav_convert.pack_frames = 0U;
        g_wav_convert.pack_target_frames = 0U;
        wav_convert_fast_write_reset(&g_wav_convert.fast_write);
    }
    if (g_wav_convert.frames_done >= g_wav_convert.target_frames)
    {
        g_wav_convert.phase = WAV_CONVERT_PHASE_SYNC;
        return 1U;
    }
    if (g_wav_convert.pack_target_frames == 0U)
    {
        uint32_t frames_budget = byte_budget / WAV_CONVERT_TARGET_BYTES_PER_FRAME;
        if (frames_budget > WAV_CONVERT_PACK_FRAMES)
        {
            frames_budget = WAV_CONVERT_PACK_FRAMES;
        }
        const uint32_t remaining =
            g_wav_convert.target_frames - g_wav_convert.frames_done;
        if (frames_budget > remaining) frames_budget = remaining;
        if (frames_budget < remaining)
        {
            frames_budget &= ~63U;
            if (frames_budget == 0U)
                frames_budget = (remaining < 64U) ? remaining : 64U;
        }
        if (frames_budget == 0U) return 0U;
        g_wav_convert.pack_target_frames = frames_budget;
    }

    if (g_wav_convert.copy_mode == WAV_CONVERT_COPY_RESAMPLE)
        return wav_convert_copy_resample_service();
    return wav_convert_copy_48k_service();
}

static uint8_t wav_convert_sync_phase(void)
{
    const FSIZE_t exact_bytes = (FSIZE_t)WAV_CONVERT_WAV_DATA_OFFSET_BYTES
        + g_wav_convert.target_data_bytes;
    g_wav_convert_bench.sync_calls++;
    FRESULT fr = f_brick_rec_commit(&g_wav_convert.dst, exact_bytes,
                                    &g_wav_convert.destination_state, 0);
    if (fr == FR_OK)
    {
        g_wav_convert_bench.sync_calls++;
        fr = f_brick_rec_release_tail(&g_wav_convert.dst, exact_bytes,
            0U, 0U, &g_wav_convert.destination_state, 0);
    }
    if (fr != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_SYNC_FAIL);
        return 0U;
    }

    g_wav_convert.phase = WAV_CONVERT_PHASE_CLOSE;
    return 1U;
}

static uint8_t wav_convert_close_phase(void)
{
    g_wav_convert_bench.close_calls++;
    FRESULT fr = f_brick_rec_close_synced(&g_wav_convert.dst);
    if (fr != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_CLOSE_FAIL);
        return 0U;
    }
    g_wav_convert.dst_open = 0U;

    fr = wav_convert_bench_close(&g_wav_convert.src);
    g_wav_convert.src_open = 0U;
    if (fr != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_CLOSE_FAIL);
        return 0U;
    }
    sample_stream_physical_map_release(
        &g_wav_convert.source_metadata.physical_map);

    g_wav_convert.phase = WAV_CONVERT_PHASE_VERIFY;
    return 1U;
}

static uint8_t wav_convert_verify_phase(void)
{
    FIL fp;
    if (wav_convert_bench_open(&fp, g_wav_convert.temp_path, FA_READ) != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_VERIFY_FAIL);
        return 0U;
    }
    UINT br = 0U;
    const FRESULT fr = wav_convert_bench_read(0, &fp,
                              g_wav_convert.stream.io_buf,
                              WAV_CONVERT_WAV_DATA_OFFSET_BYTES, &br);
    const FSIZE_t file_size = f_size(&fp);
    const FRESULT close_fr = wav_convert_bench_close(&fp);
    const uint8_t *const h = g_wav_convert.stream.io_buf;
    const uint8_t valid = ((fr == FR_OK)
        && (br == WAV_CONVERT_WAV_DATA_OFFSET_BYTES)
        && (close_fr == FR_OK)
        && (file_size == (FSIZE_t)(WAV_CONVERT_WAV_DATA_OFFSET_BYTES
                                   + g_wav_convert.target_data_bytes))
        && (memcmp(&h[0], "RIFF", 4U) == 0)
        && (wav_convert_read_le32(&h[4])
            == (WAV_CONVERT_WAV_DATA_OFFSET_BYTES - 8U
                + g_wav_convert.target_data_bytes))
        && (memcmp(&h[8], "WAVEfmt ", 8U) == 0)
        && (wav_convert_read_le32(&h[16]) == 16U)
        && (wav_convert_read_le16(&h[20]) == 3U)
        && (wav_convert_read_le16(&h[22]) == WAV_CONVERT_TARGET_CHANNELS)
        && (wav_convert_read_le32(&h[24]) == WAV_CONVERT_TARGET_RATE)
        && (wav_convert_read_le32(&h[28]) == 384000U)
        && (wav_convert_read_le16(&h[32]) == WAV_CONVERT_TARGET_BYTES_PER_FRAME)
        && (wav_convert_read_le16(&h[34]) == WAV_CONVERT_TARGET_BITS)
        && (memcmp(&h[36], "fact", 4U) == 0)
        && (wav_convert_read_le32(&h[40]) == 4U)
        && (wav_convert_read_le32(&h[44]) == g_wav_convert.target_frames)
        && (memcmp(&h[48], "JUNK", 4U) == 0)
        && (wav_convert_read_le32(&h[52]) == WAV_CONVERT_WAV_JUNK_BYTES)
        && (memcmp(&h[504], "data", 4U) == 0)
        && (wav_convert_read_le32(&h[508]) == g_wav_convert.target_data_bytes)) ? 1U : 0U;
    if (valid == 0U)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_VERIFY_FAIL);
        return 0U;
    }

    g_wav_convert.phase = WAV_CONVERT_PHASE_REPLACE;
    return 1U;
}

static uint8_t wav_convert_replace_phase(void)
{
    FRESULT fr = f_rename(g_wav_convert.source_path, g_wav_convert.bak_path);
    if (fr != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_REPLACE_FAIL);
        return 0U;
    }
    g_wav_convert.bak_created = 1U;

    fr = f_rename(g_wav_convert.temp_path, g_wav_convert.source_path);
    if (fr != FR_OK)
    {
        if (f_rename(g_wav_convert.bak_path, g_wav_convert.source_path) == FR_OK)
            g_wav_convert.bak_created = 0U;
        wav_convert_fail(WAV_CONVERT_ERROR_REPLACE_FAIL);
        return 0U;
    }
    g_wav_convert.temp_created = 0U;
    /* The media epoch identifies card insertion/removal, not an in-place
     * filesystem mutation.  The final path and catalogue identity survive
     * this replacement. */

    if (f_unlink(g_wav_convert.bak_path) != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_REPLACE_FAIL);
        return 0U;
    }
    g_wav_convert.bak_created = 0U;
    wav_convert_release_gate();
    g_wav_convert.phase = WAV_CONVERT_PHASE_IDLE;
    g_wav_convert.state = WAV_CONVERT_STATE_DONE;
    g_wav_convert.error = WAV_CONVERT_ERROR_NONE;
    wav_convert_bench_finish(2U, WAV_CONVERT_ERROR_NONE);
    return 1U;
}

void wav_convert_service(uint32_t byte_budget)
{
    if (g_wav_convert.state != WAV_CONVERT_STATE_ACTIVE)
    {
        return;
    }
    if (g_wav_convert.cancel_pending != 0U)
    {
        const wav_convert_fast_read_state_t read_state =
            wav_convert_fast_read_service(&g_wav_convert.fast_read);
        const wav_convert_fast_write_state_t write_state =
            wav_convert_fast_write_service(&g_wav_convert.fast_write);
        if ((read_state == WAV_CONVERT_FAST_READ_PENDING)
            || (write_state == WAV_CONVERT_FAST_WRITE_PENDING)) return;
        if (wav_convert_try_hold_gate() == 0U) return;
        wav_convert_fail(WAV_CONVERT_ERROR_NONE);
        return;
    }

    const uint32_t now_ms = HAL_GetTick();
    if (g_wav_convert_bench_have_service_end != 0U)
    {
        const uint32_t gap = now_ms - g_wav_convert_bench_last_service_end_ms;
        g_wav_convert_bench.total_service_gap_ms += gap;
        if (gap > g_wav_convert_bench.max_service_gap_ms)
            g_wav_convert_bench.max_service_gap_ms = gap;
    }
    g_wav_convert_bench.service_calls++;
    const wav_convert_phase_t measured_phase = g_wav_convert.phase;
    const uint64_t read_before = g_wav_convert_bench.read_cycles;
    const uint32_t service_started = wav_convert_bench_cycles();

    switch (measured_phase)
    {
        case WAV_CONVERT_PHASE_OPEN:
            (void)wav_convert_open_phase();
            break;
        case WAV_CONVERT_PHASE_WRITE_HEADER:
            (void)wav_convert_write_header_phase();
            break;
        case WAV_CONVERT_PHASE_COPY:
            (void)wav_convert_copy_phase(byte_budget);
            break;
        case WAV_CONVERT_PHASE_SYNC:
            (void)wav_convert_sync_phase();
            break;
        case WAV_CONVERT_PHASE_CLOSE:
            (void)wav_convert_close_phase();
            break;
        case WAV_CONVERT_PHASE_VERIFY:
            (void)wav_convert_verify_phase();
            break;
        case WAV_CONVERT_PHASE_REPLACE:
            (void)wav_convert_replace_phase();
            break;
        default:
            wav_convert_fail(WAV_CONVERT_ERROR_INVALID_ARG);
            break;
    }

    const uint32_t service_elapsed =
        wav_convert_bench_cycles() - service_started;
    const uint64_t read_delta = g_wav_convert_bench.read_cycles - read_before;
    g_wav_convert_bench.active_cycles += service_elapsed;
    if (measured_phase == WAV_CONVERT_PHASE_OPEN)
    {
        g_wav_convert_bench.open_parse_cycles +=
            (service_elapsed > read_delta) ? (service_elapsed - read_delta) : 0ULL;
    }
    else if ((measured_phase == WAV_CONVERT_PHASE_SYNC)
             || (measured_phase == WAV_CONVERT_PHASE_CLOSE))
    {
        g_wav_convert_bench.sync_close_cycles += service_elapsed;
    }
    else if (measured_phase == WAV_CONVERT_PHASE_VERIFY)
    {
        g_wav_convert_bench.verify_cycles +=
            (service_elapsed > read_delta) ? (service_elapsed - read_delta) : 0ULL;
    }
    else if (measured_phase == WAV_CONVERT_PHASE_REPLACE)
    {
        g_wav_convert_bench.replace_cycles += service_elapsed;
    }
    g_wav_convert_bench_last_service_end_ms = HAL_GetTick();
    g_wav_convert_bench_have_service_end = 1U;
    wav_convert_bench_publish_times();
    if (g_wav_convert_bench.status != 1U)
    {
        g_wav_convert_bench.wall_minus_active_ms =
            (g_wav_convert_bench.total_ms > g_wav_convert_bench.active_ms)
                ? (g_wav_convert_bench.total_ms - g_wav_convert_bench.active_ms)
                : 0U;
    }
}

uint8_t wav_convert_cancel(void)
{
    if (g_wav_convert.state != WAV_CONVERT_STATE_ACTIVE)
    {
        return 0U;
    }
    if (g_wav_convert.fast_read.state == WAV_CONVERT_FAST_READ_PENDING)
    {
        wav_convert_fast_read_cancel(&g_wav_convert.fast_read);
    }
    if (g_wav_convert.fast_write.state == WAV_CONVERT_FAST_WRITE_PENDING)
    {
        wav_convert_fast_write_cancel(&g_wav_convert.fast_write);
    }
    g_wav_convert.cancel_pending = 1U;
    return 1U;
}

uint8_t wav_convert_is_active(void)
{
    return (g_wav_convert.state == WAV_CONVERT_STATE_ACTIVE) ? 1U : 0U;
}

wav_convert_state_t wav_convert_get_state(void)
{
    return g_wav_convert.state;
}

wav_convert_error_t wav_convert_get_last_error(void)
{
    return g_wav_convert.error;
}

uint8_t wav_convert_get_progress_percent(void)
{
    if (g_wav_convert.state == WAV_CONVERT_STATE_DONE)
    {
        return 100U;
    }
    if ((g_wav_convert.state == WAV_CONVERT_STATE_IDLE)
        || (g_wav_convert.target_frames == 0U))
    {
        return 0U;
    }

    uint32_t percent =
        (uint32_t)(((uint64_t)g_wav_convert.frames_done * 95ULL)
                   / (uint64_t)g_wav_convert.target_frames);
    if ((g_wav_convert.phase == WAV_CONVERT_PHASE_VERIFY)
        || (g_wav_convert.phase == WAV_CONVERT_PHASE_REPLACE))
    {
        percent = 98U;
    }
    if (percent > 100U)
    {
        percent = 100U;
    }
    return (uint8_t)percent;
}

uint32_t wav_convert_get_output_bytes_done(void)
{
    return g_wav_convert.frames_written * WAV_CONVERT_TARGET_BYTES_PER_FRAME;
}

uint32_t wav_convert_get_output_bytes_total(void)
{
    return g_wav_convert.target_data_bytes;
}

void wav_convert_clear_finished(void)
{
    if ((g_wav_convert.state == WAV_CONVERT_STATE_DONE)
        || (g_wav_convert.state == WAV_CONVERT_STATE_FAILED))
    {
        memset(&g_wav_convert, 0, sizeof(g_wav_convert));
    }
}
