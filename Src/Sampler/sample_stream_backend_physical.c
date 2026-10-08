#include "Sampler/sample_stream_backend_physical.h"
#include "Platform/stream_rec_perf.h"

#include <string.h>

#include "SD/sd_block_device.h"
#include "SD/sdmmc_async_transport.h"
#include "stm32h7xx_hal.h"
#include "Sampler/sample_stream_io.h"

#define SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE (512U)
#define SAMPLE_STREAM_PHYSICAL_PENDING_COUNT (2U)

_Static_assert(SAMPLE_PAGE_BYTES == SDMMC_ASYNC_PROGRESS_PAGE_BYTES,
               "page-cache and progressive transport geometry must match");
_Static_assert(SAMPLE_STREAM_FASTPATH_MAX_BYTES
                == SDMMC_ASYNC_PROGRESS_MAX_BYTES,
               "fastpath and SDMMC burst geometry must match");
_Static_assert((SDMMC_ASYNC_PROGRESS_CHUNK_BYTES
                % SAMPLE_PAGE_BYTES_PER_FRAME) == 0U,
               "a progressive chunk must contain complete stereo frames");
_Static_assert((SDMMC_ASYNC_PROGRESS_CHUNK_BYTES
                / SAMPLE_PAGE_BYTES_PER_FRAME) == 64U,
               "a 512-byte stereo chunk must publish 64 frames");

static sample_stream_backend_physical_async_t
    *g_sample_stream_physical_pending[SAMPLE_STREAM_PHYSICAL_PENDING_COUNT];
static uint32_t g_sample_stream_physical_next_generation = 1U;

static int32_t sample_stream_backend_physical_find(
    const sample_stream_backend_physical_async_t *async)
{
    for(uint32_t i = 0U; i < SAMPLE_STREAM_PHYSICAL_PENDING_COUNT; ++i)
    {
        if(g_sample_stream_physical_pending[i] == async) return (int32_t)i;
    }
    return -1;
}

static sample_stream_backend_physical_async_t *
sample_stream_backend_physical_find_request(
    const sd_block_device_async_request_t *request)
{
    for(uint32_t i = 0U; i < SAMPLE_STREAM_PHYSICAL_PENDING_COUNT; ++i)
    {
        sample_stream_backend_physical_async_t *const async =
            g_sample_stream_physical_pending[i];
        if((async != 0) && (&async->request == request)) return async;
    }
    return 0;
}

static uint8_t sample_stream_backend_physical_progress_isr(
    void *context, uint32_t available_bytes)
{
    sample_stream_backend_physical_async_t *const async = context;
    if((async == 0) || (async->active == 0U)
        || (async->progressive_enabled == 0U)
        || ((available_bytes % SAMPLE_PAGE_BYTES_PER_FRAME) != 0U)
        || (available_bytes > async->source_bytes))
    {
        return 0U;
    }
    const uint32_t completed_byte = available_bytes - 1U;
    const uint32_t page = completed_byte / SAMPLE_PAGE_BYTES;
    if (page >= async->page_count) return 0U;
    const uint32_t page_bytes = available_bytes
        - (page * SAMPLE_PAGE_BYTES);
    return sample_page_cache_publish_loading_progress_isr(
        &async->page_token[page],
        page_bytes / SAMPLE_PAGE_BYTES_PER_FRAME);
}

static uint8_t *sample_stream_backend_physical_chunk_address_isr(
    void *context, uint32_t chunk_index)
{
    sample_stream_backend_physical_async_t *const async = context;
    const uint32_t chunks_per_page =
        SAMPLE_PAGE_BYTES / SDMMC_ASYNC_PROGRESS_CHUNK_BYTES;
    const uint32_t page = chunk_index / chunks_per_page;
    const uint32_t page_chunk = chunk_index % chunks_per_page;
    if ((async == 0) || (page >= async->page_count)
        || (async->page_buffer[page] == 0)) return 0;
    return async->page_buffer[page]
        + (page_chunk * SDMMC_ASYNC_PROGRESS_CHUNK_BYTES);
}

