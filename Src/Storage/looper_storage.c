#include "Storage/looper_storage.h"

#include "Core/track_runtime.h"
#include "Seq/seq_types.h"
#include "Storage/multi_record_writer.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#include <stdio.h>
#include <string.h>

#define LOOPER_STORAGE_WAV_HEADER_BYTES 44U
#define LOOPER_STORAGE_EXPORT_IO_BYTES 32768U
#define LOOPER_STORAGE_EXPORT_WAIT_LIMIT 1000U

#define LOOPER_STORAGE_EXPORT_PHASE_OPEN_SRC 0U
#define LOOPER_STORAGE_EXPORT_PHASE_OPEN_DST 1U
#define LOOPER_STORAGE_EXPORT_PHASE_WRITE_HEADER 2U
#define LOOPER_STORAGE_EXPORT_PHASE_COPY 3U
#define LOOPER_STORAGE_EXPORT_PHASE_SYNC 4U
#define LOOPER_STORAGE_EXPORT_PHASE_VERIFY 5U
#define LOOPER_STORAGE_EXPORT_PHASE_CLOSE_DST 6U
#define LOOPER_STORAGE_EXPORT_PHASE_CLOSE_SRC 7U
#define LOOPER_STORAGE_EXPORT_COMPARE_FRAMES 16U

static uint16_t g_looper_storage_save_counter = 0U;
static uint8_t g_looper_storage_raw_available = 0U;
static looper_storage_raw_error_t g_looper_storage_raw_last_error =
    LOOPER_STORAGE_RAW_ERROR_NOT_VALIDATED;
static uint8_t g_looper_storage_raw_last_failed_slot = 0xFFU;
static uint32_t g_looper_storage_raw_last_fresult = (uint32_t)FR_OK;
static uint64_t g_looper_storage_raw_last_observed_size = 0ULL;

static const char *const g_looper_storage_raw_paths[LOOPER_STORAGE_RAW_SLOT_COUNT] = {
    "0:/SYSTEM/LOOPER/LPR00.RAW",
    "0:/SYSTEM/LOOPER/LPR01.RAW",
    "0:/SYSTEM/LOOPER/LPR02.RAW",
    "0:/SYSTEM/LOOPER/LPR03.RAW",
};

typedef struct
{
    looper_storage_raw_export_state_t state;
    looper_storage_raw_export_error_t error;
    uint8_t phase;
    uint8_t raw_slot;
    uint8_t src_open;
    uint8_t dst_open;
    uint8_t track_id;
    uint8_t waiting;
    uint32_t recorded_frames;
    uint32_t frames_done;
    uint32_t wait_count;
    uint32_t chunks_copied;
    uint32_t bytes_copied;
    uint32_t gate_acquire_count;
    uint32_t open_ms;
    uint32_t copy_ms;
    uint32_t sync_ms;
    uint32_t verify_ms;
    uint32_t close_ms;
    char raw_path[96U];
    char final_path[96U];
    FIL src;
    FIL dst;
} looper_storage_raw_export_ctx_t;

static looper_storage_raw_export_ctx_t g_looper_raw_export;
static looper_storage_raw_export_diag_t g_looper_raw_export_diag;
static uint8_t g_looper_raw_export_io[LOOPER_STORAGE_EXPORT_IO_BYTES];

static void looper_storage_write_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void looper_storage_write_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFUL);
    dst[1] = (uint8_t)((value >> 8) & 0xFFUL);
    dst[2] = (uint8_t)((value >> 16) & 0xFFUL);
    dst[3] = (uint8_t)((value >> 24) & 0xFFUL);
}

static void looper_storage_build_wav_header(uint8_t *header, uint32_t data_bytes)
{
    const uint16_t block_align = (uint16_t)LOOPER_STORAGE_RAW_BYTES_PER_FRAME;
    const uint32_t byte_rate = LOOPER_STORAGE_RAW_SAMPLE_RATE_HZ * (uint32_t)block_align;

    memcpy(&header[0], "RIFF", 4U);
    looper_storage_write_le32(&header[4], 36U + data_bytes);
    memcpy(&header[8], "WAVE", 4U);
    memcpy(&header[12], "fmt ", 4U);
    looper_storage_write_le32(&header[16], 16U);
    looper_storage_write_le16(&header[20], 1U);
    looper_storage_write_le16(&header[22], LOOPER_STORAGE_RAW_CHANNELS);
    looper_storage_write_le32(&header[24], LOOPER_STORAGE_RAW_SAMPLE_RATE_HZ);
    looper_storage_write_le32(&header[28], byte_rate);
    looper_storage_write_le16(&header[32], block_align);
    looper_storage_write_le16(&header[34], LOOPER_STORAGE_RAW_BITS_PER_SAMPLE);
    memcpy(&header[36], "data", 4U);
    looper_storage_write_le32(&header[40], data_bytes);
}

