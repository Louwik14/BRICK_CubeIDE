#include "Storage/wav_convert.h"

#include <string.h>

#include "Sampler/sample_cache.h"
#include "Platform/memory_layout.h"
#include "Storage/audio_recorder.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/sd_access_gate.h"
#include "Storage/storage_shared_io.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/wav_audio_stream.h"
#include "ff.h"

#define WAV_CONVERT_TARGET_RATE 48000U
#define WAV_CONVERT_TARGET_CHANNELS 2U
#define WAV_CONVERT_TARGET_BITS 32U
#define WAV_CONVERT_TARGET_BYTES_PER_FRAME 8U
#define WAV_CONVERT_WAV_DATA_OFFSET_BYTES 512U
#define WAV_CONVERT_WAV_JUNK_BYTES 448U
#define WAV_CONVERT_PACK_FRAMES (STORAGE_SHARED_IO_BYTES / WAV_CONVERT_TARGET_BYTES_PER_FRAME)
#define WAV_CONVERT_SERVICE_PACK_FRAMES 1024U
#define WAV_CONVERT_PATH_MAX 160U

_Static_assert((WAV_CONVERT_PACK_FRAMES * WAV_CONVERT_TARGET_BYTES_PER_FRAME) == STORAGE_SHARED_IO_BYTES,
               "WAV convert pack chunk must be frame-aligned");
_Static_assert(WAV_CONVERT_SERVICE_PACK_FRAMES <= WAV_CONVERT_PACK_FRAMES,
               "WAV convert service slice must fit the pack buffer");

typedef enum
{
    WAV_CONVERT_PHASE_IDLE = 0,
    WAV_CONVERT_PHASE_OPEN,
    WAV_CONVERT_PHASE_WRITE_HEADER,
    WAV_CONVERT_PHASE_COPY,
    WAV_CONVERT_PHASE_SYNC,
    WAV_CONVERT_PHASE_CLOSE,
    WAV_CONVERT_PHASE_VERIFY,
    WAV_CONVERT_PHASE_REPLACE
} wav_convert_phase_t;

typedef struct
{
    wav_convert_state_t state;
    wav_convert_error_t error;
    wav_convert_phase_t phase;
    uint8_t gate_held;
    uint8_t src_open;
    uint8_t dst_open;
    uint8_t temp_created;
    uint8_t bak_created;
    uint32_t source_frames;
    uint32_t target_frames;
    uint32_t frames_done;
    uint32_t frames_written;
    uint32_t pack_fill_frames;
    uint32_t target_data_bytes;
    char source_path[WAV_CONVERT_PATH_MAX];
    char temp_path[WAV_CONVERT_PATH_MAX];
    char bak_path[WAV_CONVERT_PATH_MAX];
    wav_info_t source_info;
    FIL src;
    FIL dst;
    wav_audio_stream_t stream;
} wav_convert_ctx_t;

STORAGE_SCRATCH_SDRAM static wav_convert_ctx_t g_wav_convert;

void wav_convert_init(void)
{
    memset(&g_wav_convert, 0, sizeof(g_wav_convert));
    g_wav_convert.state = WAV_CONVERT_STATE_IDLE;
    g_wav_convert.phase = WAV_CONVERT_PHASE_IDLE;
}

static void wav_convert_write_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void wav_convert_write_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFUL);
    dst[1] = (uint8_t)((value >> 8) & 0xFFUL);
    dst[2] = (uint8_t)((value >> 16) & 0xFFUL);
    dst[3] = (uint8_t)((value >> 24) & 0xFFUL);
}

static void wav_convert_build_wav_header(uint8_t *header,
                                         uint32_t data_bytes,
                                         uint32_t frame_count)
{
    const uint16_t block_align = WAV_CONVERT_TARGET_BYTES_PER_FRAME;
    const uint32_t byte_rate = WAV_CONVERT_TARGET_RATE * (uint32_t)block_align;

    memset(header, 0, WAV_CONVERT_WAV_DATA_OFFSET_BYTES);
    memcpy(&header[0], "RIFF", 4U);
    wav_convert_write_le32(&header[4], (WAV_CONVERT_WAV_DATA_OFFSET_BYTES - 8U) + data_bytes);
    memcpy(&header[8], "WAVE", 4U);
    memcpy(&header[12], "fmt ", 4U);
    wav_convert_write_le32(&header[16], 16U);
    wav_convert_write_le16(&header[20], 3U);
    wav_convert_write_le16(&header[22], WAV_CONVERT_TARGET_CHANNELS);
    wav_convert_write_le32(&header[24], WAV_CONVERT_TARGET_RATE);
    wav_convert_write_le32(&header[28], byte_rate);
    wav_convert_write_le16(&header[32], block_align);
    wav_convert_write_le16(&header[34], WAV_CONVERT_TARGET_BITS);
    memcpy(&header[36], "fact", 4U);
    wav_convert_write_le32(&header[40], 4U);
    wav_convert_write_le32(&header[44], frame_count);
    memcpy(&header[48], "JUNK", 4U);
    wav_convert_write_le32(&header[52], WAV_CONVERT_WAV_JUNK_BYTES);
    memcpy(&header[504], "data", 4U);
    wav_convert_write_le32(&header[508], data_bytes);
}

