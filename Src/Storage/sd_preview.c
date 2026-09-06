/**
 * @file sd_preview.c
 * @brief SD preview service facade.
 *
 * Rationale:
 * - Keep SD preview separate from project sample import.
 * - Own preview session state, SD gate ownership, chunked decode, resampling,
 *   and the future MAIN sink contract without coupling to the sampler runtime.
 */

#include "Storage/sd_preview.h"

#include <string.h>

#include "App/control_domain.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_stream_manager.h"
#include "Platform/memory_layout.h"
#include "Storage/looper_storage.h"
#include "Storage/audio_recorder.h"
#include "Storage/sd_access_gate.h"
#include "Storage/storage_io_wakeup.h"
#include "Storage/wav_audio_codec.h"
#include "IPC/control_audio_command.h"
#include "IPC/sd_preview_ring_contract.h"
#include "Storage/project_load_quiesce.h"
#include "stm32h7xx_hal.h"
#include "main.h"

#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#    define SD_PREVIEW_HAS_FATFS 1
#  endif
#endif

#ifndef SD_PREVIEW_HAS_FATFS
#define SD_PREVIEW_HAS_FATFS 0
#endif

#define SD_PREVIEW_TARGET_RATE   48000U
#define SD_PREVIEW_IO_BYTES       4096U
#define SD_PREVIEW_IO_DEFERRED    2U

typedef struct
{
    sd_preview_state_t state;
    sd_preview_error_t last_error;
    char path[SAMPLE_CLASSIC_PATH_MAX];
    wav_info_t info;
    uint8_t gate_held;
    uint8_t file_open;
    uint8_t stream_initialized;
    uint8_t source_exhausted;
    uint8_t stream_ended;
    uint8_t source_prev_valid;
    uint8_t source_curr_valid;
    uint8_t io_error;
    uint32_t data_remaining;
    uint32_t range_start_frame;
    uint32_t range_frame_count;
    uint32_t io_pos;
    uint32_t io_len;
    uint32_t prev_index;
    uint32_t curr_index;
    float prev_l;
    float prev_r;
    float curr_l;
    float curr_r;
    float gain;
    double phase;
    double phase_step;
#if SD_PREVIEW_HAS_FATFS
    FIL fp;
#endif
} sd_preview_ctx_t;

/*
 * These buffers are CPU-managed, not DMA-owned. Keep them out of the D2 DMA
 * MPU window so startup remains within the explicit coverage check in main().
 */
static AUDIO_COLD_SDRAM uint8_t g_sd_preview_io[SD_PREVIEW_IO_BYTES];
STORAGE_STATE_SDRAM static sd_preview_ctx_t g_sd_preview;
STORAGE_STATE_SDRAM static sd_preview_diag_t g_sd_preview_diag;
static volatile float g_sd_preview_control_gain;
static volatile uint8_t g_sd_preview_request;
static char g_sd_preview_request_path[SAMPLE_CLASSIC_PATH_MAX];
static volatile uint32_t g_sd_preview_request_start_frame;
static volatile uint32_t g_sd_preview_request_end_frame;

static uint8_t sd_preview_storage_unavailable(void);

static void sd_preview_publish_active(uint8_t active)
{
    if (control_domain_request_storage_audio_param(
            active, CONTROL_AUDIO_PARAM_PREVIEW_ACTIVE, 0U) == 0U)
        Error_Handler();
}

static uint32_t sd_preview_ring_producer_count(void)
{
    const uint32_t read_count = g_sd_preview_ring_layout.read_count;
    __DMB();
    const uint32_t write_count = g_sd_preview_ring_layout.write_count;
    return (read_count <= write_count) ? (write_count - read_count) : 0U;
}

static void sd_preview_diag_record_open_fail(const char *path, FRESULT fr)
{
    g_sd_preview_diag.preview_open_fail_count++;
    g_sd_preview_diag.gate_owner = sd_access_gate_current_owner();
    g_sd_preview_diag.gate_last_owner = sd_access_gate_last_owner();
    g_sd_preview_diag.fatfs_result = fr;
    if (path != 0)
    {
        const size_t path_len = strlen(path);
        const size_t copy_len = (path_len < sizeof(g_sd_preview_diag.path))
                                    ? path_len
                                    : (sizeof(g_sd_preview_diag.path) - 1U);
        memcpy(g_sd_preview_diag.path, path, copy_len);
        g_sd_preview_diag.path[copy_len] = '\0';
    }
    else
    {
        g_sd_preview_diag.path[0] = '\0';
    }
}

