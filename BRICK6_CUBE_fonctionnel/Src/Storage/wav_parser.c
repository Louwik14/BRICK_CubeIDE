#include "wav_parser.h"
#include <string.h>

#define WAV_FMT_PCM        1
#define WAV_FMT_EXTENSIBLE 65534

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

static bool wav_find_chunks(FIL *fp,
                            wav_info_t *info,
                            uint16_t *audio_format,
                            uint16_t *channels,
                            uint32_t *sample_rate,
                            uint16_t *bits_per_sample,
                            uint16_t *block_align,
                            uint32_t *byte_rate,
                            uint32_t *data_offset,
                            uint32_t *data_size)
{
    uint8_t riff[12];
    UINT br;

    if((f_read(fp, riff, 12, &br) != FR_OK) || br != 12)
        return false;

    if(memcmp(&riff[0], "RIFF", 4) != 0 || memcmp(&riff[8], "WAVE", 4) != 0)
        return false;

    *audio_format = 0;
    *channels = 0;
    *sample_rate = 0;
    *bits_per_sample = 0;
    *block_align = 0;
    *byte_rate = 0;
    *data_offset = 0;
    *data_size = 0;

    while(f_tell(fp) + 8 <= f_size(fp))
    {
        uint8_t hdr[8];
        uint32_t chunk_size;

        if((f_read(fp, hdr, 8, &br) != FR_OK) || br != 8)
            return false;

        chunk_size = le32(&hdr[4]);

        if(memcmp(&hdr[0], "fmt ", 4) == 0)
        {
            uint8_t fmt[40];
            uint32_t to_read = chunk_size > sizeof(fmt) ? sizeof(fmt) : chunk_size;

            if((f_read(fp, fmt, to_read, &br) != FR_OK) || br != to_read)
                return false;

            if(to_read < 16)
                return false;

            *audio_format   = le16(&fmt[0]);
            *channels       = le16(&fmt[2]);
            *sample_rate    = le32(&fmt[4]);
            *byte_rate      = le32(&fmt[8]);
            *block_align    = le16(&fmt[12]);
            *bits_per_sample= le16(&fmt[14]);

            if(chunk_size > to_read)
            {
                if(f_lseek(fp, f_tell(fp) + (chunk_size - to_read)) != FR_OK)
                    return false;
            }
        }
        else if(memcmp(&hdr[0], "data", 4) == 0)
        {
            *data_offset = f_tell(fp);
            *data_size   = chunk_size;

            if(f_lseek(fp, f_tell(fp) + chunk_size) != FR_OK)
                return false;
        }
        else
        {
            if(f_lseek(fp, f_tell(fp) + chunk_size) != FR_OK)
                return false;
        }

        if(chunk_size & 1)
        {
            if(f_lseek(fp, f_tell(fp) + 1) != FR_OK)
                return false;
        }

        if(*audio_format && *data_size)
            break;
    }

    if(info)
    {
        info->audio_format   = *audio_format;
        info->sample_rate     = *sample_rate;
        info->byte_rate       = *byte_rate;
        info->channels        = *channels;
        info->block_align     = *block_align;
        info->bits_per_sample = *bits_per_sample;
        info->data_offset     = *data_offset;
        info->data_size       = *data_size;
    }

    return (*audio_format == WAV_FMT_PCM ||
            *audio_format == WAV_FMT_EXTENSIBLE);
}

bool wav_parser_parse_info(FIL *fp, wav_info_t *info)
{
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t block_align;
    uint32_t byte_rate;
    uint32_t data_offset;
    uint32_t data_size;

    if(!fp)
        return false;

    if(f_lseek(fp, 0) != FR_OK)
        return false;

    return wav_find_chunks(fp,
                           info,
                           &audio_format,
                           &channels,
                           &sample_rate,
                           &bits_per_sample,
                           &block_align,
                           &byte_rate,
                           &data_offset,
                           &data_size);
}
