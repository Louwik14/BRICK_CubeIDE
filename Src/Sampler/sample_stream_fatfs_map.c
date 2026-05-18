#include "Sampler/sample_stream_fatfs_map.h"

#include <string.h>

#include "Storage/wav_parser.h"

#ifndef CREATE_LINKMAP
#error "STREAM_SAFE_CONTIGUOUS scan requires FatFs _USE_FASTSEEK/CREATE_LINKMAP"
#endif

#define SAMPLE_STREAM_FATFS_MAP_SECTOR_BYTES (512U)
#define SAMPLE_STREAM_FATFS_MAP_CLMT_WORDS   (8U)

static uint8_t sample_stream_fatfs_map_format_supported(const wav_info_t *info)
{
    if (info == 0)
    {
        return 0U;
    }

    return (((info->audio_format == 1U) || (info->audio_format == 65534U))
            && ((info->channels == 1U) || (info->channels == 2U))
            && ((info->bits_per_sample == 16U)
                || (info->bits_per_sample == 24U)
                || (info->bits_per_sample == 32U))
            && (info->sample_rate == 48000U)
            && (info->block_align != 0U)) ? 1U : 0U;
}

static void sample_stream_fatfs_map_meta_init(sample_stream_source_meta_t *meta)
{
    memset(meta, 0, sizeof(*meta));
    meta->valid = 1U;
    meta->backend_kind = (uint8_t)SAMPLE_STREAM_BACKEND_FATFS;
    meta->safe_state = (uint8_t)SAMPLE_STREAM_SAFE_NONE;
    meta->reject_reason = (uint16_t)SAMPLE_STREAM_SAFE_REASON_NOT_SCANNED;
}

static void sample_stream_fatfs_map_fill_wav_meta(sample_stream_source_meta_t *meta,
                                                  const wav_info_t *info)
{
    meta->data_offset_bytes = info->data_offset;
    meta->data_size_bytes = info->data_size;
    meta->bytes_per_frame = info->block_align;
    meta->sample_rate = info->sample_rate;
    meta->channels = info->channels;
    meta->bits_per_sample = info->bits_per_sample;
    meta->block_align = info->block_align;
    meta->data_sector_offset = info->data_offset % SAMPLE_STREAM_FATFS_MAP_SECTOR_BYTES;
    if (info->block_align != 0U)
    {
        meta->total_frames = info->data_size / info->block_align;
    }
}

sample_stream_fatfs_map_result_t sample_stream_fatfs_map_scan_wav(const char *path)
{
    sample_stream_fatfs_map_result_t result;
    sample_stream_fatfs_map_meta_init(&result.meta);
    result.fr = FR_INVALID_PARAMETER;

    if ((path == 0) || (path[0] == '\0'))
    {
        result.meta.reject_reason = (uint16_t)SAMPLE_STREAM_SAFE_REASON_SCAN_FAIL;
        return result;
    }

    FIL fp;
    result.fr = f_open(&fp, path, FA_READ);
    if (result.fr != FR_OK)
    {
        result.meta.reject_reason = (uint16_t)SAMPLE_STREAM_SAFE_REASON_FATFS_ERROR;
        return result;
    }

    wav_info_t info;
    memset(&info, 0, sizeof(info));
    if (wav_parser_parse_info(&fp, &info) == false)
    {
        result.meta.reject_reason = (uint16_t)SAMPLE_STREAM_SAFE_REASON_SCAN_FAIL;
        (void)f_close(&fp);
        return result;
    }
    sample_stream_fatfs_map_fill_wav_meta(&result.meta, &info);
    result.meta.file_size_low = (uint32_t)fp.obj.objsize;

    if (sample_stream_fatfs_map_format_supported(&info) == 0U)
    {
        result.meta.reject_reason = (uint16_t)SAMPLE_STREAM_SAFE_REASON_UNSUPPORTED_FORMAT;
        (void)f_close(&fp);
        return result;
    }

    if ((info.data_size == 0U)
        || ((info.data_size % info.block_align) != 0U)
        || (result.meta.total_frames == 0U)
        || (((FSIZE_t)info.data_offset + (FSIZE_t)info.data_size) > fp.obj.objsize))
    {
        result.meta.reject_reason = (uint16_t)SAMPLE_STREAM_SAFE_REASON_INVALID_SIZE;
        (void)f_close(&fp);
        return result;
    }

    FILINFO fno;
    result.fr = f_stat(path, &fno);
    if (result.fr != FR_OK)
    {
        result.meta.reject_reason = (uint16_t)SAMPLE_STREAM_SAFE_REASON_FATFS_ERROR;
        (void)f_close(&fp);
        return result;
    }
    result.meta.file_date_time_token = (((uint32_t)fno.fdate) << 16) | (uint32_t)fno.ftime;

    if ((fp.obj.fs == 0) || (fp.obj.sclust < 2U))
    {
        result.meta.reject_reason = (uint16_t)SAMPLE_STREAM_SAFE_REASON_FATFS_ERROR;
        (void)f_close(&fp);
        return result;
    }

    DWORD clmt[SAMPLE_STREAM_FATFS_MAP_CLMT_WORDS];
    memset(clmt, 0, sizeof(clmt));
    clmt[0] = SAMPLE_STREAM_FATFS_MAP_CLMT_WORDS;
    fp.cltbl = clmt;
    result.fr = f_lseek(&fp, CREATE_LINKMAP);
    fp.cltbl = 0;
    if (result.fr == FR_NOT_ENOUGH_CORE)
    {
        result.meta.reject_reason = (uint16_t)SAMPLE_STREAM_SAFE_REASON_NON_CONTIGUOUS;
        result.meta.fragment_count = (uint16_t)((clmt[0] > 2U) ? ((clmt[0] - 2U) / 2U) : 0U);
        (void)f_close(&fp);
        return result;
    }
    if (result.fr != FR_OK)
    {
        result.meta.reject_reason = (uint16_t)SAMPLE_STREAM_SAFE_REASON_SCAN_FAIL;
        (void)f_close(&fp);
        return result;
    }

    const uint32_t used_words = clmt[0];
    const uint32_t fragment_count = (used_words > 2U) ? ((used_words - 2U) / 2U) : 0U;
    result.meta.fragment_count =
        (uint16_t)((fragment_count > UINT16_MAX) ? UINT16_MAX : fragment_count);
    if (fragment_count != 1U)
    {
        result.meta.reject_reason = (uint16_t)SAMPLE_STREAM_SAFE_REASON_NON_CONTIGUOUS;
        (void)f_close(&fp);
        return result;
    }

    const FATFS *const fs = fp.obj.fs;
    if ((fs->csize == 0U) || (fs->database == 0U))
    {
        result.meta.reject_reason = (uint16_t)SAMPLE_STREAM_SAFE_REASON_FATFS_ERROR;
        (void)f_close(&fp);
        return result;
    }

    const uint32_t first_file_lba =
        fs->database + ((fp.obj.sclust - 2U) * (uint32_t)fs->csize);
    result.meta.first_file_lba = first_file_lba;
    result.meta.first_data_lba =
        first_file_lba + (info.data_offset / SAMPLE_STREAM_FATFS_MAP_SECTOR_BYTES);
    result.meta.contig = 1U;
    result.meta.safe_state = (uint8_t)SAMPLE_STREAM_SAFE_CONTIGUOUS;
    result.meta.backend_kind = (uint8_t)SAMPLE_STREAM_BACKEND_FATFS;
    result.meta.reject_reason = (uint16_t)SAMPLE_STREAM_SAFE_REASON_NONE;

    (void)f_close(&fp);
    return result;
}
