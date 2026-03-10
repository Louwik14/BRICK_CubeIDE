#include "Sampler/sample_pool.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/wav_parser.h"

#include "ff.h"

#define SAMPLE_POOL_HAS_FATFS 1

#ifndef SAMPLE_POOL_HAS_FATFS
#define SAMPLE_POOL_HAS_FATFS 0
#endif

#define SAMPLE_POOL_DEBUG 1

#if SAMPLE_POOL_DEBUG
#define SAMPLE_POOL_LOG(...) printf(__VA_ARGS__)
#else
#define SAMPLE_POOL_LOG(...)
#endif

static sample_desc_t g_sample_pool[SAMPLE_POOL_SIZE];

#define SAMPLE_POOL_MAX_FRAMES_PER_SAMPLE (32768U)
static AUDIO_COLD_SDRAM float g_sample_pool_data[SAMPLE_POOL_SIZE][SAMPLE_POOL_MAX_FRAMES_PER_SAMPLE * 2U];

static float sample_pool_pcm24_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
    if((v & 0x00800000L) != 0)
        v |= (int32_t)0xFF000000L;
    return (float)v * (1.0f / 8388608.0f);
}

static float sample_pool_pcm32_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] |
                          ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16) |
                          ((uint32_t)p[3] << 24));
    return (float)v * (1.0f / 2147483648.0f);
}

#if SAMPLE_POOL_HAS_FATFS
static bool sample_pool_load_full_data(FIL *fp,
                                       uint16_t sample_id,
                                       const wav_info_t *info,
                                       uint32_t data_size_aligned,
                                       sample_desc_t *desc)
{
    uint8_t io_buf[512U * 8U];
    const uint32_t bytes_per_frame = info->block_align;
    const uint32_t total_frames = data_size_aligned / bytes_per_frame;
    uint32_t loaded_frames = 0U;

    if(total_frames == 0U)
        return false;

    if(total_frames > SAMPLE_POOL_MAX_FRAMES_PER_SAMPLE)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] sample too large id=%u frames=%lu max=%u\n",
                        (unsigned)sample_id,
                        (unsigned long)total_frames,
                        (unsigned)SAMPLE_POOL_MAX_FRAMES_PER_SAMPLE);
        return false;
    }

    if(f_lseek(fp, info->data_offset) != FR_OK)
        return false;

    while(loaded_frames < total_frames)
    {
        const uint32_t frames_left = total_frames - loaded_frames;
        const uint32_t chunk_frames = (frames_left > 512U) ? 512U : frames_left;
        const uint32_t chunk_bytes = chunk_frames * bytes_per_frame;

        UINT br = 0U;
        if((f_read(fp, io_buf, chunk_bytes, &br) != FR_OK) || (br == 0U))
            break;

        const uint32_t ready_frames = br / bytes_per_frame;
        for(uint32_t i = 0U; i < ready_frames; i++)
        {
            const uint8_t *frame = &io_buf[i * bytes_per_frame];
            const uint32_t out = (loaded_frames + i) * 2U;

            if(info->bits_per_sample == 24U)
            {
                g_sample_pool_data[sample_id][out] = sample_pool_pcm24_to_float(&frame[0]);
                g_sample_pool_data[sample_id][out + 1U] = sample_pool_pcm24_to_float(&frame[3]);
            }
            else
            {
                g_sample_pool_data[sample_id][out] = sample_pool_pcm32_to_float(&frame[0]);
                g_sample_pool_data[sample_id][out + 1U] = sample_pool_pcm32_to_float(&frame[4]);
            }
        }

        loaded_frames += ready_frames;

        if(ready_frames < chunk_frames)
            break;
    }

    if(loaded_frames != total_frames)
        return false;

    desc->data = &g_sample_pool_data[sample_id][0];
    return true;
}
#endif

#if SAMPLE_POOL_HAS_FATFS
static FATFS g_sample_pool_fs;
static uint8_t g_sample_pool_fs_mounted;
#endif

static void sample_pool_clear_entry(sample_desc_t *desc)
{
    if(desc == NULL)
        return;

    memset(desc, 0, sizeof(*desc));
    desc->data = NULL;
    desc->valid = 0U;
}