static uint8_t sample_stream_backend_physical_resolve_contiguous(
    const sample_stream_physical_map_t *map,
    uint64_t file_byte_offset,
    uint32_t requested_bytes,
    sample_stream_physical_cursor_t *cursor,
    sample_stream_physical_span_t *out_span)
{
    if ((out_span == 0) || (requested_bytes == 0U)) return 0U;
    sample_stream_physical_span_t part;
    uint32_t resolved = 0U;
    uint32_t sectors = 0U;
    uint32_t expected_lba = 0U;
    uint16_t first_extent = 0U;
    while (resolved < requested_bytes)
    {
        if (sample_stream_physical_map_resolve(
                map, file_byte_offset + resolved, requested_bytes - resolved,
                cursor, &part) == 0U) return 0U;
        if ((part.first_sector_skip != 0U)
            || (part.logical_bytes == 0U)
            || ((part.logical_bytes % SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE) != 0U))
            return 0U;
        if (resolved == 0U)
        {
            *out_span = part;
            first_extent = part.extent_index;
        }
        else if (part.lba != expected_lba)
        {
            return 0U;
        }
        resolved += part.logical_bytes;
        sectors += part.sector_count;
        expected_lba = part.lba + part.sector_count;
    }
    out_span->sector_count = sectors;
    out_span->logical_bytes = resolved;
    out_span->first_sector_skip = 0U;
    out_span->extent_index = first_extent;
    return 1U;
}

uint8_t sample_stream_backend_physical_contiguous_page_count(
    const sample_stream_safe_metadata_t *metadata,
    uint32_t start_frame,
    uint8_t requested_pages)
{
    if ((metadata == 0)
        || (metadata->block_align != SAMPLE_PAGE_BYTES_PER_FRAME)
        || (requested_pages == 0U)
        || (requested_pages > SAMPLE_STREAM_FASTPATH_MAX_PAGES)) return 0U;
    const uint64_t file_offset = (uint64_t)metadata->data_offset_bytes
        + (uint64_t)start_frame * metadata->block_align;
    if ((file_offset % SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE) != 0U) return 0U;
    for (uint8_t pages = requested_pages; pages != 0U; --pages)
    {
        const uint32_t bytes = pages * SAMPLE_PAGE_BYTES;
        sample_stream_physical_cursor_t cursor = {0};
        sample_stream_physical_span_t span;
        if ((file_offset + bytes <= metadata->file_size)
            && (sample_stream_backend_physical_resolve_contiguous(
                    &metadata->physical_map, file_offset, bytes,
                    &cursor, &span) != 0U)
            && (span.logical_bytes == bytes)
            && (span.sector_count
                == bytes / SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE))
            return pages;
    }
    return 0U;
}

static void sample_stream_backend_physical_invalidate_span(
    sample_stream_backend_physical_async_t *async)
{
    if (async != 0)
    {
        async->current_span_valid = 0U;
    }
}

static uint8_t sample_stream_backend_physical_next_span_impl(
    sample_stream_backend_physical_async_t *async,
    sample_stream_physical_span_t *span)
{
    if ((async == 0) || (span == 0))
    {
        return 0U;
    }

    if ((async->logical_queued >= async->source_bytes)
        || (sample_stream_physical_map_is_current(async->map) == 0U))
    {
        sample_stream_backend_physical_invalidate_span(async);
        return 0U;
    }

    if (async->current_span_valid != 0U)
    {
        *span = async->current_span;
    }
    else
    {
        const uint64_t file_byte_offset =
            async->file_byte_offset + async->logical_queued;
        const uint32_t requested_bytes =
            async->source_bytes - async->logical_queued;
        const uint8_t resolved = (async->scatter_enabled != 0U)
            ? sample_stream_backend_physical_resolve_contiguous(
                async->map, file_byte_offset, requested_bytes,
                async->cursor, span)
            : sample_stream_physical_map_resolve(
                async->map, file_byte_offset, requested_bytes,
                async->cursor, span);
        if (resolved == 0U)
        {
            return 0U;
        }
    }

    const uint64_t buffer_end =
        ((uint64_t)async->buffer_sectors + span->sector_count)
        * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE;
    if ((buffer_end > async->buffer_capacity)
            || ((async->logical_queued != 0U)
                && (span->first_sector_skip != 0U)))
    {
        sample_stream_backend_physical_invalidate_span(async);
        return 0U;
    }

    if (async->current_span_valid == 0U)
    {
        async->current_span = *span;
        async->current_span_valid = 1U;
    }
    return 1U;
}

