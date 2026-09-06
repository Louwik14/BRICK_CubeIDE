#include "Sampler/sample_stream_backend_physical.h"

#include <string.h>

#include "SD/sd_block_device.h"
#include "stm32h7xx_hal.h"

#define SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE (512U)

static sample_stream_backend_physical_async_t *g_sample_stream_physical_pending;

static uint8_t sample_stream_backend_physical_next_span(
    sample_stream_backend_physical_async_t *async,
    sample_stream_physical_span_t *span)
{
    if ((async == 0) || (span == 0)
            || (async->logical_queued >= async->source_bytes)
            || (sample_stream_physical_map_resolve(
                    async->map,
                    async->file_byte_offset + async->logical_queued,
                    async->source_bytes - async->logical_queued,
                    async->cursor,
                    span) == 0U))
    {
        return 0U;
    }
    const uint64_t scratch_end =
        ((uint64_t)async->scratch_sectors + span->sector_count)
        * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE;
    if ((scratch_end > async->scratch_capacity)
            || ((async->logical_queued != 0U)
                && (span->first_sector_skip != 0U)))
    {
        return 0U;
    }
    return 1U;
}

uint8_t sample_stream_backend_physical_begin(
    sample_stream_backend_physical_async_t *async,
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target,
    sample_stream_physical_cursor_t *cursor,
    uint8_t *scratch,
    uint32_t scratch_capacity,
    uint32_t deadline_margin_us,
    uint8_t storage_owner)
{
    if ((async == 0) || (info == 0) || (target == 0)
        || (target->frames_interleaved == 0) || (scratch == 0)
        || (info->info.block_align == 0U)
        || (g_sample_stream_physical_pending != 0)
        || (sample_stream_physical_map_is_current(
                &info->stream_safe.physical_map) == 0U))
    {
        return 0U;
    }

    const uint32_t source_bytes = target->frame_count * info->info.block_align;
    const uint64_t audio_byte_offset =
        (uint64_t)target->start_frame * (uint64_t)info->info.block_align;
    const uint64_t file_byte_offset =
        (uint64_t)info->stream_safe.data_offset_bytes + audio_byte_offset;
    const uint64_t file_end = file_byte_offset + source_bytes;
    if ((source_bytes == 0U) || (file_end < file_byte_offset)
            || (file_end > info->stream_safe.file_size))
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
    async->deadline_margin_us = deadline_margin_us;
    async->storage_owner = storage_owner;
    async->deadline_started_ms = HAL_GetTick();
    async->active = 1U;
    g_sample_stream_physical_pending = async;
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
        async->completed = 1U;
    }
    if (async->completed == 0U)
    {
        return 0U;
    }
    if (g_sample_stream_physical_pending == async)
    {
        g_sample_stream_physical_pending = 0;
    }
    async->active = 0U;
    *out_physical_reads = async->physical_reads;
    if (async->failed != 0U)
    {
        *out_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        return 1U;
    }

    const uint64_t source_end_in_scratch =
        (uint64_t)async->first_sector_skip + (uint64_t)async->source_bytes;
    if (source_end_in_scratch > ((uint64_t)async->scratch_sectors
                                 * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE))
    {
        *out_result = SAMPLE_PAGE_LOAD_READ_FAILED;
        return 1U;
    }
    *out_source = &async->scratch[async->first_sector_skip];
    *out_source_bytes = async->source_bytes;
    *out_result = SAMPLE_PAGE_LOAD_OK;
    return 1U;
}

void sample_stream_backend_physical_cancel(
    sample_stream_backend_physical_async_t *async)
{
    if ((async != 0) && (g_sample_stream_physical_pending == async))
    {
        sd_block_device_async_cancel();
        g_sample_stream_physical_pending = 0;
    }
    if (async != 0)
    {
        memset(async, 0, sizeof(*async));
    }
}