static uint8_t sd_preview_ring_push(float left, float right)
{
    const uint32_t write_count = g_sd_preview_ring_layout.write_count;
    const uint32_t read_count = g_sd_preview_ring_layout.read_count;
    if ((read_count <= write_count)
            && ((write_count - read_count) >= SD_PREVIEW_RING_FRAMES))
        return 0U;
    const uint32_t index = write_count % SD_PREVIEW_RING_FRAMES;
    g_sd_preview_ring[index * 2U] = left;
    g_sd_preview_ring[index * 2U + 1U] = right;
    __DMB();
    g_sd_preview_ring_layout.write_count = write_count + 1U;
    return 1U;
}

static void sd_preview_reset_source_state(void)
{
    g_sd_preview.stream_initialized = 0U;
    g_sd_preview.source_exhausted = 0U;
    g_sd_preview.stream_ended = 0U;
    g_sd_preview.source_prev_valid = 0U;
    g_sd_preview.source_curr_valid = 0U;
    g_sd_preview.io_error = 0U;
    g_sd_preview.data_remaining = 0U;
    g_sd_preview.io_pos = 0U;
    g_sd_preview.io_len = 0U;
    g_sd_preview.prev_index = 0U;
    g_sd_preview.curr_index = 0U;
    g_sd_preview.prev_l = 0.0f;
    g_sd_preview.prev_r = 0.0f;
    g_sd_preview.curr_l = 0.0f;
    g_sd_preview.curr_r = 0.0f;
    g_sd_preview.phase = 0.0;
    g_sd_preview.phase_step = 1.0;
}

static uint8_t sd_preview_close_file(void)
{
#if SD_PREVIEW_HAS_FATFS
    if (g_sd_preview.file_open == 0U)
    {
        return 1U;
    }
    if (sd_preview_storage_unavailable() != 0U)
    {
        memset(&g_sd_preview.fp, 0, sizeof(g_sd_preview.fp));
        g_sd_preview.file_open = 0U;
        return 1U;
    }
    uint8_t acquired = 0U;
    if (g_sd_preview.gate_held == 0U)
    {
        if (sd_access_gate_try_acquire_for_owner(
                SD_ACCESS_CLIENT_PREVIEW, (uint8_t)STORAGE_OWNER_PREVIEW) == 0U)
        {
            storage_io_owner_wait_resource(STORAGE_OWNER_PREVIEW);
            return 0U;
        }
        acquired = 1U;
    }
    (void)f_close(&g_sd_preview.fp);
    g_sd_preview.file_open = 0U;
    if (acquired != 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
    }
#endif
    return 1U;
}

static uint8_t sd_preview_clear_session(uint8_t clear_error, uint8_t clear_state)
{
    if (sd_preview_close_file() == 0U)
    {
        return 0U;
    }

    if (g_sd_preview.gate_held != 0U)
    {
        if ((clear_error == 0U) && (g_sd_preview.last_error != SD_PREVIEW_ERROR_NONE))
        {
            g_sd_preview_diag.gate_release_on_error_count++;
        }
        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        g_sd_preview.gate_held = 0U;
    }

    sd_preview_publish_active(0U);
    sd_preview_reset_source_state();

    if (clear_state != 0U)
    {
        g_sd_preview.state = SD_PREVIEW_STATE_IDLE;
        g_sd_preview.path[0] = '\0';
        memset(&g_sd_preview.info, 0, sizeof(g_sd_preview.info));
    }
    if (clear_error != 0U)
    {
        g_sd_preview.last_error = SD_PREVIEW_ERROR_NONE;
    }
    return 1U;
}

static void sd_preview_set_error(sd_preview_error_t error)
{
    g_sd_preview.last_error = error;
    g_sd_preview.state = SD_PREVIEW_STATE_ERROR;
}

static uint8_t sd_preview_storage_unavailable(void)
{
    const sd_storage_status_t status = sd_access_storage_status();
    return ((status == SD_STORAGE_STATUS_NO_MEDIA)
            || (status == SD_STORAGE_STATUS_FAULT)) ? 1U : 0U;
}

