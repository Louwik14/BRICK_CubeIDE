#include "Sampler/sample_stream_fatfs_map.h"

#include <string.h>

#include "Storage/sd_access_gate.h"

#include "ff.h"

#define SAMPLE_STREAM_FATFS_SECTOR_SIZE (512U)
#define SAMPLE_STREAM_FATFS_CLMT_ITEMS  (4U)

void sample_stream_safe_metadata_init_fatfs(sample_audio_key_t key,
                                            const wav_info_t *info,
                                            uint32_t total_frames,
                                            uint32_t data_offset,
                                            sample_stream_safe_metadata_t *out_meta)
{
    if (out_meta == 0)
    {
        return;
    }

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->key = key;
    out_meta->backend_kind = (uint8_t)SAMPLE_STREAM_BACKEND_FATFS;
    out_meta->safe_state = (uint8_t)SAMPLE_STREAM_SAFE_INVALID;
    out_meta->data_offset_bytes = data_offset;
    out_meta->total_frames = total_frames;
    out_meta->sector_size = SAMPLE_STREAM_FATFS_SECTOR_SIZE;
    if (info != 0)
    {
        out_meta->block_align = info->block_align;
        out_meta->bytes_per_frame = info->block_align;
        out_meta->channels = info->channels;
        out_meta->bits_per_sample = info->bits_per_sample;
        out_meta->sample_rate = info->sample_rate;
        out_meta->data_size = info->data_size;
        out_meta->data_sector_offset = (uint16_t)(data_offset & (SAMPLE_STREAM_FATFS_SECTOR_SIZE - 1U));
    }
}

uint8_t sample_stream_fatfs_map_certify_contiguous(sample_audio_key_t key,
                                                   const char *path,
                                                   const wav_info_t *info,
                                                   uint32_t total_frames,
                                                   uint32_t data_offset,
                                                   sample_stream_safe_metadata_t *out_meta)
{
    sample_stream_safe_metadata_init_fatfs(key, info, total_frames, data_offset, out_meta);
    if ((path == 0) || (path[0] == '\0') || (info == 0) || (out_meta == 0)
        || (info->block_align == 0U) || (total_frames == 0U))
    {
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        return 0U;
    }

    uint8_t ok = 0U;
    FIL fp;
    uint8_t fp_open = 0U;
    DWORD clmt[SAMPLE_STREAM_FATFS_CLMT_ITEMS];

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        goto done;
    }

    if (f_open(&fp, path, FA_READ) != FR_OK)
    {
        goto done;
    }
    fp_open = 1U;

    const FSIZE_t file_size_fs = f_size(&fp);
    if (file_size_fs > (FSIZE_t)UINT32_MAX)
    {
        goto done;
    }

    const uint32_t file_size = (uint32_t)file_size_fs;
    const uint64_t data_end = (uint64_t)data_offset + (uint64_t)info->data_size;
    if ((data_end > (uint64_t)file_size) || (fp.obj.sclust < 2U)
        || (fp.obj.fs == 0) || (fp.obj.fs->csize == 0U))
    {
        goto done;
    }

    memset(clmt, 0, sizeof(clmt));
    clmt[0] = SAMPLE_STREAM_FATFS_CLMT_ITEMS;
    fp.cltbl = clmt;
    const FRESULT map_fr = f_lseek(&fp, CREATE_LINKMAP);
    fp.cltbl = 0;
    if (map_fr != FR_OK)
    {
        goto done;
    }

    if ((clmt[0] != SAMPLE_STREAM_FATFS_CLMT_ITEMS) || (clmt[1] == 0U) || (clmt[2] < 2U))
    {
        goto done;
    }

    const uint64_t contiguous_bytes =
        (uint64_t)clmt[1] * (uint64_t)fp.obj.fs->csize * (uint64_t)SAMPLE_STREAM_FATFS_SECTOR_SIZE;
    if (contiguous_bytes < (uint64_t)file_size)
    {
        goto done;
    }

    const uint32_t first_file_lba =
        ((clmt[2] - 2U) * (uint32_t)fp.obj.fs->csize) + (uint32_t)fp.obj.fs->database;

    out_meta->valid = 1U;
    out_meta->backend_kind = (uint8_t)SAMPLE_STREAM_BACKEND_SAFE_CONTIGUOUS;
    out_meta->safe_state = (uint8_t)SAMPLE_STREAM_SAFE_CONTIGUOUS;
    out_meta->contiguous = 1U;
    out_meta->first_file_lba = first_file_lba;
    out_meta->file_size = file_size;
    out_meta->fragment_count = 1U;
    out_meta->generation++;
    ok = 1U;

done:
    if (fp_open != 0U)
    {
        (void)f_close(&fp);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
    return ok;
}
