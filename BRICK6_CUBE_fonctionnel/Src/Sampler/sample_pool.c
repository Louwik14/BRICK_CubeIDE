#include "Sampler/sample_pool.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "Storage/wav_parser.h"

#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#    define SAMPLE_POOL_HAS_FATFS 1
#  endif
#endif

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

#if SAMPLE_POOL_HAS_FATFS
static FATFS g_sample_pool_fs;
static uint8_t g_sample_pool_fs_mounted;
#endif

static void sample_pool_clear_entry(sample_desc_t *desc)
{
    if(desc == NULL)
        return;

    memset(desc, 0, sizeof(*desc));
    desc->attack_cache = NULL;
    desc->attack_frames = 0U;
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

    SAMPLE_POOL_LOG("[SAMPLE_POOL] OPEN PATH='%s' len=%lu\n",
                    desc->path,
                    (unsigned long)path_len);

#if SAMPLE_POOL_HAS_FATFS
    if(g_sample_pool_fs_mounted == 0U)
    {
        const FRESULT mount_fr = f_mount(&g_sample_pool_fs, "0:", 1U);
        if(mount_fr != FR_OK)
        {
            SAMPLE_POOL_LOG("[SAMPLE_POOL] mount fail id=%u fr=%d\n",
                            (unsigned)id,
                            (int)mount_fr);
            return false;
        }

        g_sample_pool_fs_mounted = 1U;
    }

    FIL fp;
    const FRESULT open_fr = f_open(&fp, desc->path, FA_READ);
    if(open_fr != FR_OK)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] open fail id=%u path=%s fr=%d\n",
                        (unsigned)id,
                        desc->path,
                        (int)open_fr);

        DIR dir;
        FILINFO fno;
        if(f_opendir(&dir, "0:/") == FR_OK)
        {
            SAMPLE_POOL_LOG("[SAMPLE_POOL] root dir listing:\n");
            while((f_readdir(&dir, &fno) == FR_OK) && (fno.fname[0] != '\0'))
                SAMPLE_POOL_LOG("  - %s\n", fno.fname);
            (void)f_closedir(&dir);
        }

        return false;
    }

    wav_info_t info;
    memset(&info, 0, sizeof(info));

    const bool parse_ok = wav_parser_parse_info(&fp, &info);
    (void)f_close(&fp);

    if(!parse_ok)
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] parse fail id=%u path=%s\n",
                        (unsigned)id,
                        desc->path);
        return false;
    }

    if((info.audio_format != 1U) ||
       (info.sample_rate != 48000U) ||
       (info.channels != 2U) ||
       ((info.bits_per_sample != 24U) && (info.bits_per_sample != 32U)) ||
       (info.block_align == 0U) ||
       (info.byte_rate != (info.sample_rate * info.block_align)))
    {
        SAMPLE_POOL_LOG("[SAMPLE_POOL] unsupported fmt id=%u fmt=%u sr=%lu ch=%u bits=%u align=%u rate=%lu\n",
                        (unsigned)id,
                        (unsigned)info.audio_format,
                        (unsigned long)info.sample_rate,
                        (unsigned)info.channels,
                        (unsigned)info.bits_per_sample,
                        (unsigned)info.block_align,
                        (unsigned long)info.byte_rate);
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
    desc->attack_cache = NULL;
    desc->attack_frames = 0U;
    desc->valid = 1U;

    SAMPLE_POOL_LOG("[SAMPLE_POOL] loaded id=%u path=%s frames=%lu off=%lu bpf=%lu\n",
                    (unsigned)id,
                    desc->path,
                    (unsigned long)desc->length_frames,
                    (unsigned long)desc->data_offset,
                    (unsigned long)desc->bytes_per_frame);

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