static uint8_t looper_storage_copy_path_local(char *dst, uint32_t dst_len, const char *src)
{
    if ((dst == 0) || (dst_len == 0U) || (src == 0) || (src[0] == '\0'))
    {
        return 0U;
    }

    uint32_t i = 0U;
    while ((i + 1U) < dst_len)
    {
        dst[i] = src[i];
        if (src[i] == '\0')
        {
            return 1U;
        }
        i++;
    }

    dst[0] = '\0';
    return 0U;
}

static void looper_storage_raw_export_set_failed(looper_storage_raw_export_error_t error)
{
    if (g_looper_raw_export.src_open != 0U)
    {
        (void)f_close(&g_looper_raw_export.src);
        g_looper_raw_export.src_open = 0U;
    }
    if (g_looper_raw_export.dst_open != 0U)
    {
        (void)f_close(&g_looper_raw_export.dst);
        g_looper_raw_export.dst_open = 0U;
    }

    g_looper_raw_export.error = error;
    g_looper_raw_export.state = LOOPER_STORAGE_RAW_EXPORT_FAILED;
}

static uint8_t looper_storage_raw_export_read_exact(FIL *fp, uint8_t *dst, uint32_t bytes)
{
    if ((fp == 0) || (dst == 0))
    {
        return 0U;
    }

    UINT br = 0U;
    const FRESULT fr = f_read(fp, dst, bytes, &br);
    return ((fr == FR_OK) && (br == bytes)) ? 1U : 0U;
}

static void looper_storage_raw_export_mark_waiting(void)
{
    g_looper_raw_export.waiting = 1U;
    if (g_looper_raw_export.wait_count < UINT32_MAX)
    {
        g_looper_raw_export.wait_count++;
    }

    if ((g_looper_raw_export.wait_count >= LOOPER_STORAGE_EXPORT_WAIT_LIMIT)
            && (g_looper_raw_export.src_open == 0U)
            && (g_looper_raw_export.dst_open == 0U))
    {
        g_looper_raw_export.error = LOOPER_STORAGE_RAW_EXPORT_ERROR_WAIT_TIMEOUT;
        g_looper_raw_export.state = LOOPER_STORAGE_RAW_EXPORT_FAILED;
    }
}

static void looper_storage_raw_export_clear_waiting(void)
{
    g_looper_raw_export.waiting = 0U;
    g_looper_raw_export.wait_count = 0U;
}

static void looper_storage_raw_export_copy_stats_to_diag(void)
{
    g_looper_raw_export_diag.chunks_copied = g_looper_raw_export.chunks_copied;
    g_looper_raw_export_diag.bytes_copied = g_looper_raw_export.bytes_copied;
    g_looper_raw_export_diag.gate_acquire_count = g_looper_raw_export.gate_acquire_count;
    g_looper_raw_export_diag.open_ms = g_looper_raw_export.open_ms;
    g_looper_raw_export_diag.copy_ms = g_looper_raw_export.copy_ms;
    g_looper_raw_export_diag.sync_ms = g_looper_raw_export.sync_ms;
    g_looper_raw_export_diag.verify_ms = g_looper_raw_export.verify_ms;
    g_looper_raw_export_diag.close_ms = g_looper_raw_export.close_ms;
}

