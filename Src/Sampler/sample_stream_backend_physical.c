#include "Sampler/sample_stream_backend_physical.h"

#include "SD/sd_block_device.h"

#define SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE (512U)

sample_page_load_result_t sample_stream_backend_physical_read_page(
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target,
    sample_stream_physical_cursor_t *cursor,
    uint8_t *scratch,
    uint32_t scratch_capacity,
    const uint8_t **out_source,
    uint32_t *out_source_bytes,
    uint8_t *out_physical_reads)
{
    if ((info == 0) || (target == 0) || (target->frames_interleaved == 0)
        || (scratch == 0) || (out_source == 0) || (out_source_bytes == 0)
        || (out_physical_reads == 0) || (info->info.block_align == 0U)
        || (sample_stream_physical_map_is_current(&info->stream_safe.physical_map) == 0U))
    {
        return SAMPLE_PAGE_LOAD_INVALID_ARG;
    }

    *out_physical_reads = 0U;
    const uint32_t source_bytes = target->frame_count * info->info.block_align;
    if (source_bytes == 0U)
    {
        return SAMPLE_PAGE_LOAD_INVALID_ARG;
    }

    const uint64_t audio_byte_offset =
        (uint64_t)target->start_frame * (uint64_t)info->info.block_align;
    const uint64_t file_byte_offset =
        (uint64_t)info->stream_safe.data_offset_bytes + audio_byte_offset;
    const uint64_t file_end = file_byte_offset + source_bytes;
    if ((file_end < file_byte_offset) || (file_end > info->stream_safe.file_size))
    {
        return SAMPLE_PAGE_LOAD_READ_FAILED;
    }

    uint32_t logical_done = 0U;
    uint32_t scratch_sectors = 0U;
    uint16_t first_sector_skip = 0U;
    while (logical_done < source_bytes)
    {
        sample_stream_physical_span_t span;
        if (sample_stream_physical_map_resolve(&info->stream_safe.physical_map,
                                               file_byte_offset + logical_done,
                                               source_bytes - logical_done,
                                               cursor,
                                               &span) == 0U)
        {
            return SAMPLE_PAGE_LOAD_READ_FAILED;
        }
        const uint64_t scratch_end = ((uint64_t)scratch_sectors + span.sector_count)
                                   * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE;
        if ((scratch_end > scratch_capacity) || (*out_physical_reads == UINT8_MAX))
        {
            return SAMPLE_PAGE_LOAD_READ_FAILED;
        }
        if (logical_done == 0U)
        {
            first_sector_skip = span.first_sector_skip;
        }
        else if (span.first_sector_skip != 0U)
        {
            return SAMPLE_PAGE_LOAD_READ_FAILED;
        }

        const sd_block_device_result_t read_result =
            sd_block_device_read(span.lba,
                                 span.sector_count,
                                 &scratch[scratch_sectors * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE]);
        if (read_result != SD_BLOCK_DEVICE_OK)
        {
            return SAMPLE_PAGE_LOAD_READ_FAILED;
        }
        (*out_physical_reads)++;
        scratch_sectors += span.sector_count;
        logical_done += span.logical_bytes;
    }

    const uint64_t source_end_in_scratch =
        (uint64_t)first_sector_skip + (uint64_t)source_bytes;
    if (source_end_in_scratch > ((uint64_t)scratch_sectors
                                 * SAMPLE_STREAM_PHYSICAL_SECTOR_SIZE))
    {
        return SAMPLE_PAGE_LOAD_READ_FAILED;
    }
    *out_source = &scratch[first_sector_skip];
    *out_source_bytes = source_bytes;
    return SAMPLE_PAGE_LOAD_OK;
}