static uint8_t sample_stream_backend_physical_next_span(
    sample_stream_backend_physical_async_t *async,
    sample_stream_physical_span_t *span)
{
    const uint8_t result =
        sample_stream_backend_physical_next_span_impl(async, span);
    return result;
}

uint8_t sample_stream_backend_physical_begin(
    sample_stream_backend_physical_async_t *async,
    const sample_stream_safe_metadata_t *metadata,
    const sample_page_load_target_t *target,
    uint8_t page_count,
    sample_stream_physical_cursor_t *cursor,
    uint8_t *buffer,
    uint32_t buffer_capacity,
    uint8_t destination_cpu_clean,
    uint32_t deadline_margin_us)
{
    if ((async != 0) && (sample_stream_backend_physical_find(async) < 0))
    {
        sample_stream_backend_physical_invalidate_span(async);
    }
    if ((async == 0) || (metadata == 0) || (target == 0)
        || (buffer == 0)
        || (page_count == 0U)
        || (page_count > SAMPLE_STREAM_FASTPATH_MAX_PAGES)
        || (metadata->block_align == 0U)
        || (sample_stream_physical_map_is_current(
                &metadata->physical_map) == 0U))
    {
        return 0U;
    }

    uint32_t source_bytes = 0U;
    for (uint8_t page = 0U; page < page_count; ++page)
    {
        const sample_page_load_target_t *const current = &target[page];
        const uint32_t current_bytes = current->frame_count
            * metadata->block_align;
        if ((current_bytes == 0U)
            || ((page != 0U)
                && ((current->page_index != target[0].page_index + page)
                    || (current->start_frame
                        != target[0].start_frame
                            + page * target[0].frames_per_page)
                    || (sample_audio_key_equal(
                            &current->key, &target[0].key) == 0U)
                    || (current->registration_epoch
                        != target[0].registration_epoch))))
            return 0U;
        source_bytes += current_bytes;
    }
    const uint64_t audio_byte_offset =
        (uint64_t)target->start_frame * (uint64_t)metadata->block_align;
    const uint64_t file_byte_offset =
        (uint64_t)metadata->data_offset_bytes + audio_byte_offset;
    const uint64_t file_end = file_byte_offset + source_bytes;
    if ((source_bytes == 0U) || (file_end < file_byte_offset)
            || (file_end > metadata->file_size))
    {
        return 0U;
    }

    int32_t pending_slot = -1;
    for(uint32_t i = 0U; i < SAMPLE_STREAM_PHYSICAL_PENDING_COUNT; ++i)
    {
        if(g_sample_stream_physical_pending[i] == 0)
        {
            pending_slot = (int32_t)i;
            break;
        }
    }
    if(pending_slot < 0) return 0U;

    memset(async, 0, sizeof(*async));
    async->map = &metadata->physical_map;
    async->cursor = cursor;
    async->buffer = buffer;
    async->page_count = page_count;
    async->scatter_enabled = (page_count > 1U) ? 1U : 0U;
    async->file_byte_offset = file_byte_offset;
    async->buffer_capacity = buffer_capacity;
    async->source_bytes = source_bytes;
    for (uint8_t page = 0U; page < page_count; ++page)
    {
        async->page_buffer[page] =
            (uint8_t *)target[page].frames_interleaved;
        async->page_token[page] = (sample_page_load_token_t){
            .key = target[page].key,
            .page_index = target[page].page_index,
            .page_generation = target[page].page_generation,
            .registration_epoch = target[page].registration_epoch,
            .slot_index = target[page].slot_index,
        };
    }
    async->progressive_enabled = (uint8_t)(
        (target->format == SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED)
        && (target->stride_floats == SAMPLE_PAGE_FRAME_STRIDE_FLOATS)
        && (target->frame_count == SAMPLE_PAGE_FRAMES)
        && ((source_bytes % SAMPLE_PAGE_BYTES) == 0U)
        && (metadata->block_align == SAMPLE_PAGE_BYTES_PER_FRAME)
        && ((file_byte_offset % SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE) == 0U));
    async->destination_cpu_clean = destination_cpu_clean;
    async->count_multi_diag = (target->key.domain == SAMPLE_AUDIO_DOMAIN_MULTI);
    if (page_count > 1U)
    {
        sample_stream_physical_span_t span;
        if ((async->progressive_enabled == 0U)
            || (source_bytes != page_count * SAMPLE_PAGE_BYTES)
            || (sample_stream_backend_physical_resolve_contiguous(
                    async->map, file_byte_offset, source_bytes,
                    async->cursor, &span) == 0U))
        {
            memset(async, 0, sizeof(*async));
            return 0U;
        }
        async->current_span = span;
        async->current_span_valid = 1U;
    }
    async->deadline_margin_us = deadline_margin_us;
    async->deadline_started_ms = HAL_GetTick();
    async->owner_generation = g_sample_stream_physical_next_generation++;
    if(async->owner_generation == 0U)
    {
        async->owner_generation = g_sample_stream_physical_next_generation++;
    }
    async->active = 1U;
    g_sample_stream_physical_pending[(uint32_t)pending_slot] = async;
    return 1U;
}