static uint8_t sd_preview_refill_io_buffer(void)
{
    if (g_sd_preview.data_remaining == 0U)
    {
        g_sd_preview.source_exhausted = 1U;
        return 0U;
    }

    uint32_t request = g_sd_preview.data_remaining;
    if (request > SD_PREVIEW_IO_BYTES)
    {
        request = SD_PREVIEW_IO_BYTES;
    }
    request -= (request % g_sd_preview.info.block_align);
    if (request == 0U)
    {
        g_sd_preview.data_remaining = 0U;
        g_sd_preview.source_exhausted = 1U;
        return 0U;
    }

#if SD_PREVIEW_HAS_FATFS
    if (sd_access_gate_try_acquire_for_owner(
            SD_ACCESS_CLIENT_PREVIEW, (uint8_t)STORAGE_OWNER_PREVIEW) == 0U)
    {
        return SD_PREVIEW_IO_DEFERRED;
    }
    UINT br = 0U;
    const FRESULT fr = f_read(&g_sd_preview.fp, g_sd_preview_io, request, &br);
    if ((fr != FR_OK) || (br < g_sd_preview.info.block_align))
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        g_sd_preview.data_remaining = 0U;
        g_sd_preview.source_exhausted = 1U;
        g_sd_preview.io_error = 1U;
        return 0U;
    }

    g_sd_preview.io_pos = 0U;
    g_sd_preview.io_len = br - (br % g_sd_preview.info.block_align);
    if (g_sd_preview.io_len == 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        g_sd_preview.data_remaining = 0U;
        g_sd_preview.source_exhausted = 1U;
        g_sd_preview.io_error = 1U;
        return 0U;
    }

    if (br > g_sd_preview.data_remaining)
    {
        g_sd_preview.data_remaining = 0U;
    }
    else
    {
        g_sd_preview.data_remaining -= br;
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
    return 1U;
#else
    g_sd_preview.data_remaining = 0U;
    g_sd_preview.source_exhausted = 1U;
    return 0U;
#endif
}

static uint8_t sd_preview_decode_next_source_frame(float *out_l, float *out_r)
{
    const uint32_t block_align = g_sd_preview.info.block_align;

    if ((out_l == 0) || (out_r == 0) || (block_align == 0U))
    {
        return 0U;
    }

    while ((g_sd_preview.io_pos + block_align) > g_sd_preview.io_len)
    {
        const uint8_t refill = sd_preview_refill_io_buffer();
        if (refill == SD_PREVIEW_IO_DEFERRED)
        {
            return SD_PREVIEW_IO_DEFERRED;
        }
        if (refill == 0U)
        {
            return 0U;
        }
    }

    wav_audio_codec_decode_stereo_frame(&g_sd_preview_io[g_sd_preview.io_pos],
                                        g_sd_preview.info.channels,
                                        g_sd_preview.info.bits_per_sample,
                                        out_l,
                                        out_r);
    g_sd_preview.io_pos += block_align;
    return 1U;
}

