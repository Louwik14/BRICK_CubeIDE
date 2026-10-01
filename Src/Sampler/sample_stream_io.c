#include "Sampler/sample_stream_io.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_stream_backend_physical.h"
#include "Sampler/sample_stream_limits.h"
#include "Platform/stream_rec_perf.h"
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
#define SAMPLE_STREAM_IO_JOB_COUNT SAMPLE_STREAM_IO_JOB_CAPACITY

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

typedef struct
{
    float *frames_interleaved;
    uint32_t start_frame;
    uint32_t frame_count;
    uint32_t frames_per_page;
    sample_audio_format_t format;
    uint16_t stride_floats;
} sample_stream_io_page_t;

_Static_assert(sizeof(sample_stream_io_page_t) == 20U,
               "stream job page view budget changed");

SDRAM_STREAM_SERVICE __attribute__((aligned(32))) static uint8_t
    g_sample_stream_io_rec_decode[
        SAMPLE_STREAM_IO_REC_DECODE_FRAMES * SAMPLE_STREAM_IO_REC_BYTES_PER_FRAME];
/* One fixed-lifetime STORAGE job owns the frozen page identity, resolved page
 * view, immutable stream metadata, async backend state and final result. */
typedef struct
{
    sample_page_load_token_t token;
    sample_stream_io_page_t page;
    sample_page_stream_load_info_t stream;
    sample_stream_physical_cursor_t local_physical_cursor;
    sample_stream_backend_physical_async_t physical;
    const uint8_t *source;
    uint32_t source_bytes;
    uint32_t read_bytes;
    uint32_t request_cycles;
    uint32_t media_epoch;
    uint32_t order;
    uint32_t perf_dma_done_cycles;
    sample_page_load_result_t load_result;
    uint8_t state;
    uint8_t active;
    uint8_t physical_active;
    uint8_t direct_float;
    uint8_t legacy_recorder_pcm24;
} sample_stream_io_async_t;
_Static_assert(sizeof(sample_page_stream_load_info_t) == 128U,
               "stream load snapshot budget changed");
_Static_assert(sizeof(sample_stream_io_async_t) == 368U,
               "stream async job budget changed");
STREAM_LOCAL_D2 static sample_stream_io_async_t
    g_sample_stream_io_async[SAMPLE_STREAM_IO_JOB_COUNT];
static uint32_t g_sample_stream_io_next_order;
static sample_stream_read_chunk_kib_t g_sample_stream_io_chunk_kib =
    (sample_stream_read_chunk_kib_t)BRICK6_STREAM_READ_CHUNK_KIB;

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
        && (target->slot_index == async->token.slot_index)
        && (target->page_index == async->token.page_index)
        && (target->page_generation == async->token.page_generation)
        && (target->registration_epoch == async->token.registration_epoch)
        && (sample_audio_key_equal(&target->key, &async->token.key) != 0U)
        && (target->start_frame == async->page.start_frame)
        && (target->frame_count == async->page.frame_count)
        && (target->frames_per_page == async->page.frames_per_page)
        && (target->format == async->page.format)
        && (target->stride_floats == async->page.stride_floats)
        && (target->frames_interleaved == async->page.frames_interleaved));
}

static void sample_stream_io_finalize(sample_stream_io_async_t *async)
{
    if ((async == NULL) || (async->load_result != SAMPLE_PAGE_LOAD_OK)) return;
    sample_page_load_target_t target;
    if ((async->media_epoch != sd_access_media_epoch())
        || (sample_page_cache_resolve_loading_target(&async->token, &target) == 0U)
        || (sample_stream_io_target_matches(async, &target) == 0U))
    {
        async->load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
        return;
    }
    const uint32_t expected_bytes = target.frame_count
        * async->stream.stream_safe.block_align;
    if ((expected_bytes == 0U) || (async->source_bytes != expected_bytes))
    {
        async->load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        return;
    }
    if (async->direct_float != 0U)
    {
        if ((async->source != (const uint8_t *)target.frames_interleaved)
            || (expected_bytes != target.frame_count * 2U * sizeof(float)))
            async->load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
        return;
    }
    if (async->legacy_recorder_pcm24 != 0U)
    {
        if (async->source == NULL)
        {
            async->load_result = SAMPLE_PAGE_LOAD_DECODE_FAILED;
            return;
        }
#if BRICK_PERF_DIAG
        uint32_t scratch_cycles = 0U;
        uint32_t conversion_cycles = 0U;
#endif
        uint32_t remaining = target.frame_count;
        /* Decode from the end so expanding 6-byte PCM frames to 8-byte float
         * frames cannot overwrite source bytes that have not been copied. */
        while (remaining != 0U)
        {
            const uint32_t count = (remaining > SAMPLE_STREAM_IO_REC_DECODE_FRAMES)
                ? SAMPLE_STREAM_IO_REC_DECODE_FRAMES : remaining;
            const uint32_t first = remaining - count;
            PERF_START(scratch_start);
            memcpy(g_sample_stream_io_rec_decode,
                   &async->source[first * SAMPLE_STREAM_IO_REC_BYTES_PER_FRAME],
                   count * SAMPLE_STREAM_IO_REC_BYTES_PER_FRAME);
            #if BRICK_PERF_DIAG
            scratch_cycles += brick_perf_now() - scratch_start;
            #endif
            PERF_START(conversion_start);
            wav_audio_codec_decode_pcm24_stereo_block(
                g_sample_stream_io_rec_decode,
                &target.frames_interleaved[first * 2U], count);
            #if BRICK_PERF_DIAG
            conversion_cycles += brick_perf_now() - conversion_start;
            #endif
            remaining = first;
        }
        if (sample_page_cache_clean_loading_payload(
                &async->token,
                target.frame_count * 2U * sizeof(float)) == 0U)
        {
            async->load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
            return;
        }
#if BRICK_PERF_DIAG
        brick_perf_add(PERF_CPU_REC_SOURCE_SCRATCH, scratch_cycles);
        brick_perf_add(PERF_CPU_REC_SOURCE_CONVERT, conversion_cycles);
#endif
        PERF_COUNT(PERF_N_REC_SOURCE_PAGES);
        PERF_ACCUM(PERF_N_REC_SOURCE_FRAMES, target.frame_count);
        return;
    }
    async->load_result = SAMPLE_PAGE_LOAD_UNSUPPORTED_SAMPLE;
}