static uint8_t wav_convert_copy_path(char *dst, const char *src)
{
    if ((dst == 0) || (src == 0) || (src[0] == '\0'))
    {
        return 0U;
    }

    uint32_t i = 0U;
    while ((i + 1U) < WAV_CONVERT_PATH_MAX)
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

static uint8_t wav_convert_make_side_paths(const char *path, char *temp_path, char *bak_path)
{
    if ((path == 0) || (temp_path == 0) || (bak_path == 0))
    {
        return 0U;
    }
    if ((wav_convert_copy_path(temp_path, path) == 0U)
        || (wav_convert_copy_path(bak_path, path) == 0U))
    {
        return 0U;
    }

    const uint32_t len = (uint32_t)strlen(path);
    if ((len < 4U) || (path[len - 4U] != '.'))
    {
        return 0U;
    }

    temp_path[len - 3U] = 'B';
    temp_path[len - 2U] = '6';
    temp_path[len - 1U] = 'T';
    bak_path[len - 3U] = 'B';
    bak_path[len - 2U] = '6';
    bak_path[len - 1U] = 'B';
    return 1U;
}

static uint8_t wav_convert_format_convertible(const wav_info_t *info)
{
    if (info == 0)
    {
        return 0U;
    }

    return wav_parser_format_supported(info);
}

static uint8_t wav_convert_format_already_target(const wav_info_t *info)
{
    if (info == 0)
    {
        return 0U;
    }

    return wav_parser_is_canonical_brick_float(info);
}

static uint8_t wav_convert_parse_path_locked(const char *path, wav_info_t *out_info)
{
    FIL fp;
    FRESULT fr = f_open(&fp, path, FA_READ);
    if (fr != FR_OK)
    {
        return 0U;
    }

    const uint8_t ok = (wav_parser_parse_info(&fp, out_info) != 0) ? 1U : 0U;
    (void)f_close(&fp);
    return ok;
}

wav_convert_path_status_t wav_convert_path_canonical_status(
    const char *path, wav_info_t *out_info)
{
    wav_info_t info;
    char temp_path[WAV_CONVERT_PATH_MAX];
    char bak_path[WAV_CONVERT_PATH_MAX];
    if ((path == 0) || (path[0] == '\0'))
    {
        return WAV_CONVERT_PATH_INVALID;
    }

    if (audio_recorder_is_active() != 0U)
    {
        return WAV_CONVERT_PATH_BUSY;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_WAV_CONVERT) == 0U)
    {
        return WAV_CONVERT_PATH_BUSY;
    }

    wav_convert_path_status_t status = WAV_CONVERT_PATH_INVALID;
    if ((sd_access_fs_mount_if_needed() != 0U)
        && (wav_convert_make_side_paths(path, temp_path, bak_path) != 0U)
        && (persistent_fatfs_recover_replace(path, temp_path, bak_path) == FR_OK)
        && (wav_convert_parse_path_locked(path, &info) != 0U)
        && (wav_convert_format_convertible(&info) != 0U))
    {
        if (out_info != 0)
        {
            *out_info = info;
        }
        status = (wav_convert_format_already_target(&info) != 0U)
                     ? WAV_CONVERT_PATH_CANONICAL
                     : WAV_CONVERT_PATH_NEEDS_CANONICAL;
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_WAV_CONVERT);
    return status;
}

uint8_t wav_convert_path_needs_canonical(const char *path, wav_info_t *out_info)
{
    return (wav_convert_path_canonical_status(path, out_info)
            == WAV_CONVERT_PATH_NEEDS_CANONICAL) ? 1U : 0U;
}