static uint8_t sd_preview_prepare_stream(void)
{
    if (g_sd_preview.stream_initialized != 0U)
    {
        return 1U;
    }

    if (g_sd_preview.info.block_align == 0U)
    {
        return 0U;
    }

#if SD_PREVIEW_HAS_FATFS
    if (sd_access_gate_try_acquire_for_owner(
            SD_ACCESS_CLIENT_PREVIEW, (uint8_t)STORAGE_OWNER_PREVIEW) == 0U)
    {
        return SD_PREVIEW_IO_DEFERRED;
    }
    const uint32_t start_byte = g_sd_preview.range_start_frame * g_sd_preview.info.block_align;
    if (f_lseek(&g_sd_preview.fp, g_sd_preview.info.data_offset + start_byte) != FR_OK)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        return 0U;
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
#endif

    sd_preview_reset_source_state();
    const uint32_t aligned_data_size = g_sd_preview.info.data_size
                                     - (g_sd_preview.info.data_size % g_sd_preview.info.block_align);
    const uint32_t source_frames = aligned_data_size / g_sd_preview.info.block_align;
    if (g_sd_preview.range_start_frame >= source_frames)
    {
        return 0U;
    }
    uint32_t range_frames = source_frames - g_sd_preview.range_start_frame;
    if ((g_sd_preview.range_frame_count != 0U) && (g_sd_preview.range_frame_count < range_frames))
    {
        range_frames = g_sd_preview.range_frame_count;
    }
    if (range_frames == 0U)
    {
        return 0U;
    }
    g_sd_preview.data_remaining = range_frames * g_sd_preview.info.block_align;
    g_sd_preview.phase_step = (g_sd_preview.info.sample_rate == 0U)
                                  ? 1.0
                                  : ((double)g_sd_preview.info.sample_rate / (double)SD_PREVIEW_TARGET_RATE);

    uint8_t decode = sd_preview_decode_next_source_frame(&g_sd_preview.prev_l,
                                                         &g_sd_preview.prev_r);
    if (decode == SD_PREVIEW_IO_DEFERRED)
    {
        return SD_PREVIEW_IO_DEFERRED;
    }
    if (decode == 0U)
    {
        return 0U;
    }
    g_sd_preview.source_prev_valid = 1U;
    g_sd_preview.prev_index = 0U;

    decode = sd_preview_decode_next_source_frame(&g_sd_preview.curr_l,
                                                 &g_sd_preview.curr_r);
    if (decode == 1U)
    {
        g_sd_preview.source_curr_valid = 1U;
        g_sd_preview.curr_index = 1U;
    }
    else if (decode == 0U)
    {
        g_sd_preview.curr_l = g_sd_preview.prev_l;
        g_sd_preview.curr_r = g_sd_preview.prev_r;
        g_sd_preview.source_curr_valid = 0U;
        g_sd_preview.curr_index = 0U;
        g_sd_preview.source_exhausted = 1U;
    }
    else
    {
        return SD_PREVIEW_IO_DEFERRED;
    }

    g_sd_preview.phase = 0.0;
    g_sd_preview.stream_initialized = 1U;
    return 1U;
}

static uint8_t sd_preview_ensure_source_window(uint32_t target_index)
{
    while ((g_sd_preview.source_curr_valid != 0U) && (g_sd_preview.curr_index < (target_index + 1U)))
    {
        float next_l = 0.0f;
        float next_r = 0.0f;

        g_sd_preview.prev_l = g_sd_preview.curr_l;
        g_sd_preview.prev_r = g_sd_preview.curr_r;
        g_sd_preview.prev_index = g_sd_preview.curr_index;

        const uint8_t decode = sd_preview_decode_next_source_frame(&next_l, &next_r);
        if (decode == SD_PREVIEW_IO_DEFERRED)
        {
            return SD_PREVIEW_IO_DEFERRED;
        }
        if (decode == 0U)
        {
            g_sd_preview.source_curr_valid = 0U;
            g_sd_preview.source_exhausted = 1U;
            break;
        }

        g_sd_preview.curr_l = next_l;
        g_sd_preview.curr_r = next_r;
        g_sd_preview.curr_index++;
        g_sd_preview.source_curr_valid = 1U;
    }

    if ((g_sd_preview.source_curr_valid == 0U) && (target_index > g_sd_preview.curr_index))
    {
        g_sd_preview.stream_ended = 1U;
        return 0U;
    }

    return 1U;
}

static uint8_t sd_preview_generate_one(float *out_l, float *out_r)
{
    const uint32_t target_index = (uint32_t)g_sd_preview.phase;

    if ((out_l == 0) || (out_r == 0))
    {
        return 0U;
    }

    if (g_sd_preview.stream_initialized == 0U)
    {
        const uint8_t prepare = sd_preview_prepare_stream();
        if (prepare == SD_PREVIEW_IO_DEFERRED)
        {
            return SD_PREVIEW_IO_DEFERRED;
        }
        if (prepare == 0U)
        {
            return 0U;
        }
    }

    const uint8_t ensure = sd_preview_ensure_source_window(target_index);
    if (ensure == SD_PREVIEW_IO_DEFERRED)
    {
        return SD_PREVIEW_IO_DEFERRED;
    }
    if (ensure == 0U)
    {
        return 0U;
    }

    if ((g_sd_preview.source_curr_valid == 0U) && (target_index > g_sd_preview.curr_index))
    {
        return 0U;
    }

    wav_audio_codec_resample_linear(g_sd_preview.prev_l,
                                    g_sd_preview.prev_r,
                                    g_sd_preview.curr_l,
                                    g_sd_preview.curr_r,
                                    (float)(g_sd_preview.phase - (double)target_index),
                                    out_l,
                                    out_r);
    g_sd_preview.phase += g_sd_preview.phase_step;
    return 1U;
}