uint8_t sample_stream_io_begin(const sample_page_load_token_t *token,
                               uint32_t deadline_margin_us)
{
    PERF_START(begin_start);
    sample_stream_io_async_t *async = 0;
    sample_page_load_target_t target;
    if (token == 0)
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
    async->token = *token;
    async->load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
    async->request_cycles = brick_perf_now();
    if ((sample_page_cache_resolve_loading_target(&async->token, &target) == 0U)
        || (sample_page_cache_get_stream_load_info_key(
                async->token.key, &async->stream) == 0U)
        || (sample_audio_key_equal(&target.key, &async->stream.key) == 0U)
        || (target.format != async->stream.format)
        || (target.stride_floats != async->stream.stride_floats)
        || (target.frames_per_page != async->stream.frames_per_page)
        || (target.registration_epoch != async->stream.registration_epoch))
    {
        async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
        return 1U;
    }
    async->page = (sample_stream_io_page_t){
        .frames_interleaved = target.frames_interleaved,
        .start_frame = target.start_frame,
        .frame_count = target.frame_count,
        .frames_per_page = target.frames_per_page,
        .format = target.format,
        .stride_floats = target.stride_floats,
    };
    if ((sample_audio_format_is_valid(async->page.format) == 0U)
        || (async->page.frame_count == 0U)
        || (async->page.frames_per_page == 0U)
        || (async->page.frame_count > async->page.frames_per_page)
        || (async->page.frames_interleaved == NULL)
        || (sample_stream_io_target_matches(async, &target) == 0U))
    {
        async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
        return 1U;
    }

    async->direct_float = (uint8_t)(
        (async->stream.canonical_float != 0U)
        && (async->page.format == SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED)
        && (async->page.stride_floats == 2U)
        && ((((uint64_t)async->stream.stream_safe.data_offset_bytes
              + (uint64_t)async->page.start_frame * 8U)
             % SAMPLE_STREAM_IO_SECTOR_BYTES) == 0U));
    async->legacy_recorder_pcm24 = (uint8_t)(
        (async->token.key.domain == SAMPLE_AUDIO_DOMAIN_REC)
        && (async->stream.encoding == WAV_SAMPLE_ENCODING_PCM_INTEGER)
        && (async->stream.stream_safe.sample_rate == 48000U)
        && (async->stream.stream_safe.channels == 2U)
        && (async->stream.stream_safe.bits_per_sample == 24U)
        && (async->stream.stream_safe.block_align == 6U)
        && (async->page.format == SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED));
    if ((async->direct_float == 0U) && (async->legacy_recorder_pcm24 == 0U))
    {
        async->load_result = SAMPLE_PAGE_LOAD_UNSUPPORTED_SAMPLE;
        async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
        return 1U;
    }

    async->source_bytes = async->page.frame_count
                          * async->stream.stream_safe.block_align;
    if ((async->source_bytes == 0U)
        || ((async->direct_float != 0U)
            && (async->source_bytes > SAMPLE_PAGE_BYTES))
        || ((async->legacy_recorder_pcm24 != 0U)
            && ((uint64_t)async->source_bytes
                + (2U * (SAMPLE_STREAM_IO_SECTOR_BYTES - 1U))
                > SAMPLE_PAGE_BYTES)))
    {
        async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
        return 1U;
    }
    sample_stream_physical_cursor_t *const cursor = &async->local_physical_cursor;
    const uint8_t physical_expected = (uint8_t)(
        sample_stream_safe_metadata_backend(&async->stream.stream_safe)
            == SAMPLE_STREAM_BACKEND_PHYSICAL);
    if(physical_expected != 0U)
    {
        if(sample_stream_backend_physical_begin(
                    &async->physical,
                    &async->stream.stream_safe,
                    &target,
                    cursor,
                    (uint8_t *)async->page.frames_interleaved,
                    SAMPLE_PAGE_BYTES,
                    sample_page_cache_loading_target_payload_cpu_clean(
                        &target),
                    deadline_margin_us) != 0U)
        {
            async->physical_active = 1U;
            async->state = SAMPLE_STREAM_IO_JOB_DMA;
            PERF_END(PERF_CPU_STREAM_IO_BEGIN, begin_start);
            return 1U;
        }
        async->load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
        return 1U;
    }
    if ((async->stream.physical_only == 0U)
        && (deadline_margin_us == UINT32_MAX))
    {
        sample_page_stream_info_t fallback;
        FIL file;
        UINT read = 0U;
        const uint64_t source_offset =
            (uint64_t)async->stream.stream_safe.data_offset_bytes
            + ((uint64_t)async->page.start_frame
               * async->stream.stream_safe.block_align);
        if ((source_offset <= UINT32_MAX)
            && (sample_page_cache_get_stream_info_key(
                    async->token.key, &fallback) != 0U)
            && (fallback.registration_epoch
                == async->token.registration_epoch)
            && (fallback.path[0] != '\0')
            && (f_open(&file, fallback.path, FA_READ) == FR_OK))
        {
            if (f_lseek(&file, (FSIZE_t)source_offset) == FR_OK)
            {
                const FRESULT read_result = f_read(
                    &file, async->page.frames_interleaved,
                    async->source_bytes, &read);
                const uint8_t cache_clean = (uint8_t)((read == 0U)
                    || (sample_page_cache_clean_loading_payload(
                            &async->token, read) != 0U));
                if ((read_result == FR_OK)
                    && (read == async->source_bytes)
                    && (cache_clean != 0U))
                {
                    async->read_bytes = read;
                    async->load_result = SAMPLE_PAGE_LOAD_OK;
                    async->source = (const uint8_t *)async->page.frames_interleaved;
                }
            }
            (void)f_close(&file);
        }
        async->state = SAMPLE_STREAM_IO_JOB_DATA_READY;
        return 1U;
    }
    /* Deadline streaming stays physical-only; synchronous full imports may
     * use the bounded Storage-side FatFs fallback above. */
    async->load_result = SAMPLE_PAGE_LOAD_READ_FAILED;
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
        PERF_START(finalize_start);
        sample_stream_io_finalize(async);
        PERF_END(PERF_CPU_STREAM_IO_FINALIZE, finalize_start);
        if ((async->load_result == SAMPLE_PAGE_LOAD_OK)
            && (async->perf_dma_done_cycles != 0U))
            brick_perf_wall(PERF_WALL_STREAM_DMA_IO_FINALIZE,
                            brick_perf_now() - async->perf_dma_done_cycles);
        *out_result = (sample_stream_io_result_t){
            .token = async->token,
            .load_result = async->load_result,
            .source_bytes = async->source_bytes,
            .read_bytes = async->read_bytes,
            .request_cycles = async->request_cycles,
        };
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
                &async->source_bytes,
                &physical_reads) == 0U)
        {
            return 0U;
        }
        async->physical_active = 0U;
        async->perf_dma_done_cycles = brick_perf_now();
        async->load_result = physical_result;
        if (physical_result == SAMPLE_PAGE_LOAD_OK)
        {
            async->read_bytes = async->source_bytes;
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

uint32_t sample_stream_io_active_job_count(void)
{
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_JOB_COUNT; ++i)
        if (g_sample_stream_io_async[i].active != 0U) ++count;
    return count;
}