static void wav_convert_pack_float32_le(uint8_t *dst, float value)
{
    uint32_t bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    wav_convert_write_le32(dst, bits);
}

static void wav_convert_pack_frame(uint8_t *dst, float left, float right)
{
    wav_convert_pack_float32_le(dst, left);
    wav_convert_pack_float32_le(dst + 4U, right);
}

static void wav_convert_close_files(void)
{
    if (g_wav_convert.src_open != 0U)
    {
        (void)f_close(&g_wav_convert.src);
        g_wav_convert.src_open = 0U;
    }
    if (g_wav_convert.dst_open != 0U)
    {
        (void)f_close(&g_wav_convert.dst);
        g_wav_convert.dst_open = 0U;
    }
}

static void wav_convert_release_gate(void)
{
    if (g_wav_convert.gate_held != 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_WAV_CONVERT);
        g_wav_convert.gate_held = 0U;
    }
}

static void wav_convert_fail(wav_convert_error_t error)
{
    wav_convert_close_files();
    if ((g_wav_convert.temp_created != 0U) && (g_wav_convert.bak_created == 0U))
    {
        (void)f_unlink(g_wav_convert.temp_path);
        g_wav_convert.temp_created = 0U;
    }
    wav_convert_release_gate();
    g_wav_convert.error = error;
    g_wav_convert.state = WAV_CONVERT_STATE_FAILED;
}

static uint8_t wav_convert_start_internal(const char *path, uint8_t acquire_gate,
                                          uint8_t allow_project_replacement)
{
    if ((allow_project_replacement == 0U)
        && (project_replacement_is_active() != 0U)) return 0U;
    if ((path == 0) || (path[0] == '\0'))
    {
        return 0U;
    }
    if ((g_wav_convert.state == WAV_CONVERT_STATE_ACTIVE)
        || (audio_recorder_is_active() != 0U)
        || (sample_cache_has_pending_sd_work() != 0U))
    {
        return 0U;
    }

    memset(&g_wav_convert, 0, sizeof(g_wav_convert));
    if ((wav_convert_copy_path(g_wav_convert.source_path, path) == 0U)
        || (wav_convert_make_side_paths(path,
                                        g_wav_convert.temp_path,
                                        g_wav_convert.bak_path) == 0U))
    {
        g_wav_convert.state = WAV_CONVERT_STATE_FAILED;
        g_wav_convert.error = WAV_CONVERT_ERROR_INVALID_ARG;
        return 0U;
    }

    if ((acquire_gate != 0U)
        && (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_WAV_CONVERT) == 0U))
    {
        g_wav_convert.state = WAV_CONVERT_STATE_FAILED;
        g_wav_convert.error = WAV_CONVERT_ERROR_BUSY;
        return 0U;
    }
    g_wav_convert.gate_held = acquire_gate;

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_MOUNT_FAIL);
        return 0U;
    }

    if (persistent_fatfs_recover_replace(g_wav_convert.source_path,
                                         g_wav_convert.temp_path,
                                         g_wav_convert.bak_path) != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_REPLACE_FAIL);
        return 0U;
    }

    g_wav_convert.state = WAV_CONVERT_STATE_ACTIVE;
    g_wav_convert.error = WAV_CONVERT_ERROR_NONE;
    g_wav_convert.phase = WAV_CONVERT_PHASE_OPEN;
    return 1U;
}

uint8_t wav_convert_start_destructive_canonical(const char *path)
{
    return wav_convert_start_internal(path, 1U, 0U);
}

uint8_t wav_convert_start_destructive_canonical_project(const char *path)
{
    return wav_convert_start_internal(path, 1U, 1U);
}

uint8_t wav_convert_path_to_canonical_locked(const char *path)
{
    wav_info_t info;
    char temp_path[WAV_CONVERT_PATH_MAX];
    char bak_path[WAV_CONVERT_PATH_MAX];
    if ((path == 0) || (path[0] == '\0')
        || (sd_access_gate_current_owner() == SD_ACCESS_CLIENT_NONE)
        || (wav_convert_make_side_paths(path, temp_path, bak_path) == 0U)
        || (persistent_fatfs_recover_replace(path, temp_path, bak_path) != FR_OK)
        || (wav_convert_parse_path_locked(path, &info) == 0U)
        || (wav_convert_format_convertible(&info) == 0U)) return 0U;
    if (wav_convert_format_already_target(&info) != 0U) return 1U;
    if (wav_convert_start_internal(path, 0U, 0U) == 0U) return 0U;
    while (g_wav_convert.state == WAV_CONVERT_STATE_ACTIVE)
        wav_convert_service(UINT32_MAX);
    const uint8_t ok = (g_wav_convert.state == WAV_CONVERT_STATE_DONE) ? 1U : 0U;
    wav_convert_clear_finished();
    return ok;
}

