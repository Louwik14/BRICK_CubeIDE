#include "Storage/wav_convert.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_cache.h"
#include "Platform/memory_layout.h"
#include "Storage/audio_recorder.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/sd_access_gate.h"
#include "Storage/storage_shared_io.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/wav_audio_codec.h"
#include "Storage/wav_audio_stream.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#define WAV_CONVERT_TARGET_RATE 48000U
#define WAV_CONVERT_TARGET_CHANNELS 2U
#define WAV_CONVERT_TARGET_BITS 32U
#define WAV_CONVERT_TARGET_BYTES_PER_FRAME 8U
#define WAV_CONVERT_WAV_DATA_OFFSET_BYTES 512U
#define WAV_CONVERT_WAV_JUNK_BYTES 448U
#define WAV_CONVERT_PACK_FRAMES (STORAGE_SHARED_IO_BYTES / WAV_CONVERT_TARGET_BYTES_PER_FRAME)
#define WAV_CONVERT_SOURCE_IO_BYTES (32U * 1024U)
#define WAV_CONVERT_PATH_MAX 160U

_Static_assert((WAV_CONVERT_PACK_FRAMES * WAV_CONVERT_TARGET_BYTES_PER_FRAME) == STORAGE_SHARED_IO_BYTES,
               "WAV convert pack chunk must be frame-aligned");
_Static_assert((WAV_CONVERT_SOURCE_IO_BYTES % 512U) == 0U,
               "WAV convert source reads must be sector-aligned");
_Static_assert(sizeof(((wav_audio_stream_t *)0)->io_buf) == WAV_CONVERT_SOURCE_IO_BYTES,
               "WAV stream source buffer contract changed");
_Static_assert(sizeof(wav_convert_bench_t) == 256U, "WAV bench ABI size changed");
_Static_assert(offsetof(wav_convert_bench_t, active_cycles) == 0xA0U,
               "WAV bench ABI cycle offset changed");
_Static_assert(offsetof(wav_convert_bench_t, source_path_hash) == 0xF8U,
               "WAV bench ABI tail offset changed");

typedef enum
{
    WAV_CONVERT_COPY_RAW_FLOAT = 0,
    WAV_CONVERT_COPY_DECODE_48K,
    WAV_CONVERT_COPY_RESAMPLE
} wav_convert_copy_mode_t;

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
    uint32_t target_data_bytes;
    wav_convert_copy_mode_t copy_mode;
    char source_path[WAV_CONVERT_PATH_MAX];
    char temp_path[WAV_CONVERT_PATH_MAX];
    char bak_path[WAV_CONVERT_PATH_MAX];
    wav_info_t source_info;
    FIL src;
    FIL dst;
    wav_audio_stream_t stream;
} wav_convert_ctx_t;

STORAGE_SCRATCH_SDRAM static wav_convert_ctx_t g_wav_convert;
__attribute__((used, externally_visible, aligned(8)))
volatile wav_convert_bench_t g_wav_convert_bench;

static uint32_t g_wav_convert_bench_last_service_end_ms;
static uint8_t g_wav_convert_bench_have_service_end;

static uint32_t wav_convert_bench_cycles(void)
{
    return DWT->CYCCNT;
}

static uint32_t wav_convert_bench_path_hash(const char *path)
{
    uint32_t hash = 2166136261UL;
    while ((path != 0) && (*path != '\0'))
    {
        hash ^= (uint8_t)*path++;
        hash *= 16777619UL;
    }
    return hash;
}

static uint32_t wav_convert_bench_cycles_to_ms(uint64_t cycles)
{
    const uint32_t hz = g_wav_convert_bench.core_clock_hz;
    return (hz != 0U) ? (uint32_t)((cycles * 1000ULL) / hz) : 0U;
}

