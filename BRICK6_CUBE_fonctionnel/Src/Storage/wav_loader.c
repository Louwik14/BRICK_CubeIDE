#include "wav_loader.h"

#include <stdio.h>
#include <string.h>

#include "memory_layout.h"

#define WAV_BUFFER_FRAMES (48000U)
#define WAV_BUFFER_SAMPLES (WAV_BUFFER_FRAMES * 2U)

static AUDIO_COLD_SDRAM float g_wav_pcm[WAV_BUFFER_SAMPLES];

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static float pcm24_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
    if((v & 0x00800000L) != 0)
        v |= (int32_t)0xFF000000L;
    return (float)v * (1.0f / 8388608.0f);
}

static float pcm32_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)le32(p);
    return (float)v * (1.0f / 2147483648.0f);
}

const float *wav_loader_get_interleaved_buffer(void)
{
    return g_wav_pcm;
}

uint32_t wav_loader_get_capacity_frames(void)
{
    return WAV_BUFFER_FRAMES;
}

#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#    define WAV_LOADER_HAS_FATFS 1
#  endif
#endif

#ifndef WAV_LOADER_HAS_FATFS
#define WAV_LOADER_HAS_FATFS 0
#endif

#if WAV_LOADER_HAS_FATFS
static FATFS g_wav_fs;
static uint8_t g_wav_fs_mounted;

static bool wav_find_chunks(FIL *fp,
                            wav_info_t *info,
                            uint16_t *audio_format,
                            uint16_t *channels,
                            uint32_t *sample_rate,
                            uint16_t *bits_per_sample,
                            uint32_t *data_offset,
                            uint32_t *data_size)
{
    uint8_t riff[12];
    UINT br = 0U;

    if((f_read(fp, riff, sizeof(riff), &br) != FR_OK) || (br != sizeof(riff)))
        return false;

    if((memcmp(&riff[0], "RIFF", 4) != 0) || (memcmp(&riff[8], "WAVE", 4) != 0))
        return false;

    *audio_format = 0U;
    *channels = 0U;
    *sample_rate = 0U;
    *bits_per_sample = 0U;
    *data_offset = 0U;
    *data_size = 0U;

    while(f_tell(fp) + 8U <= f_size(fp))
    {
        uint8_t chunk_header[8];
        uint8_t fmt_buf[40];
        uint32_t chunk_size;

        if((f_read(fp, chunk_header, sizeof(chunk_header), &br) != FR_OK) || (br != sizeof(chunk_header)))
            return false;

        chunk_size = le32(&chunk_header[4]);

        if(memcmp(&chunk_header[0], "fmt ", 4) == 0)
        {
            uint32_t to_read = (chunk_size < sizeof(fmt_buf)) ? chunk_size : (uint32_t)sizeof(fmt_buf);
            if((f_read(fp, fmt_buf, to_read, &br) != FR_OK) || (br != to_read))
                return false;

            if(to_read < 16U)
                return false;

            *audio_format = le16(&fmt_buf[0]);
            *channels = le16(&fmt_buf[2]);
            *sample_rate = le32(&fmt_buf[4]);
            *bits_per_sample = le16(&fmt_buf[14]);

            if(chunk_size > to_read)
            {
                if(f_lseek(fp, f_tell(fp) + (chunk_size - to_read)) != FR_OK)
                    return false;
            }
        }
        else if(memcmp(&chunk_header[0], "data", 4) == 0)
        {
            *data_offset = f_tell(fp);
            *data_size = chunk_size;
            if(f_lseek(fp, f_tell(fp) + chunk_size) != FR_OK)
                return false;
        }
        else
        {
            if(f_lseek(fp, f_tell(fp) + chunk_size) != FR_OK)
                return false;
        }

        if((chunk_size & 1U) != 0U)
        {
            if(f_lseek(fp, f_tell(fp) + 1U) != FR_OK)
                return false;
        }

        if((*audio_format != 0U) && (*data_size != 0U))
            break;
    }

    if(info != 0)
    {
        info->sample_rate = *sample_rate;
        info->channels = *channels;
        info->bits_per_sample = *bits_per_sample;
        info->data_offset = *data_offset;
        info->data_size = *data_size;
    }

    return (*audio_format != 0U) && (*data_size != 0U);
}
#endif