static uint8_t wav_convert_open_phase(void)
{
    FRESULT fr = f_open(&g_wav_convert.src, g_wav_convert.source_path, FA_READ);
    if (fr != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_OPEN_FAIL);
        return 0U;
    }
    g_wav_convert.src_open = 1U;

    if ((wav_parser_parse_info(&g_wav_convert.src, &g_wav_convert.source_info) == 0)
        || (wav_convert_format_convertible(&g_wav_convert.source_info) == 0U)
        || (wav_convert_format_already_target(&g_wav_convert.source_info) != 0U))
    {
        wav_convert_fail(WAV_CONVERT_ERROR_UNSUPPORTED);
        return 0U;
    }

    g_wav_convert.source_frames =
        g_wav_convert.source_info.data_size / g_wav_convert.source_info.block_align;
    const uint64_t target_frames =
        ((uint64_t)g_wav_convert.source_frames * WAV_CONVERT_TARGET_RATE
         + (uint64_t)g_wav_convert.source_info.sample_rate - 1ULL)
        / (uint64_t)g_wav_convert.source_info.sample_rate;
    if ((target_frames == 0ULL) || (target_frames > UINT32_MAX)
        || ((target_frames * WAV_CONVERT_TARGET_BYTES_PER_FRAME)
            > (uint64_t)(UINT32_MAX - WAV_CONVERT_WAV_DATA_OFFSET_BYTES)))
    {
        wav_convert_fail(WAV_CONVERT_ERROR_UNSUPPORTED);
        return 0U;
    }
    g_wav_convert.target_frames = (uint32_t)target_frames;
    g_wav_convert.target_data_bytes =
        g_wav_convert.target_frames * WAV_CONVERT_TARGET_BYTES_PER_FRAME;

    FILINFO bak_info;
    fr = f_stat(g_wav_convert.bak_path, &bak_info);
    if (fr == FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_REPLACE_FAIL);
        return 0U;
    }

    fr = f_open(&g_wav_convert.dst,
                g_wav_convert.temp_path,
                FA_CREATE_ALWAYS | FA_WRITE | FA_READ);
    if (fr != FR_OK)
    {
        wav_convert_fail((fr == FR_DENIED) ? WAV_CONVERT_ERROR_NO_SPACE : WAV_CONVERT_ERROR_OPEN_FAIL);
        return 0U;
    }
    g_wav_convert.dst_open = 1U;
    g_wav_convert.temp_created = 1U;

    wav_audio_stream_init(&g_wav_convert.stream,
                          &g_wav_convert.src,
                          &g_wav_convert.source_info,
                          WAV_CONVERT_TARGET_RATE);
    if (wav_audio_stream_start(&g_wav_convert.stream, g_wav_convert.source_info.data_offset) == 0U)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_READ_FAIL);
        return 0U;
    }

    g_wav_convert.phase = WAV_CONVERT_PHASE_WRITE_HEADER;
    return 1U;
}

static uint8_t wav_convert_write_header_phase(void)
{
    uint8_t header[WAV_CONVERT_WAV_DATA_OFFSET_BYTES];
    UINT bw = 0U;
    wav_convert_build_wav_header(header, g_wav_convert.target_data_bytes,
                                 g_wav_convert.target_frames);
    const FRESULT fr = f_write(&g_wav_convert.dst, header, sizeof(header), &bw);
    if ((fr != FR_OK) || (bw != sizeof(header)))
    {
        wav_convert_fail((fr == FR_DENIED) ? WAV_CONVERT_ERROR_NO_SPACE : WAV_CONVERT_ERROR_WRITE_FAIL);
        return 0U;
    }

    g_wav_convert.phase = WAV_CONVERT_PHASE_COPY;
    return 1U;
}