static uint8_t sample_stream_backend_physical_read_peek(
    void *context,
    sd_scheduler_candidate_t *candidate)
{
    (void)context;
    sample_stream_backend_physical_async_t *const async =
        g_sample_stream_physical_pending;
    sample_stream_physical_span_t span;
    if ((candidate == 0) || (async == 0) || (async->active == 0U)
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
    candidate->read_buffer = &async->scratch[
        async->scratch_sectors * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE];
    candidate->media_epoch = async->map->media_epoch;
    return 1U;
}

static sd_scheduler_start_result_t sample_stream_backend_physical_read_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    (void)context;
    sample_stream_backend_physical_async_t *const async =
        g_sample_stream_physical_pending;
    sample_stream_physical_span_t span;
    if ((candidate == 0) || (async == 0)
            || (sample_stream_backend_physical_next_span(async, &span) == 0U)
            || (candidate->lba != span.lba)
            || (granted_sector_count != span.sector_count))
    {
        return SD_SCHEDULER_START_ERROR;
    }
    const sd_block_device_result_t result = sd_block_device_async_enqueue(
        span.lba, span.sector_count,
        &async->scratch[async->scratch_sectors
                        * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE],
        (storage_io_owner_t)async->storage_owner);
    if ((result == SD_BLOCK_DEVICE_BUSY)
            || (result == SD_BLOCK_DEVICE_QUEUE_FULL))
    {
        return SD_SCHEDULER_START_BUSY;
    }
    if (result != SD_BLOCK_DEVICE_OK)
    {
        async->failed = 1U;
        async->completed = 1U;
        return SD_SCHEDULER_START_ERROR;
    }
    async->active_lba = span.lba;
    async->active_sector_count = span.sector_count;
    async->active_buffer = &async->scratch[
        async->scratch_sectors * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE];
    if (async->logical_queued == 0U)
    {
        async->first_sector_skip = span.first_sector_skip;
    }
    async->scratch_sectors += span.sector_count;
    async->logical_queued += span.logical_bytes;
    return SD_SCHEDULER_START_STARTED;
}

static sd_scheduler_poll_result_t sample_stream_backend_physical_read_poll(
    void *context)
{
    (void)context;
    sample_stream_backend_physical_async_t *const async =
        g_sample_stream_physical_pending;
    if (async == 0)
    {
        return SD_SCHEDULER_POLL_ERROR;
    }
    sd_block_device_async_poll();
    sd_block_device_async_completion_t completion;
    if (sd_block_device_async_take_completion(&completion) == 0U)
    {
        return (sd_block_device_async_hardware_state()
                == SD_BLOCK_DEVICE_HW_ABORTING)
            ? SD_SCHEDULER_POLL_RECOVERY_ABORT : SD_SCHEDULER_POLL_ACTIVE;
    }
    if ((completion.result != SD_BLOCK_DEVICE_OK)
            || (completion.operation != SD_BLOCK_DEVICE_OPERATION_READ)
            || (completion.lba != async->active_lba)
            || (completion.sector_count != async->active_sector_count)
            || (completion.dst != async->active_buffer)
            || (completion.media_epoch != async->map->media_epoch))
    {
        async->failed = 1U;
        async->completed = 1U;
        return SD_SCHEDULER_POLL_ERROR;
    }
    async->active_lba = 0U;
    async->active_sector_count = 0U;
    async->active_buffer = 0;
    if (async->physical_reads != UINT8_MAX)
    {
        async->physical_reads++;
    }
    else
    {
        async->failed = 1U;
        async->completed = 1U;
        return SD_SCHEDULER_POLL_ERROR;
    }
    if (async->logical_queued < async->source_bytes)
    {
        return SD_SCHEDULER_POLL_COMPLETED;
    }
    async->completed = 1U;
    return SD_SCHEDULER_POLL_COMPLETED;
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
    return (g_sample_stream_physical_pending != 0) ? 1U : 0U;
}