uint8_t sample_stream_backend_physical_poll(
    sample_stream_backend_physical_async_t *async,
    sample_page_load_result_t *out_result,
    const uint8_t **out_source,
    uint32_t *out_source_bytes,
    uint8_t *out_physical_reads)
{
    if ((async == 0) || (out_result == 0) || (out_source == 0)
        || (out_source_bytes == 0) || (out_physical_reads == 0)
        || (async->active == 0U))
    {
        return 0U;
    }
    if (sample_stream_physical_map_is_current(async->map) == 0U)
    {
        sample_stream_backend_physical_invalidate_span(async);
        async->failed = 1U;
        async->completed = 1U;
    }
    if (async->completed == 0U)
    {
        return 0U;
    }
    const int32_t pending_slot = sample_stream_backend_physical_find(async);
    if(pending_slot >= 0)
    {
        g_sample_stream_physical_pending[(uint32_t)pending_slot] = 0;
    }
    async->active = 0U;
    *out_physical_reads = async->physical_reads;
    if (async->failed != 0U)
    {
        *out_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        return 1U;
    }

    const uint64_t source_end_in_buffer =
        (uint64_t)async->first_sector_skip + (uint64_t)async->source_bytes;
    if (source_end_in_buffer > ((uint64_t)async->buffer_sectors
                                 * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE))
    {
        sample_stream_backend_physical_invalidate_span(async);
        *out_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        return 1U;
    }
    *out_source = &async->buffer[async->first_sector_skip];
    *out_source_bytes = async->source_bytes;
    *out_result = SAMPLE_PAGE_LOAD_OK;
    return 1U;
}

void sample_stream_backend_physical_cancel(
    sample_stream_backend_physical_async_t *async)
{
    if (async == 0)
    {
        return;
    }
    sample_stream_backend_physical_invalidate_span(async);
    if(sample_stream_backend_physical_find(async) >= 0)
    {
        async->cancel_requested = 1U;
        async->failed = 1U;
        if (async->request.queued == 0U)
        {
            async->completed = 1U;
        }
        else
        {
            (void)sd_block_device_async_abort_generation(
                async->owner_generation);
        }
    }
}

static uint8_t sample_stream_backend_physical_read_peek(
    void *context,
    sd_scheduler_candidate_t *candidate)
{
    (void)context;
    sample_stream_backend_physical_async_t *async = 0;
    for(uint32_t i = 0U; i < SAMPLE_STREAM_PHYSICAL_PENDING_COUNT; ++i)
    {
        sample_stream_backend_physical_async_t *const candidate_async =
            g_sample_stream_physical_pending[i];
        if((candidate_async != 0) && (candidate_async->active != 0U)
                && (candidate_async->cancel_requested == 0U)
                && (candidate_async->completed == 0U)
                && (candidate_async->request.queued == 0U)
                && ((async == 0)
                    || ((candidate_async->page_count == 1U)
                        && (async->page_count > 1U))
                    || ((((candidate_async->page_count == 1U)
                           == (async->page_count == 1U)))
                        && (candidate_async->owner_generation
                            < async->owner_generation))))
        {
            async = candidate_async;
        }
    }
    sample_stream_physical_span_t span;
    if ((candidate == 0) || (async == 0) || (async->active == 0U)
            || (async->cancel_requested != 0U)
            || (async->completed != 0U)
            || (sample_stream_backend_physical_next_span(async, &span) == 0U))
    {
        return 0U;
    }
    memset(candidate, 0, sizeof(*candidate));
    candidate->type = SD_SCHEDULER_CLASS_READ;
    candidate->ready = 1U;
    if (async->deadline_margin_us == UINT32_MAX)
    {
        candidate->margin_us = UINT32_MAX;
    }
    else
    {
        const uint32_t elapsed_ms = HAL_GetTick() - async->deadline_started_ms;
        const uint64_t elapsed_us = (uint64_t)elapsed_ms * 1000U;
        candidate->margin_us = (elapsed_us >= async->deadline_margin_us)
            ? 0U : async->deadline_margin_us - (uint32_t)elapsed_us;
    }
    candidate->estimated_cost_us = span.sector_count * 250U;
    candidate->lba = span.lba;
    candidate->sector_count = span.sector_count;
    candidate->read_buffer = &async->buffer[
        async->buffer_sectors * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE];
    candidate->owner_context = async;
    candidate->media_epoch = async->map->media_epoch;
    candidate->owner_generation = async->owner_generation;
    return 1U;
}