bool wav_loader_load_to_sdram(const char *path, wav_info_t *info)
{
    uint32_t i;

    for(i = 0U; i < WAV_BUFFER_SAMPLES; i++)
        g_wav_pcm[i] = 0.0f;

    if(info != 0)
        memset(info, 0, sizeof(*info));

#if WAV_LOADER_HAS_FATFS
    FIL fp;
    FRESULT fr;
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t bytes_per_frame;
    uint32_t max_frames_from_file;
    uint32_t frames_to_load;
    uint32_t frames_loaded = 0U;
    uint8_t io_buf[4096];

    if(path == 0)
    {
        printf("[WAV] invalid path\r\n");
        return false;
    }

    if(g_wav_fs_mounted == 0U)
    {
        fr = f_mount(&g_wav_fs, "0:", 1U);
        if(fr != FR_OK)
        {
            printf("[WAV] f_mount failed: %d\r\n", (int)fr);
            return false;
        }
        g_wav_fs_mounted = 1U;
    }

    fr = f_open(&fp, path, FA_READ);
    if(fr != FR_OK)
    {
        printf("[WAV] f_open failed: %s (%d)\r\n", path, (int)fr);
        return false;
    }

    if(!wav_find_chunks(&fp, info, &audio_format, &channels, &sample_rate,
                        &bits_per_sample, &data_offset, &data_size))
    {
        (void)f_close(&fp);
        printf("[WAV] invalid RIFF/WAVE or missing chunks\r\n");
        return false;
    }

    printf("[WAV] fmt=%u ch=%u sr=%lu bits=%u data=%lu bytes off=%lu\r\n",
           (unsigned)audio_format,
           (unsigned)channels,
           (unsigned long)sample_rate,
           (unsigned)bits_per_sample,
           (unsigned long)data_size,
           (unsigned long)data_offset);

    if((audio_format != 1U) || (sample_rate != 48000U) || (channels != 2U) ||
       !((bits_per_sample == 24U) || (bits_per_sample == 32U)))
    {
        (void)f_close(&fp);
        printf("[WAV] unsupported format (PCM 48k stereo 24/32 required)\r\n");
        return false;
    }

    bytes_per_frame = (uint32_t)channels * ((uint32_t)bits_per_sample / 8U);
    if(bytes_per_frame == 0U)
    {
        (void)f_close(&fp);
        return false;
    }

    max_frames_from_file = data_size / bytes_per_frame;
    frames_to_load = (max_frames_from_file < WAV_BUFFER_FRAMES) ? max_frames_from_file : WAV_BUFFER_FRAMES;

    if(f_lseek(&fp, data_offset) != FR_OK)
    {
        (void)f_close(&fp);
        printf("[WAV] f_lseek data failed\r\n");
        return false;
    }

    while(frames_loaded < frames_to_load)
    {
        UINT br = 0U;
        uint32_t frames_left = frames_to_load - frames_loaded;
        uint32_t chunk_frames = (frames_left > 512U) ? 512U : frames_left;
        uint32_t chunk_bytes = chunk_frames * bytes_per_frame;

        if((f_read(&fp, io_buf, chunk_bytes, &br) != FR_OK) || (br == 0U))
            break;

        chunk_frames = br / bytes_per_frame;
        for(i = 0U; i < chunk_frames; i++)
        {
            const uint8_t *frame = &io_buf[i * bytes_per_frame];
            float l;
            float r;

            if(bits_per_sample == 24U)
            {
                l = pcm24_to_float(&frame[0]);
                r = pcm24_to_float(&frame[3]);
            }
            else
            {
                l = pcm32_to_float(&frame[0]);
                r = pcm32_to_float(&frame[4]);
            }

            g_wav_pcm[(frames_loaded + i) * 2U + 0U] = l;
            g_wav_pcm[(frames_loaded + i) * 2U + 1U] = r;
        }

        frames_loaded += chunk_frames;

        if(chunk_frames == 0U)
            break;
    }

    (void)f_close(&fp);

    if(info != 0)
        info->frames_loaded = frames_loaded;

    printf("[WAV] loaded frames=%lu (capacity=%lu)\r\n",
           (unsigned long)frames_loaded,
           (unsigned long)WAV_BUFFER_FRAMES);

    return (frames_loaded > 0U);
#else
    (void)path;
    printf("[WAV] FatFs unavailable in this build\r\n");
    return false;
#endif
}