static void wav_convert_bench_publish_times(void)
{
    g_wav_convert_bench.active_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.active_cycles);
    g_wav_convert_bench.open_parse_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.open_parse_cycles);
    g_wav_convert_bench.read_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.read_cycles);
    g_wav_convert_bench.decode_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.decode_cycles);
    g_wav_convert_bench.src_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.src_cycles);
    g_wav_convert_bench.write_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.write_cycles);
    g_wav_convert_bench.sync_close_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.sync_close_cycles);
    g_wav_convert_bench.verify_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.verify_cycles);
    g_wav_convert_bench.replace_ms =
        wav_convert_bench_cycles_to_ms(g_wav_convert_bench.replace_cycles);
    const uint64_t named = g_wav_convert_bench.open_parse_cycles
        + g_wav_convert_bench.read_cycles + g_wav_convert_bench.decode_cycles
        + g_wav_convert_bench.src_cycles + g_wav_convert_bench.write_cycles
        + g_wav_convert_bench.sync_close_cycles
        + g_wav_convert_bench.verify_cycles
        + g_wav_convert_bench.replace_cycles;
    g_wav_convert_bench.other_active_ms = wav_convert_bench_cycles_to_ms(
        (g_wav_convert_bench.active_cycles > named)
            ? (g_wav_convert_bench.active_cycles - named) : 0ULL);
}

static void wav_convert_bench_reset(const char *path)
{
    memset((void *)&g_wav_convert_bench, 0, sizeof(g_wav_convert_bench));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_wav_convert_bench.magic = WAV_CONVERT_BENCH_MAGIC;
    g_wav_convert_bench.version = WAV_CONVERT_BENCH_VERSION;
    g_wav_convert_bench.struct_size = sizeof(g_wav_convert_bench);
    g_wav_convert_bench.status = 1U;
    g_wav_convert_bench.core_clock_hz = SystemCoreClock;
    g_wav_convert_bench.min_read_size = UINT32_MAX;
    g_wav_convert_bench.min_write_size = UINT32_MAX;
    g_wav_convert_bench.wall_start_ms = HAL_GetTick();
    g_wav_convert_bench.source_path_hash = wav_convert_bench_path_hash(path);
    g_wav_convert_bench_have_service_end = 0U;
    g_wav_convert_bench_last_service_end_ms = 0U;
}

static void wav_convert_bench_finish(uint32_t status, wav_convert_error_t error)
{
    g_wav_convert_bench.wall_end_ms = HAL_GetTick();
    g_wav_convert_bench.total_ms =
        g_wav_convert_bench.wall_end_ms - g_wav_convert_bench.wall_start_ms;
    g_wav_convert_bench.result_error = (uint32_t)error;
    wav_convert_bench_publish_times();
    g_wav_convert_bench.wall_minus_active_ms =
        (g_wav_convert_bench.total_ms > g_wav_convert_bench.active_ms)
            ? (g_wav_convert_bench.total_ms - g_wav_convert_bench.active_ms) : 0U;
    if (g_wav_convert_bench.read_calls == 0U)
        g_wav_convert_bench.min_read_size = 0U;
    if (g_wav_convert_bench.write_calls == 0U)
        g_wav_convert_bench.min_write_size = 0U;
    g_wav_convert_bench.status = status;
}

static FRESULT wav_convert_bench_read(void *context, FIL *fp, void *buffer,
                                      UINT requested, UINT *actual)
{
    (void)context;
    const uint32_t started = wav_convert_bench_cycles();
    const FRESULT result = f_read(fp, buffer, requested, actual);
    g_wav_convert_bench.read_cycles +=
        (uint32_t)(wav_convert_bench_cycles() - started);
    g_wav_convert_bench.read_calls++;
    g_wav_convert_bench.bytes_read += (actual != 0) ? *actual : 0U;
    if (requested < g_wav_convert_bench.min_read_size)
        g_wav_convert_bench.min_read_size = requested;
    if (requested > g_wav_convert_bench.max_read_size)
        g_wav_convert_bench.max_read_size = requested;
    return result;
}