static uint8_t looper_storage_raw_export_compare_window(uint32_t start_frame,
                                                        uint32_t frame_count,
                                                        uint8_t *raw_dst,
                                                        uint8_t *wav_dst,
                                                        uint8_t record_mismatch)
{
    if ((frame_count == 0U) || (raw_dst == 0) || (wav_dst == 0))
    {
        return 1U;
    }

    const uint32_t bytes = frame_count * LOOPER_STORAGE_RAW_BYTES_PER_FRAME;
    const FSIZE_t raw_offset = (FSIZE_t)start_frame * (FSIZE_t)LOOPER_STORAGE_RAW_BYTES_PER_FRAME;
    const FSIZE_t wav_offset = (FSIZE_t)LOOPER_STORAGE_WAV_HEADER_BYTES + raw_offset;

    if ((f_lseek(&g_looper_raw_export.src, raw_offset) != FR_OK)
            || (f_lseek(&g_looper_raw_export.dst, wav_offset) != FR_OK))
    {
        return 0U;
    }

    if ((looper_storage_raw_export_read_exact(&g_looper_raw_export.src, raw_dst, bytes) == 0U)
            || (looper_storage_raw_export_read_exact(&g_looper_raw_export.dst, wav_dst, bytes) == 0U))
    {
        return 0U;
    }

    if (memcmp(raw_dst, wav_dst, bytes) == 0)
    {
        return 1U;
    }

    if ((record_mismatch != 0U) && (g_looper_raw_export_diag.first_mismatch_data_offset == UINT32_MAX))
    {
        for (uint32_t i = 0U; i < bytes; ++i)
        {
            if (raw_dst[i] != wav_dst[i])
            {
                g_looper_raw_export_diag.first_mismatch_data_offset =
                    (start_frame * LOOPER_STORAGE_RAW_BYTES_PER_FRAME) + i;
                break;
            }
        }
    }
    return 0U;
}

static uint8_t looper_storage_raw_export_verify(void)
{
    const uint32_t data_bytes =
        g_looper_raw_export.recorded_frames * LOOPER_STORAGE_RAW_BYTES_PER_FRAME;
    const uint32_t first_frames =
        (g_looper_raw_export.recorded_frames < LOOPER_STORAGE_EXPORT_COMPARE_FRAMES)
            ? g_looper_raw_export.recorded_frames
            : LOOPER_STORAGE_EXPORT_COMPARE_FRAMES;
    const uint32_t last_frames = first_frames;
    const uint32_t last_start =
        (g_looper_raw_export.recorded_frames > LOOPER_STORAGE_EXPORT_COMPARE_FRAMES)
            ? (g_looper_raw_export.recorded_frames - LOOPER_STORAGE_EXPORT_COMPARE_FRAMES)
            : 0U;

    memset(&g_looper_raw_export_diag, 0, sizeof(g_looper_raw_export_diag));
    looper_storage_raw_export_copy_stats_to_diag();
    g_looper_raw_export_diag.recorded_frames = g_looper_raw_export.recorded_frames;
    g_looper_raw_export_diag.raw_bytes_expected = data_bytes;
    g_looper_raw_export_diag.wav_data_bytes_written =
        g_looper_raw_export.frames_done * LOOPER_STORAGE_RAW_BYTES_PER_FRAME;
    g_looper_raw_export_diag.wav_data_offset = LOOPER_STORAGE_WAV_HEADER_BYTES;
    g_looper_raw_export_diag.first_compare_frames = first_frames;
    g_looper_raw_export_diag.last_compare_frames = last_frames;
    g_looper_raw_export_diag.first_compare_start_frame = 0U;
    g_looper_raw_export_diag.last_compare_start_frame = last_start;
    g_looper_raw_export_diag.first_mismatch_data_offset = UINT32_MAX;

    if ((g_looper_raw_export_diag.wav_data_bytes_written != data_bytes)
            || (f_size(&g_looper_raw_export.dst)
                != ((FSIZE_t)LOOPER_STORAGE_WAV_HEADER_BYTES + (FSIZE_t)data_bytes)))
    {
        g_looper_raw_export_diag.first_mismatch_data_offset = data_bytes;
        return 0U;
    }

    if (looper_storage_raw_export_compare_window(0U,
                                                 first_frames,
                                                 g_looper_raw_export_diag.first_raw_bytes,
                                                 g_looper_raw_export_diag.first_wav_bytes,
                                                 1U) == 0U)
    {
        return 0U;
    }

    if (looper_storage_raw_export_compare_window(last_start,
                                                 last_frames,
                                                 g_looper_raw_export_diag.last_raw_bytes,
                                                 g_looper_raw_export_diag.last_wav_bytes,
                                                 1U) == 0U)
    {
        return 0U;
    }

    g_looper_raw_export_diag.verified = 1U;
    return 1U;
}

void looper_storage_raw_init(void)
{
    g_looper_storage_raw_available = 0U;
    g_looper_storage_raw_last_error = LOOPER_STORAGE_RAW_ERROR_NOT_VALIDATED;
    g_looper_storage_raw_last_failed_slot = 0xFFU;
    g_looper_storage_raw_last_fresult = (uint32_t)FR_OK;
    g_looper_storage_raw_last_observed_size = 0ULL;
}

