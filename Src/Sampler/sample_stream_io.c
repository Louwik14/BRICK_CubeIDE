#include "Sampler/sample_stream_io.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_stream_backend_physical.h"
#include "Sampler/sample_stream_limits.h"
#include "Storage/wav_audio_codec.h"
#include "SD/sd_block_device.h"
#include "SD/sd_scheduler_runtime.h"
#include "Platform/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "stm32h7xx_hal.h"
#include "ff.h"

#ifndef BRICK6_STREAM_READ_CHUNK_KIB
#define BRICK6_STREAM_READ_CHUNK_KIB (32U)
#endif
#define SAMPLE_STREAM_IO_SECTOR_BYTES (512U)
#define SAMPLE_STREAM_IO_REC_BYTES_PER_FRAME (6U)
#define SAMPLE_STREAM_IO_REC_DECODE_FRAMES (512U)
#define SAMPLE_STREAM_IO_JOB_COUNT (2U)

_Static_assert((SAMPLE_PAGE_FRAMES * SAMPLE_STREAM_IO_REC_BYTES_PER_FRAME
                + (2U * (SAMPLE_STREAM_IO_SECTOR_BYTES - 1U)))
                   <= SAMPLE_PAGE_BYTES,
               "A recorder sector span must fit directly in one float page");
typedef enum
{
    SAMPLE_STREAM_IO_JOB_FREE = 0,
    SAMPLE_STREAM_IO_JOB_DMA,
    SAMPLE_STREAM_IO_JOB_DATA_READY,
    SAMPLE_STREAM_IO_JOB_FINALIZING
} sample_stream_io_job_state_t;

SDRAM_STREAM_SERVICE __attribute__((aligned(32))) static uint8_t
    g_sample_stream_io_rec_decode[
        SAMPLE_STREAM_IO_REC_DECODE_FRAMES * SAMPLE_STREAM_IO_REC_BYTES_PER_FRAME];
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
    uint8_t state;
    uint8_t active;
    uint8_t physical_active;
    uint8_t direct_float;
    uint8_t recorder_pcm24;
} sample_stream_io_async_t;
SDRAM_STREAM_SERVICE static sample_stream_io_async_t
    g_sample_stream_io_async[SAMPLE_STREAM_IO_JOB_COUNT];
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

static uint8_t sample_stream_io_target_matches(
    const sample_stream_io_async_t *async,
    const sample_page_load_target_t *target)
{
    return (uint8_t)((async != NULL) && (target != NULL)
        && (target->slot_index == async->command.target.slot_index)
        && (target->page_index == async->command.target.page_index)
        && (target->page_generation == async->command.target.page_generation)
        && (target->registration_epoch == async->command.target.registration_epoch)
        && (target->frame_count == async->command.target.frame_count)
        && (target->format == async->command.target.format)
        && (target->stride_floats == async->command.target.stride_floats)
        && (target->frames_interleaved == async->target.frames_interleaved));
}

static void sample_stream_io_finalize(sample_stream_io_async_t *async)
{
    if ((async == NULL) || (async->result.load_result != SAMPLE_PAGE_LOAD_OK)) return;
    sample_page_load_target_t target;
    if ((async->media_epoch != sd_access_media_epoch())
        || (sample_page_cache_resolve_loading_target(
                &async->result.token, &target) == 0U)
        || (sample_stream_io_target_matches(async, &target) == 0U))
    {
        async->result.load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
        return;
    }
    const uint32_t expected_bytes = target.frame_count
        * async->command.stream_info.info.block_align;
    if ((expected_bytes == 0U) || (async->result.source_bytes != expected_bytes))
    {
        async->result.load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        return;
    }
    if (async->direct_float != 0U)
    {
        if ((async->source != (const uint8_t *)target.frames_interleaved)
            || (expected_bytes != target.frame_count * 2U * sizeof(float)))
            async->result.load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
        return;
    }
    if (async->recorder_pcm24 != 0U)
    {
        if (async->source == NULL)
        {
            async->result.load_result = SAMPLE_PAGE_LOAD_DECODE_FAILED;
            return;
        }
        uint32_t remaining = target.frame_count;
        /* Decode from the end so expanding 6-byte PCM frames to 8-byte float
         * frames cannot overwrite source bytes that have not been copied. */
        while (remaining != 0U)
        {
            const uint32_t count = (remaining > SAMPLE_STREAM_IO_REC_DECODE_FRAMES)
                ? SAMPLE_STREAM_IO_REC_DECODE_FRAMES : remaining;
            const uint32_t first = remaining - count;
            memcpy(g_sample_stream_io_rec_decode,
                   &async->source[first * SAMPLE_STREAM_IO_REC_BYTES_PER_FRAME],
                   count * SAMPLE_STREAM_IO_REC_BYTES_PER_FRAME);
            wav_audio_codec_decode_pcm24_stereo_block(
                g_sample_stream_io_rec_decode,
                &target.frames_interleaved[first * 2U], count);
            remaining = first;
        }
        return;
    }
    async->result.load_result = SAMPLE_PAGE_LOAD_UNSUPPORTED_SAMPLE;
}