static FRESULT wav_convert_bench_write(FIL *fp, const void *buffer,
                                       UINT requested, UINT *actual)
{
    const uint32_t started = wav_convert_bench_cycles();
    const FRESULT result = f_write(fp, buffer, requested, actual);
    g_wav_convert_bench.write_cycles +=
        (uint32_t)(wav_convert_bench_cycles() - started);
    g_wav_convert_bench.write_calls++;
    g_wav_convert_bench.bytes_written += (actual != 0) ? *actual : 0U;
    if (requested < g_wav_convert_bench.min_write_size)
        g_wav_convert_bench.min_write_size = requested;
    if (requested > g_wav_convert_bench.max_write_size)
        g_wav_convert_bench.max_write_size = requested;
    return result;
}

static FRESULT wav_convert_bench_seek(void *context, FIL *fp, FSIZE_t offset)
{
    (void)context;
    g_wav_convert_bench.seek_calls++;
    return f_lseek(fp, offset);
}

static FRESULT wav_convert_bench_open(FIL *fp, const char *path, BYTE mode)
{
    g_wav_convert_bench.open_calls++;
    return f_open(fp, path, mode);
}

static FRESULT wav_convert_bench_close(FIL *fp)
{
    g_wav_convert_bench.close_calls++;
    return f_close(fp);
}

static FRESULT wav_convert_bench_sync(FIL *fp)
{
    g_wav_convert_bench.sync_calls++;
    return f_sync(fp);
}