uint8_t looper_storage_raw_validate(void)
{
    g_looper_storage_raw_available = 0U;
    g_looper_storage_raw_last_error = LOOPER_STORAGE_RAW_ERROR_NONE;
    g_looper_storage_raw_last_failed_slot = 0xFFU;
    g_looper_storage_raw_last_fresult = (uint32_t)FR_OK;
    g_looper_storage_raw_last_observed_size = 0ULL;

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
    {
        g_looper_storage_raw_last_error = LOOPER_STORAGE_RAW_ERROR_SD_BUSY;
        g_looper_storage_raw_last_fresult = (uint32_t)FR_LOCKED;
        return 0U;
    }

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        g_looper_storage_raw_last_error = LOOPER_STORAGE_RAW_ERROR_MOUNT_FAIL;
        g_looper_storage_raw_last_fresult = (uint32_t)FR_NOT_READY;
        sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
        return 0U;
    }

    for (uint8_t slot = 0U; slot < LOOPER_STORAGE_RAW_SLOT_COUNT; ++slot)
    {
        FILINFO info;
        const FRESULT fr = f_stat(g_looper_storage_raw_paths[slot], &info);
        if (fr == FR_NO_FILE)
        {
            g_looper_storage_raw_last_error = LOOPER_STORAGE_RAW_ERROR_MISSING;
            g_looper_storage_raw_last_failed_slot = slot;
            g_looper_storage_raw_last_fresult = (uint32_t)fr;
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
            return 0U;
        }

        if (fr != FR_OK)
        {
            g_looper_storage_raw_last_error = LOOPER_STORAGE_RAW_ERROR_STAT_FAIL;
            g_looper_storage_raw_last_failed_slot = slot;
            g_looper_storage_raw_last_fresult = (uint32_t)fr;
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
            return 0U;
        }

        if ((uint64_t)info.fsize != (uint64_t)LOOPER_STORAGE_RAW_RESERVOIR_BYTES)
        {
            g_looper_storage_raw_last_error = LOOPER_STORAGE_RAW_ERROR_SIZE_MISMATCH;
            g_looper_storage_raw_last_failed_slot = slot;
            g_looper_storage_raw_last_fresult = (uint32_t)fr;
            g_looper_storage_raw_last_observed_size = (uint64_t)info.fsize;
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
            return 0U;
        }
    }

    g_looper_storage_raw_available = 1U;
    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    return 1U;
}

uint8_t looper_storage_raw_is_available(void)
{
    return g_looper_storage_raw_available;
}

const char *looper_storage_raw_get_path(uint8_t slot)
{
    if (slot >= LOOPER_STORAGE_RAW_SLOT_COUNT)
    {
        return 0;
    }

    return g_looper_storage_raw_paths[slot];
}

uint32_t looper_storage_raw_get_capacity_frames(void)
{
    return (uint32_t)LOOPER_STORAGE_RAW_CAPACITY_FRAMES;
}

looper_storage_raw_error_t looper_storage_raw_get_last_error(void)
{
    return g_looper_storage_raw_last_error;
}

uint8_t looper_storage_raw_get_last_failed_slot(void)
{
    return g_looper_storage_raw_last_failed_slot;
}

uint32_t looper_storage_raw_get_last_fresult(void)
{
    return g_looper_storage_raw_last_fresult;
}

uint64_t looper_storage_raw_get_last_observed_size(void)
{
    return g_looper_storage_raw_last_observed_size;
}

uint8_t looper_storage_raw_get_slot_for_track(uint8_t track_id, uint8_t *out_slot)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (out_slot == 0))
    {
        return 0U;
    }

    uint8_t looper_index = 0U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        track_runtime_descriptor_t descriptor;
        if (track_runtime_get_descriptor(track, &descriptor) == 0U)
        {
            continue;
        }

        const uint8_t is_looper =
            (uint8_t)((descriptor.family == TRACK_RUNTIME_FAMILY_SAMPLER)
                      && (descriptor.type == TRACK_RUNTIME_TYPE_LOOPER));
        if (is_looper == 0U)
        {
            continue;
        }

        if (track == track_id)
        {
            if (looper_index >= LOOPER_STORAGE_RAW_SLOT_COUNT)
            {
                return 0U;
            }

            *out_slot = looper_index;
            return 1U;
        }

        if (looper_index < UINT8_MAX)
        {
            looper_index++;
        }
    }

    return 0U;
}

uint8_t looper_storage_raw_track_is_available(uint8_t track_id)
{
    uint8_t slot = 0U;
    return (uint8_t)((g_looper_storage_raw_available != 0U)
                     && (looper_storage_raw_get_slot_for_track(track_id, &slot) != 0U));
}

