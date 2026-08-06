#include "Sampler/sample_stream_backend_contiguous.h"

#include "SD/sd_block_device.h"

#define SAMPLE_STREAM_CONTIG_SECTOR_SIZE       (512U)
#define SAMPLE_STREAM_CONTIG_SCRATCH_SECTORS   \
    ((SAMPLE_PAGE_BYTES + (SAMPLE_STREAM_CONTIG_SECTOR_SIZE - 1U) \
      + (SAMPLE_STREAM_CONTIG_SECTOR_SIZE - 1U)) / SAMPLE_STREAM_CONTIG_SECTOR_SIZE)
#define SAMPLE_STREAM_CONTIG_SCRATCH_BYTES \
    (SAMPLE_STREAM_CONTIG_SECTOR_SIZE * SAMPLE_STREAM_CONTIG_SCRATCH_SECTORS)

sample_page_load_result_t sample_stream_backend_contiguous_read_page(
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target,
    uint8_t *scratch,
    uint32_t scratch_capacity,
    const uint8_t **out_source,
    uint32_t *out_source_bytes)
{
    if ((info == 0) || (target == 0) || (target->frames_interleaved == 0)
        || (scratch == 0) || (out_source == 0) || (out_source_bytes == 0)
        || (info->stream_safe.valid == 0U)
        || (info->stream_safe.backend_kind != (uint8_t)SAMPLE_STREAM_BACKEND_SAFE_CONTIGUOUS)
        || (info->stream_safe.contiguous == 0U)
        || (info->info.block_align == 0U))
    {
        return SAMPLE_PAGE_LOAD_INVALID_ARG;
    }

    const uint32_t source_bytes = target->frame_count * info->info.block_align;
    if (source_bytes == 0U)
    {
        return SAMPLE_PAGE_LOAD_INVALID_ARG;
    }

    const uint64_t audio_byte_offset =
        (uint64_t)target->start_frame * (uint64_t)info->info.block_align;
    const uint64_t file_byte_offset =
        (uint64_t)info->stream_safe.data_offset_bytes + audio_byte_offset;
    const uint64_t file_end = file_byte_offset + (uint64_t)source_bytes;
    if ((file_end > (uint64_t)info->stream_safe.file_size)
        || (file_end > ((uint64_t)UINT32_MAX + 1ULL)))
    {
        return SAMPLE_PAGE_LOAD_READ_FAILED;
    }

    const uint32_t sector_offset =
        (uint32_t)file_byte_offset & (SAMPLE_STREAM_CONTIG_SECTOR_SIZE - 1U);
    const uint32_t read_bytes =
        (sector_offset + source_bytes + (SAMPLE_STREAM_CONTIG_SECTOR_SIZE - 1U))
        & ~(SAMPLE_STREAM_CONTIG_SECTOR_SIZE - 1U);
    const uint32_t sector_count = read_bytes / SAMPLE_STREAM_CONTIG_SECTOR_SIZE;
    if ((sector_count == 0U) || (sector_count > SAMPLE_STREAM_CONTIG_SCRATCH_SECTORS)
        || (read_bytes > scratch_capacity))
    {
        return SAMPLE_PAGE_LOAD_READ_FAILED;
    }

    const uint32_t lba = info->stream_safe.first_file_lba
                       + (uint32_t)(file_byte_offset / SAMPLE_STREAM_CONTIG_SECTOR_SIZE);

    const sd_block_device_result_t read_result =
        sd_block_device_read(lba, sector_count, scratch);
    if (read_result != SD_BLOCK_DEVICE_OK)
    {
        return SAMPLE_PAGE_LOAD_READ_FAILED;
    }

    *out_source = &scratch[sector_offset];
    *out_source_bytes = source_bytes;
    return SAMPLE_PAGE_LOAD_OK;
}
