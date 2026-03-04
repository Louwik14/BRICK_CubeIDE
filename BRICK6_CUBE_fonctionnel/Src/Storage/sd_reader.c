#include "sd_reader.h"

#include "ff.h"

static FATFS g_fs;
static uint8_t g_fs_mounted = 0U;

static FIL g_file;
static uint8_t g_opened = 0U;
static uint32_t g_data_offset = 0U;
static uint32_t g_data_size = 0U;
static uint32_t g_data_pos = 0U;

bool sd_reader_open(const char *path, uint32_t data_offset, uint32_t data_size)
{
    FRESULT fr;

    if((path == NULL) || (data_size == 0U))
    {
        return false;
    }

    sd_reader_close();

    if(g_fs_mounted == 0U)
    {
        fr = f_mount(&g_fs, "0:", 1U);
        if(fr != FR_OK)
        {
            return false;
        }
        g_fs_mounted = 1U;
    }

    fr = f_open(&g_file, path, FA_READ);
    if(fr != FR_OK)
    {
        return false;
    }

    if(f_lseek(&g_file, data_offset) != FR_OK)
    {
        (void)f_close(&g_file);
        return false;
    }

    g_data_offset = data_offset;
    g_data_size = data_size;
    g_data_pos = 0U;
    g_opened = 1U;

    return true;
}

void sd_reader_close(void)
{
    if(g_opened != 0U)
    {
        (void)f_close(&g_file);
    }

    g_opened = 0U;
    g_data_offset = 0U;
    g_data_size = 0U;
    g_data_pos = 0U;
}

bool sd_reader_read_looping(uint8_t *dst, uint32_t requested_bytes, uint32_t *out_bytes)
{
    uint32_t total_read = 0U;

    if((dst == NULL) || (out_bytes == NULL) || (requested_bytes == 0U) || (g_opened == 0U))
    {
        return false;
    }

    while(total_read < requested_bytes)
    {
        UINT br = 0U;
        uint32_t bytes_left = requested_bytes - total_read;
        uint32_t remain_in_data;

        if(g_data_pos >= g_data_size)
        {
            if(f_lseek(&g_file, g_data_offset) != FR_OK)
            {
                return false;
            }
            g_data_pos = 0U;
        }

        remain_in_data = g_data_size - g_data_pos;
        if(bytes_left > remain_in_data)
        {
            bytes_left = remain_in_data;
        }

        if(bytes_left == 0U)
        {
            continue;
        }

        if(f_read(&g_file, &dst[total_read], bytes_left, &br) != FR_OK)
        {
            return false;
        }

        if(br == 0U)
        {
            if(f_lseek(&g_file, g_data_offset) != FR_OK)
            {
                return false;
            }
            g_data_pos = 0U;
            continue;
        }

        g_data_pos += br;
        total_read += br;
    }

    *out_bytes = total_read;
    return (total_read == requested_bytes);
}