uint8_t looper_storage_raw_export_start(uint8_t track_id,
                                        uint8_t raw_slot,
                                        const char *raw_path,
                                        uint32_t recorded_frames,
                                        const char *final_path)
{
    if ((track_id >= SEQ_TRACK_COUNT)
            || (raw_slot >= LOOPER_STORAGE_RAW_SLOT_COUNT)
            || (raw_path == 0)
            || (final_path == 0)
            || (recorded_frames == 0U)
            || (recorded_frames > looper_storage_raw_get_capacity_frames()))
    {
        g_looper_raw_export.error = LOOPER_STORAGE_RAW_EXPORT_ERROR_INVALID_ARG;
        return 0U;
    }

    if (multi_record_writer_any_active() != 0U)
    {
        g_looper_raw_export.error = LOOPER_STORAGE_RAW_EXPORT_ERROR_BUSY;
        return 0U;
    }

    if (g_looper_raw_export.state == LOOPER_STORAGE_RAW_EXPORT_ACTIVE)
    {
        g_looper_raw_export.error = LOOPER_STORAGE_RAW_EXPORT_ERROR_BUSY;
        return 0U;
    }

    if ((g_looper_raw_export.src_open != 0U) || (g_looper_raw_export.dst_open != 0U))
    {
        looper_storage_raw_export_set_failed(LOOPER_STORAGE_RAW_EXPORT_ERROR_BUSY);
        return 0U;
    }

    memset(&g_looper_raw_export, 0, sizeof(g_looper_raw_export));
    memset(&g_looper_raw_export_diag, 0, sizeof(g_looper_raw_export_diag));
    if ((looper_storage_copy_path_local(g_looper_raw_export.raw_path,
                                        sizeof(g_looper_raw_export.raw_path),
                                        raw_path) == 0U)
            || (looper_storage_copy_path_local(g_looper_raw_export.final_path,
                                               sizeof(g_looper_raw_export.final_path),
                                               final_path) == 0U))
    {
        g_looper_raw_export.error = LOOPER_STORAGE_RAW_EXPORT_ERROR_INVALID_ARG;
        g_looper_raw_export.state = LOOPER_STORAGE_RAW_EXPORT_FAILED;
        return 0U;
    }

    g_looper_raw_export.track_id = track_id;
    g_looper_raw_export.raw_slot = raw_slot;
    g_looper_raw_export.recorded_frames = recorded_frames;
    g_looper_raw_export.phase = LOOPER_STORAGE_EXPORT_PHASE_OPEN_SRC;
    g_looper_raw_export.error = LOOPER_STORAGE_RAW_EXPORT_ERROR_NONE;
    g_looper_raw_export.state = LOOPER_STORAGE_RAW_EXPORT_ACTIVE;
    return 1U;
}