uint8_t sample_stream_io_begin(const sample_stream_io_command_t *command)
{
    /* A Storage worker must never resolve or write the M7 page-cache. */
    (void)command;
    return 0U;
}

uint8_t sample_stream_io_begin_to(const sample_stream_io_command_t *command)
{
    sample_stream_io_async_t *async = 0;
    if (command == 0)
    {
        return 0U;
    }
    (void)sample_stream_backend_physical_busy();
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_JOB_COUNT; ++i)
    {
        if ((g_sample_stream_io_async[i].physical_active != 0U)
                && (g_sample_stream_io_async[i].physical.cancel_requested != 0U)
                && (g_sample_stream_io_async[i].physical.completed != 0U))
        {
            memset(&g_sample_stream_io_async[i], 0,
                   sizeof(g_sample_stream_io_async[i]));
        }
    }
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_JOB_COUNT; ++i)
    {
        if (g_sample_stream_io_async[i].active == 0U)
        {
            async = &g_sample_stream_io_async[i];
            memset(async, 0, sizeof(*async));
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
        .frames_interleaved = NULL,
    };
    if ((sample_audio_format_is_valid(async->target.format) == 0U)
        || (async->target.frame_count == 0U)
        || (async->target.frames_per_page == 0U)
        || (async->target.frame_count > async->target.frames_per_page))
    {
        async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
        return 1U;
    }

    if ((sample_page_cache_resolve_loading_target(
            &async->result.token, &async->target) == 0U)
        || (sample_stream_io_target_matches(async, &async->target) == 0U)
        || (async->target.frames_interleaved == NULL))
    {
        async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
        return 1U;
    }

    const wav_info_t *const wav = &command->stream_info.info;
    async->direct_float = (uint8_t)(
        (command->target.key.domain != SAMPLE_AUDIO_DOMAIN_REC)
        && (wav_parser_is_canonical_brick_float(wav) != 0U)
        && (command->target.format == SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED)
        && (command->target.stride_floats == 2U)
        && ((((uint64_t)command->stream_info.data_offset
              + (uint64_t)command->target.start_frame * 8U)
             % SAMPLE_STREAM_IO_SECTOR_BYTES) == 0U));
    async->recorder_pcm24 = (uint8_t)(
        (command->target.key.domain == SAMPLE_AUDIO_DOMAIN_REC)
        && (wav->encoding == WAV_SAMPLE_ENCODING_PCM_INTEGER)
        && (wav->sample_rate == 48000U) && (wav->channels == 2U)
        && (wav->bits_per_sample == 24U) && (wav->block_align == 6U)
        && (command->target.format == SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED));
    if ((async->direct_float == 0U) && (async->recorder_pcm24 == 0U))
    {
        async->result.load_result = SAMPLE_PAGE_LOAD_UNSUPPORTED_SAMPLE;
        async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
        return 1U;
    }

    async->result.source_bytes = async->target.frame_count
                                 * command->stream_info.info.block_align;
    if ((async->result.source_bytes == 0U)
        || ((async->direct_float != 0U)
            && (async->result.source_bytes > SAMPLE_PAGE_BYTES))
        || ((async->recorder_pcm24 != 0U)
            && ((uint64_t)async->result.source_bytes
                + (2U * (SAMPLE_STREAM_IO_SECTOR_BYTES - 1U))
                > SAMPLE_PAGE_BYTES)))
    {
        async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
        return 1U;
    }
    sample_stream_physical_cursor_t *const cursor = &async->local_physical_cursor;
    const uint8_t physical_expected = (uint8_t)(
        sample_stream_safe_metadata_backend(&command->stream_info.stream_safe)
            == SAMPLE_STREAM_BACKEND_PHYSICAL);
    if(physical_expected != 0U)
    {
        if(sample_stream_backend_physical_begin(
                    &async->physical,
                    &async->command.stream_info,
                    &async->target,
                    cursor,
                    (uint8_t *)async->target.frames_interleaved,
                    SAMPLE_PAGE_BYTES,
                    command->deadline_margin_us) != 0U)
        {
            async->physical_active = 1U;
            async->state = SAMPLE_STREAM_IO_JOB_DMA;
            return 1U;
        }
        async->result.load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
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
            if ((f_lseek(&file, (FSIZE_t)source_offset) == FR_OK)
                && (f_read(&file, async->target.frames_interleaved,
                           async->result.source_bytes, &read) == FR_OK)
                && (read == async->result.source_bytes))
            {
                async->result.read_bytes = read;
                async->result.load_result = SAMPLE_PAGE_LOAD_OK;
                async->source = (const uint8_t *)async->target.frames_interleaved;
            }
            (void)f_close(&file);
        }
        async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
        return 1U;
    }
    /* Deadline streaming stays physical-only; synchronous full imports may
     * use the bounded Storage-side FatFs fallback above. */
    async->result.load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
    async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
    return 1U;
}

