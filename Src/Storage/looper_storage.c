#include "Storage/looper_storage.h"

#include <stdio.h>

#include "Seq/seq_types.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"

static uint16_t g_looper_storage_save_counter;

static uint8_t looper_storage_build_final_path(uint8_t track_id,
                                               uint16_t counter,
                                               char *out_path,
                                               uint32_t out_len)
{
    if((out_path == 0) || (out_len == 0U) || (track_id >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }
    const int written = snprintf(out_path,
                                 out_len,
                                 "0:/PROJECT/LOOPS/LPR%02u_%04u.WAV",
                                 (unsigned)track_id,
                                 (unsigned)counter);
    return ((written > 0) && ((uint32_t)written < out_len)) ? 1U : 0U;
}

looper_storage_path_result_t looper_storage_make_next_path(uint8_t track_id,
                                                           char *out_path,
                                                           uint32_t out_len)
{
    if((out_path == 0) || (out_len == 0U) || (track_id >= SEQ_TRACK_COUNT))
    {
        return LOOPER_STORAGE_PATH_FAIL;
    }
    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
    {
        return LOOPER_STORAGE_PATH_BUSY;
    }

    looper_storage_path_result_t result = LOOPER_STORAGE_PATH_FAIL;
    if(sd_access_fs_mount_if_needed() == 0U)
    {
        goto done;
    }
    FRESULT fr = f_mkdir("0:/PROJECT");
    if((fr != FR_OK) && (fr != FR_EXIST))
    {
        goto done;
    }
    fr = f_mkdir("0:/PROJECT/LOOPS");
    if((fr != FR_OK) && (fr != FR_EXIST))
    {
        goto done;
    }
    for(uint32_t attempt = 0U; attempt < LOOPER_STORAGE_SAVE_PATH_TRIES; ++attempt)
    {
        const uint16_t counter = g_looper_storage_save_counter;
        g_looper_storage_save_counter = (uint16_t)(
            (g_looper_storage_save_counter + 1U) % LOOPER_STORAGE_SAVE_PATH_TRIES);
        if(looper_storage_build_final_path(
                track_id, counter, out_path, out_len) == 0U)
        {
            break;
        }
        FILINFO info;
        fr = f_stat(out_path, &info);
        if(fr == FR_NO_FILE)
        {
            result = LOOPER_STORAGE_PATH_OK;
            break;
        }
        if(fr != FR_OK)
        {
            break;
        }
    }

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    return result;
}

uint8_t looper_storage_copy_wav_path_as_rec(const char *wav_path,
                                            char *out_path,
                                            uint32_t out_len)
{
    if((wav_path == 0) || (out_path == 0) || (out_len < 5U))
    {
        return 0U;
    }
    uint32_t length = 0U;
    while((length < out_len) && (wav_path[length] != '\0'))
    {
        out_path[length] = wav_path[length];
        length++;
    }
    if((length >= out_len) || (length < 4U))
    {
        out_path[0] = '\0';
        return 0U;
    }
    out_path[length] = '\0';
    if((out_path[length - 4U] != '.')
            || (out_path[length - 3U] != 'W')
            || (out_path[length - 2U] != 'A')
            || (out_path[length - 1U] != 'V'))
    {
        out_path[0] = '\0';
        return 0U;
    }
    out_path[length - 3U] = 'R';
    out_path[length - 2U] = 'E';
    out_path[length - 1U] = 'C';
    return 1U;
}