static uint8_t wav_convert_flush_pack(uint32_t byte_budget)
{
    if (g_wav_convert.pack_fill_frames == 0U)
    {
        if (g_wav_convert.frames_written >= g_wav_convert.target_frames)
        {
            g_wav_convert.phase = WAV_CONVERT_PHASE_SYNC;
        }
        return 1U;
    }

    const uint32_t bytes = g_wav_convert.pack_fill_frames * WAV_CONVERT_TARGET_BYTES_PER_FRAME;
    if (byte_budget < bytes)
    {
        return 0U;
    }

    UINT bw = 0U;
    const FRESULT fr = f_write(&g_wav_convert.dst, g_storage_shared_io, bytes, &bw);
    if ((fr != FR_OK) || (bw != bytes))
    {
        wav_convert_fail((fr == FR_DENIED) ? WAV_CONVERT_ERROR_NO_SPACE : WAV_CONVERT_ERROR_WRITE_FAIL);
        return 0U;
    }

    g_wav_convert.frames_written += g_wav_convert.pack_fill_frames;
    g_wav_convert.pack_fill_frames = 0U;
    if (g_wav_convert.frames_written >= g_wav_convert.target_frames)
    {
        g_wav_convert.phase = WAV_CONVERT_PHASE_SYNC;
    }
    return 1U;
}

static uint8_t wav_convert_copy_phase(uint32_t byte_budget)
{
    if (byte_budget < WAV_CONVERT_TARGET_BYTES_PER_FRAME)
    {
        return 0U;
    }

    if ((g_wav_convert.pack_fill_frames >= WAV_CONVERT_PACK_FRAMES)
        || ((g_wav_convert.frames_done >= g_wav_convert.target_frames)
            && (g_wav_convert.pack_fill_frames != 0U)))
    {
        return wav_convert_flush_pack(byte_budget);
    }

    if (g_wav_convert.frames_done >= g_wav_convert.target_frames)
    {
        g_wav_convert.phase = WAV_CONVERT_PHASE_SYNC;
        return 1U;
    }

    uint32_t frames_budget = byte_budget / WAV_CONVERT_TARGET_BYTES_PER_FRAME;
    if (frames_budget > WAV_CONVERT_SERVICE_PACK_FRAMES)
    {
        frames_budget = WAV_CONVERT_SERVICE_PACK_FRAMES;
    }

    const uint32_t pack_room = WAV_CONVERT_PACK_FRAMES - g_wav_convert.pack_fill_frames;
    if (frames_budget > pack_room)
    {
        frames_budget = pack_room;
    }

    const uint32_t remaining = g_wav_convert.target_frames - g_wav_convert.frames_done;
    if (frames_budget > remaining)
    {
        frames_budget = remaining;
    }
    if (frames_budget == 0U)
    {
        g_wav_convert.phase = WAV_CONVERT_PHASE_SYNC;
        return 1U;
    }

    uint32_t packed = 0U;
    for (; packed < frames_budget; ++packed)
    {
        float left = 0.0f;
        float right = 0.0f;
        if (wav_audio_stream_next_frame(&g_wav_convert.stream, &left, &right) == 0U)
        {
            break;
        }
        wav_convert_pack_frame(&g_storage_shared_io[(g_wav_convert.pack_fill_frames + packed)
                                                    * WAV_CONVERT_TARGET_BYTES_PER_FRAME],
                               left,
                               right);
    }

    if (packed == 0U)
    {
        wav_convert_fail((g_wav_convert.stream.io_error != 0U)
                             ? WAV_CONVERT_ERROR_READ_FAIL
                             : WAV_CONVERT_ERROR_UNSUPPORTED);
        return 0U;
    }

    g_wav_convert.pack_fill_frames += packed;
    g_wav_convert.frames_done += packed;
    return 1U;
}

static uint8_t wav_convert_sync_phase(void)
{
    FRESULT fr = f_sync(&g_wav_convert.dst);
    if (fr != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_SYNC_FAIL);
        return 0U;
    }

    g_wav_convert.phase = WAV_CONVERT_PHASE_CLOSE;
    return 1U;
}

static uint8_t wav_convert_close_phase(void)
{
    FRESULT fr = f_close(&g_wav_convert.dst);
    g_wav_convert.dst_open = 0U;
    if (fr != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_CLOSE_FAIL);
        return 0U;
    }

    fr = f_close(&g_wav_convert.src);
    g_wav_convert.src_open = 0U;
    if (fr != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_CLOSE_FAIL);
        return 0U;
    }

    g_wav_convert.phase = WAV_CONVERT_PHASE_VERIFY;
    return 1U;
}