void looper_storage_raw_export_service(uint32_t byte_budget)
{
    if ((g_looper_raw_export.state != LOOPER_STORAGE_RAW_EXPORT_ACTIVE) || (byte_budget == 0U))
    {
        return;
    }

    if (multi_record_writer_any_active() != 0U)
    {
        looper_storage_raw_export_mark_waiting();
        return;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
    {
        looper_storage_raw_export_mark_waiting();
        return;
    }
    looper_storage_raw_export_clear_waiting();
    g_looper_raw_export.gate_acquire_count++;

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        looper_storage_raw_export_set_failed(LOOPER_STORAGE_RAW_EXPORT_ERROR_MOUNT_FAIL);
        sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
        return;
    }

    if (g_looper_raw_export.phase == LOOPER_STORAGE_EXPORT_PHASE_OPEN_SRC)
    {
        const uint32_t t0 = HAL_GetTick();
        FRESULT fr = f_open(&g_looper_raw_export.src, g_looper_raw_export.raw_path, FA_READ);
        if (fr != FR_OK)
        {
            g_looper_raw_export.open_ms += HAL_GetTick() - t0;
            looper_storage_raw_export_set_failed(LOOPER_STORAGE_RAW_EXPORT_ERROR_OPEN_FAIL);
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
            return;
        }
        g_looper_raw_export.src_open = 1U;
        fr = f_lseek(&g_looper_raw_export.src, 0U);
        g_looper_raw_export.open_ms += HAL_GetTick() - t0;
        if (fr != FR_OK)
        {
            looper_storage_raw_export_set_failed(LOOPER_STORAGE_RAW_EXPORT_ERROR_SEEK_FAIL);
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
            return;
        }
        g_looper_raw_export.phase = LOOPER_STORAGE_EXPORT_PHASE_OPEN_DST;
        sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
        return;
    }

    if (g_looper_raw_export.phase == LOOPER_STORAGE_EXPORT_PHASE_OPEN_DST)
    {
        const uint32_t t0 = HAL_GetTick();
        const FRESULT fr = f_open(&g_looper_raw_export.dst,
                                  g_looper_raw_export.final_path,
                                  FA_CREATE_NEW | FA_WRITE | FA_READ);
        g_looper_raw_export.open_ms += HAL_GetTick() - t0;
        if (fr != FR_OK)
        {
            looper_storage_raw_export_set_failed(LOOPER_STORAGE_RAW_EXPORT_ERROR_OPEN_FAIL);
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
            return;
        }
        g_looper_raw_export.dst_open = 1U;
        g_looper_raw_export.phase = LOOPER_STORAGE_EXPORT_PHASE_WRITE_HEADER;
        sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
        return;
    }

    if (g_looper_raw_export.phase == LOOPER_STORAGE_EXPORT_PHASE_WRITE_HEADER)
    {
        if (byte_budget < LOOPER_STORAGE_WAV_HEADER_BYTES)
        {
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
            return;
        }

        uint8_t header[LOOPER_STORAGE_WAV_HEADER_BYTES];
        UINT bw = 0U;
        looper_storage_build_wav_header(header,
                                        g_looper_raw_export.recorded_frames
                                            * LOOPER_STORAGE_RAW_BYTES_PER_FRAME);
        const FRESULT fr = f_write(&g_looper_raw_export.dst, header, sizeof(header), &bw);
        if ((fr != FR_OK) || (bw != sizeof(header)))
        {
            looper_storage_raw_export_set_failed(LOOPER_STORAGE_RAW_EXPORT_ERROR_WRITE_FAIL);
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
            return;
        }
        g_looper_raw_export.phase = LOOPER_STORAGE_EXPORT_PHASE_COPY;
        sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
        return;
    }

    if (g_looper_raw_export.phase == LOOPER_STORAGE_EXPORT_PHASE_COPY)
    {
        const uint32_t t0 = HAL_GetTick();
        while (byte_budget >= LOOPER_STORAGE_RAW_BYTES_PER_FRAME)
        {
            const uint32_t remaining_frames =
                g_looper_raw_export.recorded_frames - g_looper_raw_export.frames_done;
            if (remaining_frames == 0U)
            {
                g_looper_raw_export.phase = LOOPER_STORAGE_EXPORT_PHASE_SYNC;
                break;
            }

            uint32_t request_frames = remaining_frames;
            uint32_t request_bytes = request_frames * LOOPER_STORAGE_RAW_BYTES_PER_FRAME;
            if (request_bytes > byte_budget)
            {
                request_bytes = byte_budget - (byte_budget % LOOPER_STORAGE_RAW_BYTES_PER_FRAME);
            }
            if (request_bytes > sizeof(g_looper_raw_export_io))
            {
                request_bytes = sizeof(g_looper_raw_export_io);
            }
            request_bytes -= request_bytes % LOOPER_STORAGE_RAW_BYTES_PER_FRAME;
            if (request_bytes == 0U)
            {
                break;
            }
            request_frames = request_bytes / LOOPER_STORAGE_RAW_BYTES_PER_FRAME;

            UINT br = 0U;
            FRESULT fr = f_read(&g_looper_raw_export.src,
                                g_looper_raw_export_io,
                                request_bytes,
                                &br);
            if ((fr != FR_OK) || (br != request_bytes))
            {
                g_looper_raw_export.copy_ms += HAL_GetTick() - t0;
                looper_storage_raw_export_set_failed(LOOPER_STORAGE_RAW_EXPORT_ERROR_READ_FAIL);
                sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
                return;
            }

            UINT bw = 0U;
            fr = f_write(&g_looper_raw_export.dst, g_looper_raw_export_io, request_bytes, &bw);
            if ((fr != FR_OK) || (bw != request_bytes))
            {
                g_looper_raw_export.copy_ms += HAL_GetTick() - t0;
                looper_storage_raw_export_set_failed(LOOPER_STORAGE_RAW_EXPORT_ERROR_WRITE_FAIL);
                sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
                return;
            }

            g_looper_raw_export.frames_done += request_frames;
            g_looper_raw_export.chunks_copied++;
            g_looper_raw_export.bytes_copied += request_bytes;
            byte_budget -= request_bytes;
        }
        g_looper_raw_export.copy_ms += HAL_GetTick() - t0;
        sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
        return;
    }

    if (g_looper_raw_export.phase == LOOPER_STORAGE_EXPORT_PHASE_SYNC)
    {
        const uint32_t t0 = HAL_GetTick();
        const FRESULT fr = f_sync(&g_looper_raw_export.dst);
        g_looper_raw_export.sync_ms += HAL_GetTick() - t0;
        if (fr != FR_OK)
        {
            looper_storage_raw_export_set_failed(LOOPER_STORAGE_RAW_EXPORT_ERROR_SYNC_FAIL);
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
            return;
        }
        g_looper_raw_export.phase = LOOPER_STORAGE_EXPORT_PHASE_VERIFY;
        sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
        return;
    }

    if (g_looper_raw_export.phase == LOOPER_STORAGE_EXPORT_PHASE_VERIFY)
    {
        const uint32_t t0 = HAL_GetTick();
        if (looper_storage_raw_export_verify() == 0U)
        {
            g_looper_raw_export.verify_ms += HAL_GetTick() - t0;
            looper_storage_raw_export_set_failed(LOOPER_STORAGE_RAW_EXPORT_ERROR_VERIFY_FAIL);
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
            return;
        }
        g_looper_raw_export.verify_ms += HAL_GetTick() - t0;
        g_looper_raw_export.phase = LOOPER_STORAGE_EXPORT_PHASE_CLOSE_DST;
        sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
        return;
    }

    if (g_looper_raw_export.phase == LOOPER_STORAGE_EXPORT_PHASE_CLOSE_DST)
    {
        const uint32_t t0 = HAL_GetTick();
        const FRESULT fr = f_close(&g_looper_raw_export.dst);
        g_looper_raw_export.close_ms += HAL_GetTick() - t0;
        g_looper_raw_export.dst_open = 0U;
        if (fr != FR_OK)
        {
            looper_storage_raw_export_set_failed(LOOPER_STORAGE_RAW_EXPORT_ERROR_CLOSE_FAIL);
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
            return;
        }
        g_looper_raw_export.phase = LOOPER_STORAGE_EXPORT_PHASE_CLOSE_SRC;
        sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
        return;
    }

    if (g_looper_raw_export.phase == LOOPER_STORAGE_EXPORT_PHASE_CLOSE_SRC)
    {
        const uint32_t t0 = HAL_GetTick();
        const FRESULT fr = f_close(&g_looper_raw_export.src);
        g_looper_raw_export.close_ms += HAL_GetTick() - t0;
        g_looper_raw_export.src_open = 0U;
        if (fr != FR_OK)
        {
            looper_storage_raw_export_set_failed(LOOPER_STORAGE_RAW_EXPORT_ERROR_CLOSE_FAIL);
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
            return;
        }
        g_looper_raw_export.error = LOOPER_STORAGE_RAW_EXPORT_ERROR_NONE;
        g_looper_raw_export.state = LOOPER_STORAGE_RAW_EXPORT_DONE;
        sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    }
}