static void sd_preview_fill_ring(void)
{
    while (sd_preview_ring_producer_count() < SD_PREVIEW_RING_FRAMES)
    {
        float left = 0.0f;
        float right = 0.0f;

        if (sd_preview_generate_one(&left, &right) != 1U)
        {
            break;
        }

        if (sd_preview_ring_push(left, right) == 0U)
        {
            break;
        }
    }

    if ((g_sd_preview.state == SD_PREVIEW_STATE_OPENING)
            && (sd_preview_ring_producer_count() != 0U))
    {
        __DMB();
        if (g_sd_preview_request != 0U)
            return;
        g_sd_preview.state = SD_PREVIEW_STATE_STREAMING;
        sd_preview_publish_active(1U);
    }

    if ((g_sd_preview.stream_ended != 0U)
            && (sd_preview_ring_producer_count() == 0U))
    {
        g_sd_preview.state = SD_PREVIEW_STATE_STOPPING;
        sd_preview_clear_session(1U, 1U);
    }
}

void sd_preview_init(void)
{
    memset(&g_sd_preview, 0, sizeof(g_sd_preview));
    memset(&g_sd_preview_diag, 0, sizeof(g_sd_preview_diag));
    g_sd_preview.state = SD_PREVIEW_STATE_IDLE;
    g_sd_preview.last_error = SD_PREVIEW_ERROR_NONE;
    g_sd_preview.gain = 1.0f;
    g_sd_preview_control_gain = 1.0f;
    g_sd_preview_ring_layout.write_count = 0U;
    sd_preview_reset_source_state();
    g_sd_preview_request = 0U;
    g_sd_preview_request_start_frame = 0U;
    g_sd_preview_request_end_frame = 0U;
}

uint8_t sd_preview_request_begin(const char *path)
{
    return sd_preview_request_begin_range(path, 0U, 0U);
}

uint8_t sd_preview_request_begin_range(const char *path,
                                       uint32_t start_frame,
                                       uint32_t end_frame)
{
    if ((path == NULL) || (path[0] == '\0')
        || (strlen(path) >= sizeof(g_sd_preview_request_path)))
        return 0U;
    if (sd_preview_storage_unavailable() != 0U)
        return 0U;
    const size_t path_length = strlen(path) + 1U;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (g_sd_preview_request == 2U)
    {
        if (primask == 0U) __enable_irq();
        else __set_PRIMASK(primask);
        return 0U;
    }
    memcpy(g_sd_preview_request_path, path, path_length);
    g_sd_preview_request_start_frame = start_frame;
    g_sd_preview_request_end_frame = end_frame;
    __DMB();
    g_sd_preview_request = 1U;
    if (primask == 0U) __enable_irq();
    else __set_PRIMASK(primask);
    storage_io_owner_set(STORAGE_OWNER_PREVIEW);
    storage_io_wakeup(STORAGE_IO_WAKE_RUNNABLE);
    return 1U;
}

void sd_preview_request_stop(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_sd_preview_request = 2U;
    if (primask == 0U) __enable_irq();
    else __set_PRIMASK(primask);
    storage_io_owner_set(STORAGE_OWNER_PREVIEW);
    storage_io_wakeup(STORAGE_IO_WAKE_RUNNABLE);
}

uint8_t sd_preview_is_active(void)
{
    return ((g_sd_preview.state == SD_PREVIEW_STATE_OPENING)
            || (g_sd_preview.state == SD_PREVIEW_STATE_STREAMING)) ? 1U : 0U;
}

sd_preview_state_t sd_preview_get_state(void)
{
    return g_sd_preview.state;
}

sd_preview_error_t sd_preview_get_last_error(void)
{
    return g_sd_preview.last_error;
}

const sd_preview_diag_t *sd_preview_get_diag(void)
{
    return &g_sd_preview_diag;
}

const char *sd_preview_get_path(void)
{
    return g_sd_preview.path;
}

const wav_info_t *sd_preview_get_source_info(void)
{
    return &g_sd_preview.info;
}

void sd_preview_set_gain(float gain)
{
    if (gain < 0.0f)
    {
        gain = 0.0f;
    }
    else if (gain > 1.0f)
    {
        gain = 1.0f;
    }

    g_sd_preview_control_gain = gain;
    __DMB();
}

float sd_preview_get_gain(void)
{
    return g_sd_preview_control_gain;
}

