#include "wav_parser.h"

#include <string.h>
#include <stdio.h>

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

static bool wav_ext_is_wav(const char *name)
{
    size_t len;

    if(name == NULL)
    {
        return false;
    }

    len = strlen(name);
    if(len < 4U)
    {
        return false;
    }

    return (name[len - 4U] == '.') &&
           ((name[len - 3U] == 'w') || (name[len - 3U] == 'W')) &&
           ((name[len - 2U] == 'a') || (name[len - 2U] == 'A')) &&
           ((name[len - 1U] == 'v') || (name[len - 1U] == 'V'));
}

bool wav_parser_find_first_wav(char *out_path, uint32_t max_len)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;

    if((out_path == NULL) || (max_len < 8U))
    {
        return false;
    }

    fr = f_opendir(&dir, "0:/");
    if(fr != FR_OK)
    {
        return false;
    }

    while(1)
    {
        fr = f_readdir(&dir, &fno);
        if(fr != FR_OK)
        {
            (void)f_closedir(&dir);
            return false;
        }

        if(fno.fname[0] == '\0')
        {
            break;
        }

        if((fno.fattrib & AM_DIR) != 0U)
        {
            continue;
        }

        if(!wav_ext_is_wav(fno.fname))
        {
            continue;
        }

        if((snprintf(out_path, max_len, "0:/%s", fno.fname) < 0) ||
           (strlen(out_path) >= max_len))
        {
            (void)f_closedir(&dir);
            return false;
        }

        (void)f_closedir(&dir);
        return true;
    }

    (void)f_closedir(&dir);
    return false;
}

bool wav_parser_read_header(FIL *fp, wav_parser_info_t *info)
{
    uint8_t riff[12];
    UINT br = 0U;

    if((fp == NULL) || (info == NULL))
    {
        return false;
    }

    memset(info, 0, sizeof(*info));

    if(f_lseek(fp, 0U) != FR_OK)
    {
        return false;
    }

    if((f_read(fp, riff, sizeof(riff), &br) != FR_OK) || (br != sizeof(riff)))
    {
        return false;
    }

    if((memcmp(&riff[0], "RIFF", 4) != 0) || (memcmp(&riff[8], "WAVE", 4) != 0))
    {
        return false;
    }

    while((f_tell(fp) + 8U) <= f_size(fp))
    {
        uint8_t chunk_header[8];
        uint8_t fmt_buf[40];
        uint32_t chunk_size;

        if((f_read(fp, chunk_header, sizeof(chunk_header), &br) != FR_OK) || (br != sizeof(chunk_header)))
        {
            return false;
        }

        chunk_size = le32(&chunk_header[4]);

        if(memcmp(&chunk_header[0], "fmt ", 4) == 0)
        {
            uint32_t to_read = (chunk_size < sizeof(fmt_buf)) ? chunk_size : (uint32_t)sizeof(fmt_buf);
            if((f_read(fp, fmt_buf, to_read, &br) != FR_OK) || (br != to_read))
            {
                return false;
            }

            if(to_read < 16U)
            {
                return false;
            }

            info->audio_format = le16(&fmt_buf[0]);
            info->channels = le16(&fmt_buf[2]);
            info->sample_rate = le32(&fmt_buf[4]);
            info->bits_per_sample = le16(&fmt_buf[14]);

            if(chunk_size > to_read)
            {
                if(f_lseek(fp, f_tell(fp) + (chunk_size - to_read)) != FR_OK)
                {
                    return false;
                }
            }
        }
        else if(memcmp(&chunk_header[0], "data", 4) == 0)
        {
            info->data_offset = f_tell(fp);
            info->data_size = chunk_size;
            if(f_lseek(fp, f_tell(fp) + chunk_size) != FR_OK)
            {
                return false;
            }
        }
        else
        {
            if(f_lseek(fp, f_tell(fp) + chunk_size) != FR_OK)
            {
                return false;
            }
        }

        if((chunk_size & 1U) != 0U)
        {
            if(f_lseek(fp, f_tell(fp) + 1U) != FR_OK)
            {
                return false;
            }
        }

        if((info->audio_format != 0U) && (info->data_size != 0U))
        {
            break;
        }
    }

    return (info->audio_format == 1U) &&
           (info->channels == 2U) &&
           (info->sample_rate == 48000U) &&
           (info->bits_per_sample == 24U) &&
           (info->data_size > 0U);
}
