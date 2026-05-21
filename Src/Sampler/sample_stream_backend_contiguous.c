#include "Sampler/sample_stream_backend_contiguous.h"

#include <string.h>

#include "SD/sd_block_device.h"
#include "Storage/memory_layout.h"
#include "Storage/wav_audio_codec.h"

#define SAMPLE_STREAM_CONTIG_SECTOR_SIZE       (512U)
#define SAMPLE_STREAM_CONTIG_SCRATCH_SECTORS   \
    (((SAMPLE_PAGE_FRAMES * 8U) + (SAMPLE_STREAM_CONTIG_SECTOR_SIZE - 1U) \
      + (SAMPLE_STREAM_CONTIG_SECTOR_SIZE - 1U)) / SAMPLE_STREAM_CONTIG_SECTOR_SIZE)
#define SAMPLE_STREAM_CONTIG_SCRATCH_BYTES \
    (SAMPLE_STREAM_CONTIG_SECTOR_SIZE * SAMPLE_STREAM_CONTIG_SCRATCH_SECTORS)

SDRAM_STREAM_SCRATCH static uint8_t
    g_sample_stream_contig_scratch[SAMPLE_STREAM_CONTIG_SCRATCH_BYTES];

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_STREAM_CONTIG_SCRATCH_BYTES
                   >= ((SAMPLE_PAGE_FRAMES * 8U) + (SAMPLE_STREAM_CONTIG_SECTOR_SIZE - 1U)),
               "contiguous scratch must cover one PCM32 stereo page plus sector envelope");
#endif

static sample_page_load_result_t sample_stream_backend_decode_pcm_page(
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target,
    const uint8_t *pcm)
{
    if ((info == 0) || (target == 0) || (target->frames_interleaved == 0)
        || (pcm == 0) || (info->info.block_align == 0U))
    {
        return SAMPLE_PAGE_LOAD_INVALID_ARG;
    }

    const uint32_t source_bytes = target->frame_count * info->info.block_align;
    if (source_bytes != 0U)
    {
        const uint32_t aligned_bytes = source_bytes - (source_bytes % info->info.block_align);
        if (aligned_bytes != source_bytes)
        {
            return SAMPLE_PAGE_LOAD_DECODE_FAILED;
        }
    }

    for (uint32_t frame = 0U; frame < target->frame_count; ++frame)
    {
        float left = 0.0f;
        float right = 0.0f;
        wav_audio_codec_decode_stereo_frame(&pcm[frame * info->info.block_align],
                                            info->info.channels,
                                            info->info.bits_per_sample,
                                            &left,
                                            &right);
        target->frames_interleaved[(frame * SAMPLE_PAGE_FRAME_STRIDE_FLOATS)] = left;
        target->frames_interleaved[(frame * SAMPLE_PAGE_FRAME_STRIDE_FLOATS) + 1U] = right;
    }

    return SAMPLE_PAGE_LOAD_OK;
}

sample_page_load_result_t sample_stream_backend_contiguous_load_page(
    const sample_page_stream_info_t *info,
    const sample_page_load_target_t *target)
{
    if ((info == 0) || (target == 0) || (target->frames_interleaved == 0)
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
    if ((sector_count == 0U) || (sector_count > SAMPLE_STREAM_CONTIG_SCRATCH_SECTORS))
    {
        return SAMPLE_PAGE_LOAD_READ_FAILED;
    }

    const uint32_t lba = info->stream_safe.first_file_lba
                       + (uint32_t)(file_byte_offset / SAMPLE_STREAM_CONTIG_SECTOR_SIZE);

    memset(g_sample_stream_contig_scratch, 0, sizeof(g_sample_stream_contig_scratch));
    const sd_block_device_result_t read_result =
        sd_block_device_read(lba, sector_count, g_sample_stream_contig_scratch);
    if (read_result != SD_BLOCK_DEVICE_OK)
    {
        return SAMPLE_PAGE_LOAD_READ_FAILED;
    }

    const sample_page_load_result_t decode_result =
        sample_stream_backend_decode_pcm_page(info,
                                              target,
                                              &g_sample_stream_contig_scratch[sector_offset]);
    if (decode_result != SAMPLE_PAGE_LOAD_OK)
    {
        return decode_result;
    }

    return SAMPLE_PAGE_LOAD_OK;
}