static uint8_t wav_convert_verify_phase(void)
{
    wav_info_t info;
    if (wav_convert_parse_path_locked(g_wav_convert.temp_path, &info) == 0U)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_VERIFY_FAIL);
        return 0U;
    }

    if ((wav_parser_is_canonical_brick_float(&info) == 0U)
        || (info.data_size != g_wav_convert.target_data_bytes))
    {
        wav_convert_fail(WAV_CONVERT_ERROR_VERIFY_FAIL);
        return 0U;
    }

    g_wav_convert.phase = WAV_CONVERT_PHASE_REPLACE;
    return 1U;
}

static uint8_t wav_convert_replace_phase(void)
{
    FRESULT fr = f_rename(g_wav_convert.source_path, g_wav_convert.bak_path);
    if (fr != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_REPLACE_FAIL);
        return 0U;
    }
    g_wav_convert.bak_created = 1U;

    fr = f_rename(g_wav_convert.temp_path, g_wav_convert.source_path);
    if (fr != FR_OK)
    {
        if (f_rename(g_wav_convert.bak_path, g_wav_convert.source_path) == FR_OK)
            g_wav_convert.bak_created = 0U;
        wav_convert_fail(WAV_CONVERT_ERROR_REPLACE_FAIL);
        return 0U;
    }
    g_wav_convert.temp_created = 0U;
    sd_access_media_epoch_advance();

    if (f_unlink(g_wav_convert.bak_path) != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_REPLACE_FAIL);
        return 0U;
    }
    g_wav_convert.bak_created = 0U;
    wav_convert_release_gate();
    g_wav_convert.phase = WAV_CONVERT_PHASE_IDLE;
    g_wav_convert.state = WAV_CONVERT_STATE_DONE;
    g_wav_convert.error = WAV_CONVERT_ERROR_NONE;
    return 1U;
}

void wav_convert_service(uint32_t byte_budget)
{
    if (g_wav_convert.state != WAV_CONVERT_STATE_ACTIVE)
    {
        return;
    }

    switch (g_wav_convert.phase)
    {
        case WAV_CONVERT_PHASE_OPEN:
            (void)wav_convert_open_phase();
            break;
        case WAV_CONVERT_PHASE_WRITE_HEADER:
            (void)wav_convert_write_header_phase();
            break;
        case WAV_CONVERT_PHASE_COPY:
            (void)wav_convert_copy_phase(byte_budget);
            break;
        case WAV_CONVERT_PHASE_SYNC:
            (void)wav_convert_sync_phase();
            break;
        case WAV_CONVERT_PHASE_CLOSE:
            (void)wav_convert_close_phase();
            break;
        case WAV_CONVERT_PHASE_VERIFY:
            (void)wav_convert_verify_phase();
            break;
        case WAV_CONVERT_PHASE_REPLACE:
            (void)wav_convert_replace_phase();
            break;
        default:
            wav_convert_fail(WAV_CONVERT_ERROR_INVALID_ARG);
            break;
    }
}

uint8_t wav_convert_is_active(void)
{
    return (g_wav_convert.state == WAV_CONVERT_STATE_ACTIVE) ? 1U : 0U;
}

wav_convert_state_t wav_convert_get_state(void)
{
    return g_wav_convert.state;
}

wav_convert_error_t wav_convert_get_last_error(void)
{
    return g_wav_convert.error;
}

uint8_t wav_convert_get_progress_percent(void)
{
    if (g_wav_convert.state == WAV_CONVERT_STATE_DONE)
    {
        return 100U;
    }
    if ((g_wav_convert.state == WAV_CONVERT_STATE_IDLE)
        || (g_wav_convert.target_frames == 0U))
    {
        return 0U;
    }

    uint32_t percent =
        (uint32_t)(((uint64_t)g_wav_convert.frames_done * 95ULL)
                   / (uint64_t)g_wav_convert.target_frames);
    if ((g_wav_convert.phase == WAV_CONVERT_PHASE_VERIFY)
        || (g_wav_convert.phase == WAV_CONVERT_PHASE_REPLACE))
    {
        percent = 98U;
    }
    if (percent > 100U)
    {
        percent = 100U;
    }
    return (uint8_t)percent;
}

void wav_convert_clear_finished(void)
{
    if ((g_wav_convert.state == WAV_CONVERT_STATE_DONE)
        || (g_wav_convert.state == WAV_CONVERT_STATE_FAILED))
    {
        memset(&g_wav_convert, 0, sizeof(g_wav_convert));
    }
}