uint8_t sample_stream_io_key_busy(sample_audio_key_t key)
{
    for (uint32_t i = 0U; i < SAMPLE_STREAM_IO_JOB_COUNT; ++i)
        if ((g_sample_stream_io_async[i].active != 0U)
            && (sample_audio_key_equal(
                    &g_sample_stream_io_async[i].token.key,
                    &key) != 0U)) return 1U;
    return 0U;
}

void sample_stream_io_execute_local(const sample_page_load_token_t *token,
                                    uint32_t deadline_margin_us,
                                    sample_stream_io_result_t *out_result)
{
    if (out_result == NULL) return;
    memset(out_result, 0, sizeof(*out_result));
    out_result->load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
    if (token == NULL) return;
    out_result->token = *token;

    /* Bulk callers own the Storage service while using this blocking helper.
     * Refuse to steal a completion from the cooperative Stream manager. */
    if (sample_stream_io_active_job_count() != 0U) return;

    if (sample_stream_io_begin(token, deadline_margin_us) == 0U) return;
    do
    {
        sd_scheduler_runtime_service();
    } while (sample_stream_io_poll(out_result) == 0U);

    if ((out_result->token.slot_index != token->slot_index)
        || (out_result->token.page_generation
            != token->page_generation)
        || (out_result->token.registration_epoch
            != token->registration_epoch)
        || (out_result->token.page_index != token->page_index)
        || (sample_audio_key_equal(&out_result->token.key,
                                   &token->key) == 0U))
    {
        out_result->token = *token;
        out_result->load_result = SAMPLE_PAGE_LOAD_INVALID_ARG;
    }
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