static size_t sample_pool_trim_path_copy(char *dst, size_t dst_size, const char *src)
{
    size_t start = 0U;
    size_t end;

    if((dst == NULL) || (dst_size == 0U) || (src == NULL))
        return 0U;

    end = strlen(src);

    while((start < end) && (isspace((unsigned char)src[start]) != 0))
        start++;

    while((end > start) && (isspace((unsigned char)src[end - 1U]) != 0))
        end--;

    const size_t trimmed_len = end - start;
    if(trimmed_len >= dst_size)
        return 0U;

    memcpy(dst, &src[start], trimmed_len);
    dst[trimmed_len] = '\0';

    return trimmed_len;
}

void sample_pool_init(void)
{
    for(uint32_t i = 0U; i < SAMPLE_POOL_SIZE; i++)
        sample_pool_clear_entry(&g_sample_pool[i]);

#if SAMPLE_POOL_HAS_FATFS
    g_sample_pool_fs_mounted = 0U;
#endif
}

bool sample_pool_load(uint16_t id, const char *path)
{
    if(id >= SAMPLE_POOL_SIZE)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] invalid id=%u\n", (unsigned)id);
        return false;
    }

    sample_desc_t *desc = &g_sample_pool[id];
    sample_pool_clear_entry(desc);

    if((path == NULL) || (path[0] == '\0'))
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] id=%u invalid path\n", (unsigned)id);
        return false;
    }

    const size_t raw_path_len = strlen(path);
    if(raw_path_len >= SAMPLE_POOL_PATH_MAX)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] id=%u path too long (%lu >= %u)\n",
                        (unsigned)id,
                        (unsigned long)raw_path_len,
                        (unsigned)SAMPLE_POOL_PATH_MAX);
        return false;
    }

    const size_t path_len = sample_pool_trim_path_copy(desc->path,
                                                       sizeof(desc->path),
                                                       path);
    if(path_len == 0U)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] id=%u path invalid/empty after trim\n",
                        (unsigned)id);
        return false;
    }

#if SAMPLE_POOL_HAS_FATFS
    if(g_sample_pool_fs_mounted == 0U)
    {
        const FRESULT mount_fr = f_mount(&g_sample_pool_fs, "0:", 1U);
        if(mount_fr != FR_OK)
            return false;

        g_sample_pool_fs_mounted = 1U;
    }

    FIL fp;
    if(f_open(&fp, desc->path, FA_READ) != FR_OK)
        return false;

    wav_info_t info;
    memset(&info, 0, sizeof(info));

    if(!wav_parser_parse_info(&fp, &info))
    {
        (void)f_close(&fp);
        return false;
    }

    if((info.audio_format != 1U) ||
       (info.sample_rate != 48000U) ||
       (info.channels != 2U) ||
       ((info.bits_per_sample != 24U) && (info.bits_per_sample != 32U)) ||
       (info.block_align == 0U) ||
       (info.byte_rate != (info.sample_rate * info.block_align)))
    {
        (void)f_close(&fp);
        return false;
    }

    const uint32_t bytes_per_frame = (uint32_t)info.block_align;
    const uint32_t data_size_aligned = info.data_size - (info.data_size % bytes_per_frame);

    desc->data_offset = info.data_offset;
    desc->length_frames = data_size_aligned / bytes_per_frame;
    desc->bytes_per_frame = bytes_per_frame;
    desc->sample_rate = info.sample_rate;
    desc->channels = info.channels;
    desc->bits_per_sample = info.bits_per_sample;

    if(!sample_pool_load_full_data(&fp, id, &info, data_size_aligned, desc))
    {
        (void)f_close(&fp);
        return false;
    }

    desc->valid = 1U;
    (void)f_close(&fp);

    SAMPLE_POOL_LOG("[SAMPLE_POOL] loaded id=%u path=%s frames=%lu\n",
                    (unsigned)id,
                    desc->path,
                    (unsigned long)desc->length_frames);

    return true;
#else
    (void)path;
    SAMPLE_POOL_LOG("[SAMPLE_POOL] FatFs unavailable in this build\n");
    return false;
#endif
}

const sample_desc_t *sample_pool_get(uint16_t id)
{
    if(id >= SAMPLE_POOL_SIZE)
        return NULL;

    return &g_sample_pool[id];
}