uint8_t sd_preview_begin_range(const char *path, uint32_t start_frame, uint32_t end_frame)
{
    if (project_replacement_is_active() != 0U) return 0U;
    if (audio_recorder_is_active() != 0U)
    {
        sd_preview_set_error(SD_PREVIEW_ERROR_RECORD_ACTIVE);
        return 0U;
    }

    if ((path == NULL) || (path[0] == '\0'))
    {
        sd_preview_set_error(SD_PREVIEW_ERROR_INVALID_PATH);
        return 0U;
    }
    if (sd_preview_storage_unavailable() != 0U)
    {
        sd_preview_set_error(SD_PREVIEW_ERROR_MOUNT_FAIL);
        return 0U;
    }

    if (sd_preview_is_active() != 0U)
    {
        if (sd_preview_stop() == 0U)
            return 0U;
    }

    if (sd_preview_clear_session(0U, 1U) == 0U)
        return 0U;
    g_sd_preview.state = SD_PREVIEW_STATE_OPENING;
    g_sd_preview.last_error = SD_PREVIEW_ERROR_NONE;
    g_sd_preview.range_start_frame = start_frame;
    g_sd_preview.range_frame_count = (end_frame > start_frame) ? (end_frame - start_frame) : 0U;

    {
        const size_t path_len = strlen(path);
        if (path_len >= sizeof(g_sd_preview.path))
        {
            sd_preview_set_error(SD_PREVIEW_ERROR_INVALID_PATH);
            return 0U;
        }
        memcpy(g_sd_preview.path, path, path_len + 1U);
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U)
    {
        sd_preview_set_error(SD_PREVIEW_ERROR_GATE_REFUSED);
        return 0U;
    }
    g_sd_preview.gate_held = 1U;

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        sd_preview_set_error(SD_PREVIEW_ERROR_MOUNT_FAIL);
        sd_preview_clear_session(0U, 0U);
        return 0U;
    }

#if SD_PREVIEW_HAS_FATFS
    const FRESULT open_fr = f_open(&g_sd_preview.fp, g_sd_preview.path, FA_READ);
    if (open_fr != FR_OK)
    {
        sd_preview_set_error(SD_PREVIEW_ERROR_OPEN_FAIL);
        sd_preview_diag_record_open_fail(g_sd_preview.path, open_fr);
        sd_preview_clear_session(0U, 0U);
        return 0U;
    }
    g_sd_preview.file_open = 1U;

    if (!wav_parser_parse_info(&g_sd_preview.fp, &g_sd_preview.info))
    {
        sd_preview_set_error(SD_PREVIEW_ERROR_PARSE_FAIL);
        sd_preview_clear_session(0U, 0U);
        return 0U;
    }

    if (!((g_sd_preview.info.audio_format == 1U)
          || (g_sd_preview.info.audio_format == 65534U)))
    {
        sd_preview_set_error(SD_PREVIEW_ERROR_UNSUPPORTED_FORMAT);
        sd_preview_clear_session(0U, 0U);
        return 0U;
    }

    if (!((g_sd_preview.info.channels == 1U) || (g_sd_preview.info.channels == 2U))
        || !((g_sd_preview.info.bits_per_sample == 16U)
             || (g_sd_preview.info.bits_per_sample == 24U)
             || (g_sd_preview.info.bits_per_sample == 32U))
        || (g_sd_preview.info.block_align == 0U)
        || (g_sd_preview.info.sample_rate == 0U))
    {
        sd_preview_set_error(SD_PREVIEW_ERROR_UNSUPPORTED_FORMAT);
        sd_preview_clear_session(0U, 0U);
        return 0U;
    }

    {
        const uint32_t aligned_data_size = g_sd_preview.info.data_size
                                         - (g_sd_preview.info.data_size % g_sd_preview.info.block_align);
        const uint32_t source_frames = aligned_data_size / g_sd_preview.info.block_align;
        if ((start_frame >= source_frames)
                || ((end_frame != 0U) && (end_frame <= start_frame)))
        {
            sd_preview_set_error(SD_PREVIEW_ERROR_UNSUPPORTED_FORMAT);
            sd_preview_clear_session(0U, 0U);
            return 0U;
        }
        if ((end_frame != 0U) && (end_frame < source_frames))
        {
            g_sd_preview.data_remaining = (end_frame - start_frame) * g_sd_preview.info.block_align;
        }
        else
        {
            g_sd_preview.data_remaining = aligned_data_size - (start_frame * g_sd_preview.info.block_align);
        }
    }

    const FRESULT seek_fr = f_lseek(&g_sd_preview.fp,
                                    g_sd_preview.info.data_offset
                                        + (start_frame * g_sd_preview.info.block_align));
    if (seek_fr != FR_OK)
    {
        sd_preview_set_error(SD_PREVIEW_ERROR_OPEN_FAIL);
        sd_preview_diag_record_open_fail(g_sd_preview.path, seek_fr);
        sd_preview_clear_session(0U, 0U);
        return 0U;
    }