static uint8_t sample_stream_io_poll_impl(sample_stream_io_result_t *out_result)
{
    sample_stream_io_async_t *async = 0;
    if (out_result == 0)
    {
        return 0U;
    }
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_JOB_COUNT; ++i)
    {
        sample_stream_io_async_t *const candidate = &g_sample_stream_io_async[i];
        if ((candidate->active != 0U)
            && (candidate->state == SAMPLE_STREAM_IO_JOB_DATA_READY)
            && ((async == 0) || (candidate->order < async->order)))
        {
            async = candidate;
        }
    }
    if (async != 0)
    {
        async->state = SAMPLE_STREAM_IO_JOB_FINALIZING;
        sample_stream_io_finalize(async);
        *out_result = async->result;
        memset(async, 0, sizeof(*async));
        return 1U;
    }
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_JOB_COUNT; ++i)
    {
        if ((g_sample_stream_io_async[i].active != 0U)
            && (g_sample_stream_io_async[i].state == SAMPLE_STREAM_IO_JOB_DMA))
        {
            async = &g_sample_stream_io_async[i];
            break;
        }
    }
    if ((async != 0) && (async->physical_active != 0U))
    {
        sample_page_load_result_t physical_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        uint8_t physical_reads;
        if (sample_stream_backend_physical_poll(
                &async->physical,
                &physical_result,
                &async->source,
                &async->result.source_bytes,
                &physical_reads) == 0U)
        {
            return 0U;
        }
        async->physical_active = 0U;
        async->result.load_result = physical_result;
        if (physical_result == SAMPLE_PAGE_LOAD_OK)
        {
            async->result.read_bytes = async->result.source_bytes;
        }
        async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
        return 0U;
    }
    return 0U;
}

uint8_t sample_stream_io_poll(sample_stream_io_result_t *out_result)
{
    const uint8_t result = sample_stream_io_poll_impl(out_result);
    return result;
}

void sample_stream_io_cancel(void)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_JOB_COUNT; ++i)
    {
        if (g_sample_stream_io_async[i].physical_active != 0U)
        {
            sample_stream_backend_physical_cancel(&g_sample_stream_io_async[i].physical);
        }
        else
        {
            memset(&g_sample_stream_io_async[i], 0,
                   sizeof(g_sample_stream_io_async[i]));
        }
    }
}