static sd_scheduler_start_result_t sample_stream_backend_physical_read_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    (void)context;
    sample_stream_backend_physical_async_t *const async =
        (candidate != 0) ? candidate->owner_context : 0;
    sample_stream_physical_span_t span;
    if ((candidate == 0) || (async == 0)
            || (sample_stream_backend_physical_find(async) < 0)
            || (candidate->owner_generation != async->owner_generation)
            || (sample_stream_backend_physical_next_span(async, &span) == 0U)
            || (candidate->lba != span.lba)
            || (granted_sector_count != span.sector_count))
    {
        sample_stream_backend_physical_invalidate_span(async);
        return SD_SCHEDULER_START_ERROR;
    }
    PERF_START(read_start);
    const uint8_t progressive = (uint8_t)(
        (async->progressive_enabled != 0U)
        && (async->logical_queued == 0U)
        && (span.first_sector_skip == 0U)
        && (span.sector_count == (async->source_bytes
                                  / SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE))
        && (span.logical_bytes == async->source_bytes));
    const sd_block_device_result_t result = (progressive != 0U)
        ? ((async->scatter_enabled != 0U)
            ? sd_block_device_async_read_submit_progressive_scatter_request(
                &async->request, span.lba, span.sector_count,
                async->page_buffer[0], async->owner_generation,
                async->destination_cpu_clean,
                sample_stream_backend_physical_progress_isr, async,
                sample_stream_backend_physical_chunk_address_isr, async)
            : sd_block_device_async_read_submit_progressive_request(
            &async->request, span.lba, span.sector_count,
            &async->buffer[async->buffer_sectors
                            * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE],
            async->owner_generation, async->destination_cpu_clean,
            sample_stream_backend_physical_progress_isr, async))
        : sd_block_device_async_read_submit_request(
            &async->request, span.lba, span.sector_count,
            &async->buffer[async->buffer_sectors
                            * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE],
            async->owner_generation, async->destination_cpu_clean);
    PERF_END(PERF_CPU_STREAM_READ_START, read_start);
    if ((result == SD_BLOCK_DEVICE_BUSY)
            || (result == SD_BLOCK_DEVICE_QUEUE_FULL))
    {
        return SD_SCHEDULER_START_BUSY;
    }
    if (result != SD_BLOCK_DEVICE_OK)
    {
        sample_stream_backend_physical_invalidate_span(async);
        async->failed = 1U;
        async->completed = 1U;
        return SD_SCHEDULER_START_ERROR;
    }
    if (async->logical_queued == 0U)
    {
        async->first_sector_skip = span.first_sector_skip;
    }
    async->buffer_sectors += span.sector_count;
    async->logical_queued += span.logical_bytes;
    sample_stream_backend_physical_invalidate_span(async);
    return SD_SCHEDULER_START_STARTED;
}