#else
    sd_preview_set_error(SD_PREVIEW_ERROR_OPEN_FAIL);
    sd_preview_diag_record_open_fail(path, FR_INT_ERR);
    sd_preview_clear_session(0U, 0U);
    return 0U;
#endif

    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
    g_sd_preview.gate_held = 0U;

    return 1U;
}

uint8_t sd_preview_begin(const char *path)
{
    return sd_preview_begin_range(path, 0U, 0U);
}

uint8_t sd_preview_stop(void)
{
    g_sd_preview.state = SD_PREVIEW_STATE_STOPPING;
    return sd_preview_clear_session(1U, 1U);
}

void sd_preview_process(void)
{
    g_sd_preview.gain = g_sd_preview_control_gain;
    uint8_t request = 0U;
    char request_path[SAMPLE_CLASSIC_PATH_MAX];
    uint32_t request_start_frame = 0U;
    uint32_t request_end_frame = 0U;
    uint8_t request_pending = 0U;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    request = g_sd_preview_request;
    if (primask == 0U) __enable_irq();
    else __set_PRIMASK(primask);
    if ((request == 1U)
            && (sd_access_gate_current_owner() != SD_ACCESS_CLIENT_NONE
                || sd_access_gate_streaming_critical_active() != 0U))
    {
        storage_io_owner_wait_resource(STORAGE_OWNER_PREVIEW);
        return;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    if (g_sd_preview_request != 0U)
    {
        request = g_sd_preview_request;
        if (request == 1U)
        {
            request_start_frame = g_sd_preview_request_start_frame;
            request_end_frame = g_sd_preview_request_end_frame;
            memcpy(request_path, g_sd_preview_request_path,
                   sizeof(request_path));
        }
        g_sd_preview_request = 0U;
        request_pending = 1U;
    }
    if (primask == 0U) __enable_irq();
    else __set_PRIMASK(primask);
    if (request_pending != 0U)
    {
        if (request == 1U)
            (void)sd_preview_begin_range(request_path,
                                         request_start_frame,
                                         request_end_frame);
        else
            (void)sd_preview_stop();
    }
    if ((g_sd_preview.state != SD_PREVIEW_STATE_OPENING)
        && (g_sd_preview.state != SD_PREVIEW_STATE_STREAMING))
    {
        if (g_sd_preview.file_open != 0U)
        {
            sd_preview_clear_session(0U,
                                      (g_sd_preview.state == SD_PREVIEW_STATE_STOPPING)
                                          ? 1U : 0U);
        }
        return;
    }

    if (sd_preview_storage_unavailable() != 0U)
    {
        sd_preview_set_error(SD_PREVIEW_ERROR_READ_FAIL);
        sd_preview_clear_session(0U, 0U);
        return;
    }

    if ((sample_stream_manager_io_in_flight() != 0U)
        || (storage_io_owner_test(STORAGE_OWNER_STREAM) != 0U))
    {
        storage_io_owner_wait_resource(STORAGE_OWNER_PREVIEW);
        return;
    }

    if (g_sd_preview.stream_initialized == 0U)
    {
        const uint8_t prepare = sd_preview_prepare_stream();
        if (prepare == SD_PREVIEW_IO_DEFERRED)
        {
            return;
        }
        if (prepare == 0U)
        {
            sd_preview_set_error((g_sd_preview.io_error != 0U)
                                 ? SD_PREVIEW_ERROR_READ_FAIL
                                 : SD_PREVIEW_ERROR_PARSE_FAIL);
            sd_preview_clear_session(0U, 0U);
            return;
        }
    }

    sd_preview_fill_ring();

    if (g_sd_preview.io_error != 0U)
    {
        sd_preview_set_error(SD_PREVIEW_ERROR_READ_FAIL);
        sd_preview_clear_session(0U, 0U);
    }
}