uint8_t looper_storage_raw_export_is_active(void)
{
    return (g_looper_raw_export.state == LOOPER_STORAGE_RAW_EXPORT_ACTIVE) ? 1U : 0U;
}

looper_storage_raw_export_state_t looper_storage_raw_export_get_state(void)
{
    return g_looper_raw_export.state;
}

looper_storage_raw_export_error_t looper_storage_raw_export_get_last_error(void)
{
    return g_looper_raw_export.error;
}

looper_storage_raw_export_phase_t looper_storage_raw_export_get_phase(void)
{
    if (g_looper_raw_export.state == LOOPER_STORAGE_RAW_EXPORT_DONE)
    {
        return LOOPER_STORAGE_RAW_EXPORT_PHASE_DONE;
    }
    if (g_looper_raw_export.state == LOOPER_STORAGE_RAW_EXPORT_FAILED)
    {
        return LOOPER_STORAGE_RAW_EXPORT_PHASE_FAIL;
    }
    if (g_looper_raw_export.state != LOOPER_STORAGE_RAW_EXPORT_ACTIVE)
    {
        return LOOPER_STORAGE_RAW_EXPORT_PHASE_IDLE;
    }
    if (g_looper_raw_export.waiting != 0U)
    {
        return LOOPER_STORAGE_RAW_EXPORT_PHASE_WAIT;
    }

    switch (g_looper_raw_export.phase)
    {
        case LOOPER_STORAGE_EXPORT_PHASE_OPEN_SRC:
        case LOOPER_STORAGE_EXPORT_PHASE_OPEN_DST:
        case LOOPER_STORAGE_EXPORT_PHASE_WRITE_HEADER:
            return LOOPER_STORAGE_RAW_EXPORT_PHASE_OPEN;
        case LOOPER_STORAGE_EXPORT_PHASE_COPY:
            return LOOPER_STORAGE_RAW_EXPORT_PHASE_WRITE;
        case LOOPER_STORAGE_EXPORT_PHASE_SYNC:
        case LOOPER_STORAGE_EXPORT_PHASE_VERIFY:
        case LOOPER_STORAGE_EXPORT_PHASE_CLOSE_DST:
        case LOOPER_STORAGE_EXPORT_PHASE_CLOSE_SRC:
            return LOOPER_STORAGE_RAW_EXPORT_PHASE_VERIFY;
        default:
            break;
    }

    return LOOPER_STORAGE_RAW_EXPORT_PHASE_WAIT;
}

