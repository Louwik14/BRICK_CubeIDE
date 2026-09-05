#include "Sampler/sample_stream_io.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_stream_backend_physical.h"
#include "Sampler/sample_stream_decoder.h"
#include "Sampler/sample_stream_limits.h"
#include "SD/sd_block_device.h"
#include "SD/sd_scheduler_runtime.h"
#include "Platform/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "Storage/storage_io_wakeup.h"
#include "stm32h7xx_hal.h"
#include "ff.h"

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
typedef struct
{
    sample_stream_io_command_t command;
    sample_page_load_target_t target;
    sample_stream_io_result_t result;
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

void sample_stream_io_init(void)
{
    memset(g_sample_stream_io_async, 0, sizeof(g_sample_stream_io_async));
    g_sample_stream_io_next_order = 1U;
    sd_block_device_async_init();
    sd_scheduler_runtime_init();
    if (sample_stream_io_chunk_valid(g_sample_stream_io_chunk_kib) == 0U)
    {
        g_sample_stream_io_chunk_kib = SAMPLE_STREAM_READ_CHUNK_32_KIB;
    }
}

void sample_stream_io_reset(void)
{
    sample_stream_io_cancel();
}

void sample_stream_io_release_key(sample_audio_key_t key)
{
    /* Physical transport has no persistent FatFs reader to release. */
    (void)key;
}

uint32_t sample_stream_io_active_reader_count(void)
{
    return 0U;
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
    if ((async->media_epoch != sd_access_media_epoch())
        || (async->target.frames_interleaved == NULL)
        || (async->target.page_generation != async->command.target.page_generation)
        || (async->target.registration_epoch != async->command.target.registration_epoch))
    {
        async->result.load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
        return;
    }
    const uint32_t decode_begin = DWT->CYCCNT;
    async->result.load_result = sample_stream_decoder_decode_page(
        &async->command.stream_info, &async->target, async->source,
        async->result.source_bytes);
    async->result.decode_cycles = DWT->CYCCNT - decode_begin;
}

uint8_t sample_stream_io_begin(const sample_stream_io_command_t *command)
{
    /* A Storage worker must never resolve or write the M7 page-cache. */
    (void)command;
    return 0U;
}

uint8_t sample_stream_io_begin_to(const sample_stream_io_command_t *command,
                                  float *decoded_frames,
                                  uint32_t decoded_capacity_bytes)
{
    sample_stream_io_async_t *async = 0;
    if ((command == 0) || (decoded_frames == NULL)
        || (decoded_capacity_bytes < SAMPLE_PAGE_BYTES))
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
    async->target = (sample_page_load_target_t){
        .key = command->target.key,
        .page_index = command->target.page_index,
        .start_frame = command->target.start_frame,
        .frame_count = command->target.frame_count,
        .frames_per_page = command->target.frames_per_page,
        .registration_epoch = command->target.registration_epoch,
        .page_generation = command->target.page_generation,
        .slot_index = command->target.slot_index,
        .format = command->target.format,
        .stride_floats = command->target.stride_floats,
        .frames_interleaved = decoded_frames,
    };
    if ((sample_audio_format_is_valid(async->target.format) == 0U)
        || (async->target.frame_count == 0U)
        || (async->target.frames_per_page == 0U)
        || (async->target.frame_count > async->target.frames_per_page))
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
    sample_stream_physical_cursor_t *const cursor = &async->local_physical_cursor;
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
        async->result.load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        async->state = SAMPLE_STREAM_IO_SCRATCH_RAW_READY;
        return 1U;
    }
    if ((command->stream_info.physical_only == 0U)
        && (command->deadline_margin_us == UINT32_MAX)
        && (command->stream_info.path[0] != '\0'))
    {
        FIL file;
        UINT read = 0U;
        const uint64_t source_offset =
            (uint64_t)command->stream_info.data_offset
            + ((uint64_t)async->target.start_frame
               * command->stream_info.info.block_align);
        if ((source_offset <= UINT32_MAX)
            && (f_open(&file, command->stream_info.path, FA_READ) == FR_OK))
        {
            async->result.file_opens = 1U;
            async->result.fatfs_ops++;
            if ((f_lseek(&file, (FSIZE_t)source_offset) == FR_OK)
                && (f_read(&file, async->scratch,
                           async->result.source_bytes, &read) == FR_OK)
                && (read == async->result.source_bytes))
            {
                async->result.seeks = 1U;
                async->result.fatfs_ops += 2U;
                async->result.read_bytes = read;
                async->result.load_result = SAMPLE_PAGE_LOAD_OK;
                async->source = async->scratch;
            }
            (void)f_close(&file);
            async->result.fatfs_ops++;
        }
        async->state = SAMPLE_STREAM_IO_SCRATCH_RAW_READY;
        return 1U;
    }
    /* Deadline streaming stays physical-only; full imports may use the
     * bounded Storage-side FatFs fallback above. */
    async->result.load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
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
            async->result.backend = 1U;
            async->result.read_bytes = async->result.source_bytes;
        }
        async->state = SAMPLE_STREAM_IO_SCRATCH_RAW_READY;
        storage_io_wakeup(STORAGE_IO_WAKE_WORK);
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
