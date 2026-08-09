#include "Sampler/sample_stream_backend_physical.h"

#include <string.h>

#include "SD/sd_block_device.h"

#define SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE (512U)

static void sample_stream_backend_physical_pump(
    sample_stream_backend_physical_async_t *async)
{
    while ((async->failed == 0U)
           && (async->logical_queued < async->source_bytes)
           && (sd_block_device_async_pending_count()
               < SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH))
    {
        sample_stream_physical_span_t span;
        if (sample_stream_physical_map_resolve(
                async->map,
                async->file_byte_offset + async->logical_queued,
                async->source_bytes - async->logical_queued,
                async->cursor,
                &span) == 0U)
        {
            async->failed = 1U;
            return;
        }
        const uint64_t scratch_end =
            ((uint64_t)async->scratch_sectors + span.sector_count)
            * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE;
        if (scratch_end > async->scratch_capacity)
        {
            async->failed = 1U;
            return;
        }
        if (async->logical_queued == 0U)
        {
            async->first_sector_skip = span.first_sector_skip;
        }
        else if (span.first_sector_skip != 0U)
        {
            async->failed = 1U;
            return;
        }

        const sd_block_device_result_t queued = sd_block_device_async_enqueue(
            span.lba,
            span.sector_count,
            &async->scratch[async->scratch_sectors
                            * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE]);
        if (queued == SD_BLOCK_DEVICE_QUEUE_FULL)
        {
            return;
        }
        if (queued != SD_BLOCK_DEVICE_OK)
        {
            async->failed = 1U;
            return;
        }
        async->scratch_sectors += span.sector_count;
        async->logical_queued += span.logical_bytes;
    }
}

uint8_t sample_stream_backend_physical_begin(
    sample_stream_backend_physical_async_t *async,
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target,
    sample_stream_physical_cursor_t *cursor,
    uint8_t *scratch,
    uint32_t scratch_capacity)
{
    if ((async == 0) || (info == 0) || (target == 0)
        || (target->frames_interleaved == 0) || (scratch == 0)
        || (info->info.block_align == 0U)
        || (sample_stream_physical_map_is_current(&info->stream_safe.physical_map) == 0U))
    {
        return 0U;
    }

    const uint32_t source_bytes = target->frame_count * info->info.block_align;
    if (source_bytes == 0U)
    {
        return 0U;
    }

    const uint64_t audio_byte_offset =
        (uint64_t)target->start_frame * (uint64_t)info->info.block_align;
    const uint64_t file_byte_offset =
        (uint64_t)info->stream_safe.data_offset_bytes + audio_byte_offset;
    const uint64_t file_end = file_byte_offset + source_bytes;
    if ((file_end < file_byte_offset) || (file_end > info->stream_safe.file_size))
    {
        return 0U;
    }
    if (sd_block_device_async_pending_count() != 0U)
    {
        return 0U;
    }

    memset(async, 0, sizeof(*async));
    async->map = &info->stream_safe.physical_map;
    async->cursor = cursor;
    async->scratch = scratch;
    async->file_byte_offset = file_byte_offset;
    async->scratch_capacity = scratch_capacity;
    async->source_bytes = source_bytes;
    async->active = 1U;
    sample_stream_backend_physical_pump(async);
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
        async->failed = 1U;
    }

    sd_block_device_async_poll();
    sd_block_device_async_completion_t completion;
    while (sd_block_device_async_take_completion(&completion) != 0U)
    {
        if (completion.result != SD_BLOCK_DEVICE_OK)
        {
            async->failed = 1U;
        }
        else if (async->physical_reads != UINT8_MAX)
        {
            async->physical_reads++;
        }
        else
        {
            async->failed = 1U;
        }
    }
    if (async->failed != 0U)
    {
        sd_block_device_async_cancel();
        async->active = 0U;
        *out_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        *out_physical_reads = async->physical_reads;
        return 1U;
    }

    sample_stream_backend_physical_pump(async);
    if ((async->logical_queued < async->source_bytes)
        || (sd_block_device_async_pending_count() != 0U))
    {
        return 0U;
    }

    const uint64_t source_end_in_scratch =
        (uint64_t)async->first_sector_skip + (uint64_t)async->source_bytes;
    if (source_end_in_scratch > ((uint64_t)async->scratch_sectors
                                 * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE))
    {
        async->active = 0U;
        *out_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        return 1U;
    }
    *out_source = &async->scratch[async->first_sector_skip];
    *out_source_bytes = async->source_bytes;
    *out_physical_reads = async->physical_reads;
    *out_result = SAMPLE_PAGE_LOAD_OK;
    async->active = 0U;
    return 1U;
}

void sample_stream_backend_physical_cancel(
    sample_stream_backend_physical_async_t *async)
{
    sd_block_device_async_cancel();
    if (async != 0)
    {
        memset(async, 0, sizeof(*async));
    }
}