void looper_storage_raw_export_get_diag(looper_storage_raw_export_diag_t *out_diag)
{
    if (out_diag == 0)
    {
        return;
    }

    looper_storage_raw_export_copy_stats_to_diag();
    *out_diag = g_looper_raw_export_diag;
}

uint8_t looper_storage_raw_export_get_progress_percent(void)
{
    if (g_looper_raw_export.state == LOOPER_STORAGE_RAW_EXPORT_DONE)
    {
        return 100U;
    }
    if ((g_looper_raw_export.state == LOOPER_STORAGE_RAW_EXPORT_IDLE)
            || (g_looper_raw_export.recorded_frames == 0U))
    {
        return 0U;
    }

    uint32_t percent =
        (uint32_t)(((uint64_t)g_looper_raw_export.frames_done * 100ULL)
                   / (uint64_t)g_looper_raw_export.recorded_frames);
    if (percent > 100U)
    {
        percent = 100U;
    }
    return (uint8_t)percent;
}

const char *looper_storage_raw_export_get_final_path(void)
{
    return (g_looper_raw_export.final_path[0] != '\0') ? g_looper_raw_export.final_path : 0;
}

void looper_storage_raw_export_clear_finished(void)
{
    if ((g_looper_raw_export.state == LOOPER_STORAGE_RAW_EXPORT_DONE)
            || (g_looper_raw_export.state == LOOPER_STORAGE_RAW_EXPORT_FAILED))
    {
        memset(&g_looper_raw_export, 0, sizeof(g_looper_raw_export));
    }
}

static uint8_t looper_storage_build_final_path(uint8_t track_id,
                                               uint16_t counter,
                                               char *out_path,
                                               uint32_t out_len)
{
    if ((out_path == 0) || (out_len == 0U) || (track_id >= SEQ_TRACK_COUNT))
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
    if ((out_path == 0) || (out_len == 0U) || (track_id >= SEQ_TRACK_COUNT))
    {
        return LOOPER_STORAGE_PATH_FAIL;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
    {
        return LOOPER_STORAGE_PATH_BUSY;
    }

    looper_storage_path_result_t result = LOOPER_STORAGE_PATH_FAIL;
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        goto done;
    }

    FRESULT fr = f_mkdir("0:/PROJECT");
    if ((fr != FR_OK) && (fr != FR_EXIST))
    {
        goto done;
    }

    fr = f_mkdir("0:/PROJECT/LOOPS");
    if ((fr != FR_OK) && (fr != FR_EXIST))
    {
        goto done;
    }

    for (uint32_t attempt = 0U; attempt < LOOPER_STORAGE_SAVE_PATH_TRIES; ++attempt)
    {
        const uint16_t counter = g_looper_storage_save_counter;
        g_looper_storage_save_counter =
            (uint16_t)((g_looper_storage_save_counter + 1U) % LOOPER_STORAGE_SAVE_PATH_TRIES);
        if (looper_storage_build_final_path(track_id, counter, out_path, out_len) == 0U)
        {
            break;
        }

        FILINFO info;
        fr = f_stat(out_path, &info);
        if (fr == FR_NO_FILE)
        {
            result = LOOPER_STORAGE_PATH_OK;
            break;
        }

        if (fr != FR_OK)
        {
            break;
        }
    }

done:
    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    return result;
}
