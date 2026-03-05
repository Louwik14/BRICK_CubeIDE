#include "wav_parser.h"

#include <string.h>

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

bool wav_parser_parse_info(FIL *fp, wav_info_t *info)
{
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;

    if(fp == 0)
        return false;

    if(f_lseek(fp, 0U) != FR_OK)
        return false;

    if(!wav_find_chunks(fp, info, &audio_format, &channels, &sample_rate,
                        &bits_per_sample, &data_offset, &data_size))
        return false;

    return (audio_format == 1U);
}