static sd_scheduler_poll_result_t sample_stream_backend_physical_read_poll(
    void *context)
{
    (void)context;
    sd_block_device_async_request_t *completion = 0;
    if (sd_block_device_async_take_completion_request(&completion) == 0U)
    {
        if (sd_block_device_async_hardware_state()
                == SD_BLOCK_DEVICE_HW_ABORTING)
        {
            for(uint32_t i = 0U; i < SAMPLE_STREAM_PHYSICAL_PENDING_COUNT; ++i)
            {
                sample_stream_backend_physical_invalidate_span(
                    g_sample_stream_physical_pending[i]);
            }
            return SD_SCHEDULER_POLL_RECOVERY_ABORT;
        }
        return SD_SCHEDULER_POLL_ACTIVE;
    }
    PERF_START(read_complete);
    sample_stream_backend_physical_async_t *const async =
        sample_stream_backend_physical_find_request(completion);
    if(async == 0)
    {
        PERF_END(PERF_CPU_STREAM_READ_COMPLETE, read_complete);
        return (sd_block_device_async_pending_count() != 0U)
            ? SD_SCHEDULER_POLL_ACTIVE : SD_SCHEDULER_POLL_ERROR;
    }
    if ((completion != &async->request)
            || (completion->owner_generation != async->owner_generation)
            || (completion->result != SD_BLOCK_DEVICE_OK)
            || (completion->operation != SD_BLOCK_DEVICE_OPERATION_READ)
            || (completion->sector_count == 0U)
            || (completion->sector_count > async->buffer_sectors)
            || (completion->buffer != ((async->scatter_enabled != 0U)
                    ? async->page_buffer[0]
                    : &async->buffer[
                        (async->buffer_sectors - completion->sector_count)
                        * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE]))
            || (completion->media_epoch != async->map->media_epoch))
    {
        sample_stream_backend_physical_invalidate_span(async);
        async->failed = 1U;
        async->completed = 1U;
        PERF_END(PERF_CPU_STREAM_READ_COMPLETE, read_complete);
        return (sd_block_device_async_pending_count() != 0U)
            ? SD_SCHEDULER_POLL_ACTIVE : SD_SCHEDULER_POLL_ERROR;
    }
    const uint32_t physical_bytes = completion->sector_count
        * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE;
    (void)physical_bytes;
    PERF_COUNT(PERF_N_READS);
    PERF_ACCUM(PERF_N_READ_BYTES, physical_bytes);
    PERF_MIN_NONZERO(PERF_N_READ_MIN_BYTES, physical_bytes);
    PERF_MAX(PERF_N_READ_MAX_BYTES, physical_bytes);
    if (async->physical_reads != UINT8_MAX)
    {
        async->physical_reads++;
    }
    else
    {
        sample_stream_backend_physical_invalidate_span(async);
        async->failed = 1U;
        async->completed = 1U;
        PERF_END(PERF_CPU_STREAM_READ_COMPLETE, read_complete);
        return (sd_block_device_async_pending_count() != 0U)
            ? SD_SCHEDULER_POLL_ACTIVE : SD_SCHEDULER_POLL_ERROR;
    }
    if (async->count_multi_diag != 0U)
    {
    }
    if (async->logical_queued < async->source_bytes)
    {
        PERF_END(PERF_CPU_STREAM_READ_COMPLETE, read_complete);
        return (sd_block_device_async_pending_count() != 0U)
            ? SD_SCHEDULER_POLL_ACTIVE : SD_SCHEDULER_POLL_COMPLETED;
    }
    async->completed = 1U;
    PERF_END(PERF_CPU_STREAM_READ_COMPLETE, read_complete);
    return (sd_block_device_async_pending_count() != 0U)
        ? SD_SCHEDULER_POLL_ACTIVE : SD_SCHEDULER_POLL_COMPLETED;
}

sd_scheduler_provider_t sample_stream_backend_physical_read_provider(void)
{
    const sd_scheduler_provider_t provider = {
        .context = 0,
        .peek = sample_stream_backend_physical_read_peek,
        .start = sample_stream_backend_physical_read_start,
        .poll = sample_stream_backend_physical_read_poll,
    };
    return provider;
}

uint8_t sample_stream_backend_physical_busy(void)
{
    uint8_t busy = 0U;
    for(uint32_t i = 0U; i < SAMPLE_STREAM_PHYSICAL_PENDING_COUNT; ++i)
    {
        sample_stream_backend_physical_async_t *const async =
            g_sample_stream_physical_pending[i];
        if((async != 0) && (async->cancel_requested != 0U)
                && (async->completed != 0U))
        {
            async->active = 0U;
            g_sample_stream_physical_pending[i] = 0;
        }
        else if(async != 0)
        {
            busy = 1U;
        }
    }
    return busy;
}

uint8_t sample_stream_backend_physical_submitted(
    const sample_stream_backend_physical_async_t *async)
{
    return ((async != 0)
        && ((async->logical_queued != 0U)
            || (async->request.queued != 0U)
            || (async->request.started != 0U))) ? 1U : 0U;
}