void wav_convert_init(void)
{
    memset(&g_wav_convert, 0, sizeof(g_wav_convert));
    g_wav_convert.state = WAV_CONVERT_STATE_IDLE;
    g_wav_convert.phase = WAV_CONVERT_PHASE_IDLE;
    memset((void *)&g_wav_convert_bench, 0, sizeof(g_wav_convert_bench));
    g_wav_convert_bench.magic = WAV_CONVERT_BENCH_MAGIC;
    g_wav_convert_bench.version = WAV_CONVERT_BENCH_VERSION;
    g_wav_convert_bench.struct_size = sizeof(g_wav_convert_bench);
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

static uint16_t wav_convert_read_le16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t wav_convert_read_le32(const uint8_t *src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8)
           | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
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

static void wav_convert_close_files(void)
{
    if (g_wav_convert.src_open != 0U)
    {
        (void)wav_convert_bench_close(&g_wav_convert.src);
        g_wav_convert.src_open = 0U;
    }
    if (g_wav_convert.dst_open != 0U)
    {
        (void)wav_convert_bench_close(&g_wav_convert.dst);
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
    if (g_wav_convert_bench.status == 1U)
        wav_convert_bench_finish(3U, error);
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

    wav_convert_bench_reset(g_wav_convert.source_path);
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

uint8_t wav_convert_start_destructive_canonical_locked(const char *path)
{
    if (sd_access_gate_current_owner() == SD_ACCESS_CLIENT_NONE)
    {
        return 0U;
    }
    return wav_convert_start_internal(path, 0U, 0U);
}

static uint8_t wav_convert_open_phase(void)
{
    FRESULT fr = wav_convert_bench_open(&g_wav_convert.src,
                                        g_wav_convert.source_path, FA_READ);
    if (fr != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_OPEN_FAIL);
        return 0U;
    }
    g_wav_convert.src_open = 1U;

    const wav_parser_io_hooks_t parser_hooks = {
        .read_fn = wav_convert_bench_read,
        .seek_fn = wav_convert_bench_seek,
        .context = 0,
    };
    if ((wav_parser_parse_info_with_io(&g_wav_convert.src,
                                       &g_wav_convert.source_info,
                                       &parser_hooks) == 0)
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

    fr = wav_convert_bench_open(&g_wav_convert.dst,
                                g_wav_convert.temp_path,
                                FA_CREATE_ALWAYS | FA_WRITE | FA_READ);
    if (fr != FR_OK)
    {
        wav_convert_fail((fr == FR_DENIED) ? WAV_CONVERT_ERROR_NO_SPACE : WAV_CONVERT_ERROR_OPEN_FAIL);
        return 0U;
    }
    g_wav_convert.dst_open = 1U;
    g_wav_convert.temp_created = 1U;

    if ((g_wav_convert.source_info.encoding == WAV_SAMPLE_ENCODING_IEEE_FLOAT)
        && (g_wav_convert.source_info.channels == WAV_CONVERT_TARGET_CHANNELS)
        && (g_wav_convert.source_info.bits_per_sample == WAV_CONVERT_TARGET_BITS)
        && (g_wav_convert.source_info.sample_rate == WAV_CONVERT_TARGET_RATE))
    {
        g_wav_convert.copy_mode = WAV_CONVERT_COPY_RAW_FLOAT;
    }
    else if (g_wav_convert.source_info.sample_rate == WAV_CONVERT_TARGET_RATE)
    {
        g_wav_convert.copy_mode = WAV_CONVERT_COPY_DECODE_48K;
    }
    else
    {
        g_wav_convert.copy_mode = WAV_CONVERT_COPY_RESAMPLE;
    }

    if (g_wav_convert.copy_mode == WAV_CONVERT_COPY_RESAMPLE)
    {
        wav_audio_stream_init(&g_wav_convert.stream,
                              &g_wav_convert.src,
                              &g_wav_convert.source_info,
                              WAV_CONVERT_TARGET_RATE);
        wav_audio_stream_set_read_hook(&g_wav_convert.stream,
                                       wav_convert_bench_read, 0);
    }
    if (((g_wav_convert.copy_mode == WAV_CONVERT_COPY_RESAMPLE)
         && (wav_audio_stream_start(&g_wav_convert.stream,
                                    g_wav_convert.source_info.data_offset) == 0U))
        || ((g_wav_convert.copy_mode != WAV_CONVERT_COPY_RESAMPLE)
            && (wav_convert_bench_seek(0, &g_wav_convert.src,
                        g_wav_convert.source_info.data_offset) != FR_OK)))
    {
        wav_convert_fail(WAV_CONVERT_ERROR_READ_FAIL);
        return 0U;
    }

    g_wav_convert.phase = WAV_CONVERT_PHASE_WRITE_HEADER;
    g_wav_convert_bench.source_encoding =
        (uint32_t)g_wav_convert.source_info.encoding;
    g_wav_convert_bench.source_bits_per_sample =
        g_wav_convert.source_info.bits_per_sample;
    g_wav_convert_bench.source_channels = g_wav_convert.source_info.channels;
    g_wav_convert_bench.source_sample_rate =
        g_wav_convert.source_info.sample_rate;
    g_wav_convert_bench.source_frames = g_wav_convert.source_frames;
    g_wav_convert_bench.copy_mode = (uint32_t)g_wav_convert.copy_mode;
    g_wav_convert_bench.has_src =
        (g_wav_convert.copy_mode == WAV_CONVERT_COPY_RESAMPLE) ? 1U : 0U;
    g_wav_convert_bench.target_frames = g_wav_convert.target_frames;
    return 1U;
}

static uint8_t wav_convert_write_header_phase(void)
{
    uint8_t header[WAV_CONVERT_WAV_DATA_OFFSET_BYTES];
    UINT bw = 0U;
    wav_convert_build_wav_header(header, g_wav_convert.target_data_bytes,
                                 g_wav_convert.target_frames);
    const FRESULT fr = wav_convert_bench_write(&g_wav_convert.dst, header,
                                               sizeof(header), &bw);
    if ((fr != FR_OK) || (bw != sizeof(header)))
    {
        wav_convert_fail((fr == FR_DENIED) ? WAV_CONVERT_ERROR_NO_SPACE : WAV_CONVERT_ERROR_WRITE_FAIL);
        return 0U;
    }

    g_wav_convert.phase = WAV_CONVERT_PHASE_COPY;
    return 1U;
}

static uint8_t wav_convert_write_output(uint32_t frame_count)
{
    const uint32_t bytes = frame_count * WAV_CONVERT_TARGET_BYTES_PER_FRAME;
    UINT bw = 0U;
    const FRESULT fr = wav_convert_bench_write(&g_wav_convert.dst,
                                               g_storage_shared_io, bytes, &bw);
    if ((fr != FR_OK) || (bw != bytes))
    {
        wav_convert_fail((fr == FR_DENIED) ? WAV_CONVERT_ERROR_NO_SPACE : WAV_CONVERT_ERROR_WRITE_FAIL);
        return 0U;
    }

    g_wav_convert.frames_done += frame_count;
    g_wav_convert.frames_written += frame_count;
    if (g_wav_convert.frames_written >= g_wav_convert.target_frames)
    {
        g_wav_convert.phase = WAV_CONVERT_PHASE_SYNC;
    }
    return 1U;
}

static uint8_t wav_convert_copy_raw_float(uint32_t frames)
{
    const uint32_t bytes = frames * WAV_CONVERT_TARGET_BYTES_PER_FRAME;
    UINT br = 0U;
    const FRESULT fr = wav_convert_bench_read(0, &g_wav_convert.src,
                                              g_storage_shared_io, bytes, &br);
    if ((fr != FR_OK) || (br != bytes))
    {
        wav_convert_fail(WAV_CONVERT_ERROR_READ_FAIL);
        return 0U;
    }
    return wav_convert_write_output(frames);
}

static uint8_t wav_convert_copy_decode_48k(uint32_t frames)
{
    uint32_t decoded = 0U;
    float *const dst = (float *)(void *)g_storage_shared_io;
    while (decoded < frames)
    {
        uint32_t read_frames = frames - decoded;
        const uint32_t source_capacity =
            WAV_CONVERT_SOURCE_IO_BYTES / g_wav_convert.source_info.block_align;
        if (read_frames > source_capacity)
        {
            read_frames = source_capacity;
        }
        const uint32_t bytes = read_frames * g_wav_convert.source_info.block_align;
        UINT br = 0U;
        const FRESULT fr = wav_convert_bench_read(0, &g_wav_convert.src,
                                                  g_wav_convert.stream.io_buf,
                                                  bytes, &br);
        if ((fr != FR_OK) || (br != bytes))
        {
            wav_convert_fail(WAV_CONVERT_ERROR_READ_FAIL);
            return 0U;
        }
        const uint32_t decode_started = wav_convert_bench_cycles();
        wav_audio_codec_decode_stereo_block(g_wav_convert.stream.io_buf,
                                            g_wav_convert.source_info.encoding,
                                            g_wav_convert.source_info.channels,
                                            g_wav_convert.source_info.bits_per_sample,
                                            &dst[decoded * 2U],
                                            read_frames);
        g_wav_convert_bench.decode_cycles +=
            (uint32_t)(wav_convert_bench_cycles() - decode_started);
        decoded += read_frames;
    }
    return wav_convert_write_output(frames);
}

static uint8_t wav_convert_copy_resample(uint32_t frames)
{
    const uint64_t read_before = g_wav_convert_bench.read_cycles;
    const uint32_t started = wav_convert_bench_cycles();
    const uint32_t produced = wav_audio_stream_read_frames(
        &g_wav_convert.stream,
        (float *)(void *)g_storage_shared_io,
        frames);
    if (produced == 0U)
    {
        wav_convert_fail((g_wav_convert.stream.io_error != 0U)
                             ? WAV_CONVERT_ERROR_READ_FAIL
                             : WAV_CONVERT_ERROR_UNSUPPORTED);
        return 0U;
    }
    const uint32_t elapsed = wav_convert_bench_cycles() - started;
    const uint64_t read_delta = g_wav_convert_bench.read_cycles - read_before;
    g_wav_convert_bench.src_cycles +=
        (elapsed > read_delta) ? (elapsed - read_delta) : 0ULL;
    return wav_convert_write_output(produced);
}

static uint8_t wav_convert_copy_phase(uint32_t byte_budget)
{
    if (g_wav_convert.frames_done >= g_wav_convert.target_frames)
    {
        g_wav_convert.phase = WAV_CONVERT_PHASE_SYNC;
        return 1U;
    }
    uint32_t frames_budget = byte_budget / WAV_CONVERT_TARGET_BYTES_PER_FRAME;
    if (frames_budget > WAV_CONVERT_PACK_FRAMES)
    {
        frames_budget = WAV_CONVERT_PACK_FRAMES;
    }
    const uint32_t remaining = g_wav_convert.target_frames - g_wav_convert.frames_done;
    if (frames_budget > remaining)
    {
        frames_budget = remaining;
    }
    if (frames_budget == 0U)
    {
        return 0U;
    }

    if (g_wav_convert.copy_mode == WAV_CONVERT_COPY_RAW_FLOAT)
        return wav_convert_copy_raw_float(frames_budget);
    if (g_wav_convert.copy_mode == WAV_CONVERT_COPY_DECODE_48K)
        return wav_convert_copy_decode_48k(frames_budget);
    return wav_convert_copy_resample(frames_budget);
}

static uint8_t wav_convert_sync_phase(void)
{
    FRESULT fr = wav_convert_bench_sync(&g_wav_convert.dst);
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
    FRESULT fr = wav_convert_bench_close(&g_wav_convert.dst);
    g_wav_convert.dst_open = 0U;
    if (fr != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_CLOSE_FAIL);
        return 0U;
    }

    fr = wav_convert_bench_close(&g_wav_convert.src);
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
    FIL fp;
    if (wav_convert_bench_open(&fp, g_wav_convert.temp_path, FA_READ) != FR_OK)
    {
        wav_convert_fail(WAV_CONVERT_ERROR_VERIFY_FAIL);
        return 0U;
    }
    UINT br = 0U;
    const FRESULT fr = wav_convert_bench_read(0, &fp,
                              g_wav_convert.stream.io_buf,
                              WAV_CONVERT_WAV_DATA_OFFSET_BYTES, &br);
    const FSIZE_t file_size = f_size(&fp);
    const FRESULT close_fr = wav_convert_bench_close(&fp);
    const uint8_t *const h = g_wav_convert.stream.io_buf;
    const uint8_t valid = ((fr == FR_OK)
        && (br == WAV_CONVERT_WAV_DATA_OFFSET_BYTES)
        && (close_fr == FR_OK)
        && (file_size == (FSIZE_t)(WAV_CONVERT_WAV_DATA_OFFSET_BYTES
                                   + g_wav_convert.target_data_bytes))
        && (memcmp(&h[0], "RIFF", 4U) == 0)
        && (wav_convert_read_le32(&h[4])
            == (WAV_CONVERT_WAV_DATA_OFFSET_BYTES - 8U
                + g_wav_convert.target_data_bytes))
        && (memcmp(&h[8], "WAVEfmt ", 8U) == 0)
        && (wav_convert_read_le32(&h[16]) == 16U)
        && (wav_convert_read_le16(&h[20]) == 3U)
        && (wav_convert_read_le16(&h[22]) == WAV_CONVERT_TARGET_CHANNELS)
        && (wav_convert_read_le32(&h[24]) == WAV_CONVERT_TARGET_RATE)
        && (wav_convert_read_le32(&h[28]) == 384000U)
        && (wav_convert_read_le16(&h[32]) == WAV_CONVERT_TARGET_BYTES_PER_FRAME)
        && (wav_convert_read_le16(&h[34]) == WAV_CONVERT_TARGET_BITS)
        && (memcmp(&h[36], "fact", 4U) == 0)
        && (wav_convert_read_le32(&h[40]) == 4U)
        && (wav_convert_read_le32(&h[44]) == g_wav_convert.target_frames)
        && (memcmp(&h[48], "JUNK", 4U) == 0)
        && (wav_convert_read_le32(&h[52]) == WAV_CONVERT_WAV_JUNK_BYTES)
        && (memcmp(&h[504], "data", 4U) == 0)
        && (wav_convert_read_le32(&h[508]) == g_wav_convert.target_data_bytes)) ? 1U : 0U;
    if (valid == 0U)
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
    /* The media epoch identifies card insertion/removal, not an in-place
     * filesystem mutation.  The final path and catalogue identity survive
     * this replacement. */

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
    wav_convert_bench_finish(2U, WAV_CONVERT_ERROR_NONE);
    return 1U;
}

void wav_convert_service(uint32_t byte_budget)
{
    if (g_wav_convert.state != WAV_CONVERT_STATE_ACTIVE)
    {
        return;
    }

    const uint32_t now_ms = HAL_GetTick();
    if (g_wav_convert_bench_have_service_end != 0U)
    {
        const uint32_t gap = now_ms - g_wav_convert_bench_last_service_end_ms;
        g_wav_convert_bench.total_service_gap_ms += gap;
        if (gap > g_wav_convert_bench.max_service_gap_ms)
            g_wav_convert_bench.max_service_gap_ms = gap;
    }
    g_wav_convert_bench.service_calls++;
    const wav_convert_phase_t measured_phase = g_wav_convert.phase;
    const uint64_t read_before = g_wav_convert_bench.read_cycles;
    const uint32_t service_started = wav_convert_bench_cycles();

    switch (measured_phase)
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

    const uint32_t service_elapsed =
        wav_convert_bench_cycles() - service_started;
    const uint64_t read_delta = g_wav_convert_bench.read_cycles - read_before;
    g_wav_convert_bench.active_cycles += service_elapsed;
    if (measured_phase == WAV_CONVERT_PHASE_OPEN)
    {
        g_wav_convert_bench.open_parse_cycles +=
            (service_elapsed > read_delta) ? (service_elapsed - read_delta) : 0ULL;
    }
    else if ((measured_phase == WAV_CONVERT_PHASE_SYNC)
             || (measured_phase == WAV_CONVERT_PHASE_CLOSE))
    {
        g_wav_convert_bench.sync_close_cycles += service_elapsed;
    }
    else if (measured_phase == WAV_CONVERT_PHASE_VERIFY)
    {
        g_wav_convert_bench.verify_cycles +=
            (service_elapsed > read_delta) ? (service_elapsed - read_delta) : 0ULL;
    }
    else if (measured_phase == WAV_CONVERT_PHASE_REPLACE)
    {
        g_wav_convert_bench.replace_cycles += service_elapsed;
    }
    g_wav_convert_bench_last_service_end_ms = HAL_GetTick();
    g_wav_convert_bench_have_service_end = 1U;
    wav_convert_bench_publish_times();
    if (g_wav_convert_bench.status != 1U)
    {
        g_wav_convert_bench.wall_minus_active_ms =
            (g_wav_convert_bench.total_ms > g_wav_convert_bench.active_ms)
                ? (g_wav_convert_bench.total_ms - g_wav_convert_bench.active_ms)
                : 0U;
    }
}

uint8_t wav_convert_cancel(void)
{
    if (g_wav_convert.state != WAV_CONVERT_STATE_ACTIVE)
    {
        return 0U;
    }
    wav_convert_fail(WAV_CONVERT_ERROR_NONE);
    return 1U;
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

uint32_t wav_convert_get_output_bytes_done(void)
{
    return g_wav_convert.frames_written * WAV_CONVERT_TARGET_BYTES_PER_FRAME;
}

uint32_t wav_convert_get_output_bytes_total(void)
{
    return g_wav_convert.target_data_bytes;
}

void wav_convert_clear_finished(void)
{
    if ((g_wav_convert.state == WAV_CONVERT_STATE_DONE)
        || (g_wav_convert.state == WAV_CONVERT_STATE_FAILED))
    {
        memset(&g_wav_convert, 0, sizeof(g_wav_convert));
    }
}
