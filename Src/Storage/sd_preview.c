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

#include "Sampler/sample_cache.h"
#include "Storage/memory_layout.h"
#include "Storage/looper_storage.h"
#include "Storage/audio_recorder.h"
#include "Storage/sd_access_gate.h"
#include "Storage/wav_audio_codec.h"
#include "Audio/control_audio_command.h"
#include "Audio/control_audio_fifo.h"
#include "Core/control_audio_publication.h"
#include "Core/live_clock.h"
#include "stm32h7xx_hal.h"

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
#define SD_PREVIEW_RING_FRAMES    2048U
#define SD_PREVIEW_IO_BYTES       4096U

typedef struct
{
    sd_preview_state_t state;
    sd_preview_error_t last_error;
    char path[SAMPLE_POOL_PATH_MAX];
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
static AUDIO_STORAGE_SHARED_SDRAM float
    g_sd_preview_ring[SD_PREVIEW_RING_FRAMES * 2U];
static AUDIO_COLD_SDRAM uint8_t g_sd_preview_io[SD_PREVIEW_IO_BYTES];
STORAGE_STATE_SDRAM static sd_preview_ctx_t g_sd_preview;
STORAGE_STATE_SDRAM static sd_preview_diag_t g_sd_preview_diag;

typedef struct
{
    volatile uint32_t epoch;
    volatile uint32_t write_count;
    volatile uint32_t read_count;
    volatile float gain;
    volatile uint8_t active;
    uint8_t reserved[15];
} sd_preview_ring_ipc_t;

_Static_assert(sizeof(sd_preview_ring_ipc_t) == 32U,
               "Preview ring IPC ABI changed");

D3_IPC static sd_preview_ring_ipc_t g_sd_preview_ring_ipc;
static uint32_t g_sd_preview_control_epoch;
static uint32_t g_sd_preview_reset_fence;
static uint8_t g_sd_preview_reset_pending;
static uint8_t g_sd_preview_reset_publish_failed;

static void sd_preview_publish_active(uint8_t active)
{
    uint64_t sample_time = 0U;
    if (live_clock_read_audio_sample(&sample_time))
        (void)control_audio_publish_param(active, CONTROL_AUDIO_PARAM_PREVIEW_ACTIVE,
                                          g_sd_preview_control_epoch, 0U,
                                          sample_time);
}

static void sd_preview_ring_request_reset(void)
{
    if (g_sd_preview_reset_pending != 0U) return;
    ++g_sd_preview_control_epoch;
    if (g_sd_preview_control_epoch == 0U) g_sd_preview_control_epoch = 1U;
    g_sd_preview_reset_fence = 0U;
    g_sd_preview_reset_publish_failed = 0U;
    g_sd_preview_reset_pending = 1U;
}

static uint8_t sd_preview_ring_service_reset(void)
{
    if (g_sd_preview_reset_pending == 0U) return 1U;
    if (g_sd_preview_reset_publish_failed != 0U) return 0U;
    if (g_sd_preview_reset_fence == 0U)
    {
        uint64_t sample_time = 0U;
        if (!live_clock_read_audio_sample(&sample_time)
                || (control_audio_fifo_control_free() == 0U))
            return 0U;
        if (!control_audio_publish_param_fenced(
                0U, CONTROL_AUDIO_PARAM_PREVIEW_ACTIVE,
                g_sd_preview_control_epoch, 0U, sample_time,
                &g_sd_preview_reset_fence))
        {
            g_sd_preview_reset_publish_failed = 1U;
            return 0U;
        }
    }
    if (!control_audio_consumer_fence_consumed(g_sd_preview_reset_fence))
        return 0U;
    g_sd_preview_ring_ipc.write_count = 0U;
    __DMB();
    g_sd_preview_reset_fence = 0U;
    g_sd_preview_reset_pending = 0U;
    return 1U;
}

static uint32_t sd_preview_ring_producer_count(void)
{
    const uint32_t read_count = g_sd_preview_ring_ipc.read_count;
    __DMB();
    const uint32_t write_count = g_sd_preview_ring_ipc.write_count;
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
    const uint32_t write_count = g_sd_preview_ring_ipc.write_count;
    const uint32_t read_count = g_sd_preview_ring_ipc.read_count;
    if ((read_count <= write_count)
            && ((write_count - read_count) >= SD_PREVIEW_RING_FRAMES))
        return 0U;
    const uint32_t index = write_count % SD_PREVIEW_RING_FRAMES;
    g_sd_preview_ring[index * 2U] = left;
    g_sd_preview_ring[index * 2U + 1U] = right;
    __DMB();
    g_sd_preview_ring_ipc.write_count = write_count + 1U;
    return 1U;
}

static uint8_t sd_preview_ring_pop(float *left, float *right)
{
    static uint32_t consumer_epoch;
    const uint32_t epoch = g_sd_preview_ring_ipc.epoch;
    if (consumer_epoch != epoch)
    {
        consumer_epoch = epoch;
        g_sd_preview_ring_ipc.read_count = 0U;
        __DMB();
    }
    const uint32_t read_count = g_sd_preview_ring_ipc.read_count;
    const uint32_t write_count = g_sd_preview_ring_ipc.write_count;
    __DMB();
    if ((read_count == write_count) || (left == 0) || (right == 0))
        return 0U;
    const uint32_t index = read_count % SD_PREVIEW_RING_FRAMES;
    *left = g_sd_preview_ring[index * 2U];
    *right = g_sd_preview_ring[index * 2U + 1U];
    __DMB();
    g_sd_preview_ring_ipc.read_count = read_count + 1U;
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

static void sd_preview_clear_session(uint8_t clear_error, uint8_t clear_state)
{
#if SD_PREVIEW_HAS_FATFS
    if (g_sd_preview.file_open != 0U)
    {
        (void)f_close(&g_sd_preview.fp);
        g_sd_preview.file_open = 0U;
    }
#endif

    if (g_sd_preview.gate_held != 0U)
    {
        if ((clear_error == 0U) && (g_sd_preview.last_error != SD_PREVIEW_ERROR_NONE))
        {
            g_sd_preview_diag.gate_release_on_error_count++;
        }
        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        g_sd_preview.gate_held = 0U;
    }

    sd_preview_ring_request_reset();
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
}

static void sd_preview_set_error(sd_preview_error_t error)
{
    g_sd_preview.last_error = error;
    g_sd_preview.state = SD_PREVIEW_STATE_ERROR;
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
    UINT br = 0U;
    const FRESULT fr = f_read(&g_sd_preview.fp, g_sd_preview_io, request, &br);
    if ((fr != FR_OK) || (br < g_sd_preview.info.block_align))
    {
        g_sd_preview.data_remaining = 0U;
        g_sd_preview.source_exhausted = 1U;
        g_sd_preview.io_error = 1U;
        return 0U;
    }

    g_sd_preview.io_pos = 0U;
    g_sd_preview.io_len = br - (br % g_sd_preview.info.block_align);
    if (g_sd_preview.io_len == 0U)
    {
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
        if (sd_preview_refill_io_buffer() == 0U)
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
    const uint32_t start_byte = g_sd_preview.range_start_frame * g_sd_preview.info.block_align;
    if (f_lseek(&g_sd_preview.fp, g_sd_preview.info.data_offset + start_byte) != FR_OK)
    {
        return 0U;
    }
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

    if (sd_preview_decode_next_source_frame(&g_sd_preview.prev_l, &g_sd_preview.prev_r) == 0U)
    {
        return 0U;
    }
    g_sd_preview.source_prev_valid = 1U;
    g_sd_preview.prev_index = 0U;

    if (sd_preview_decode_next_source_frame(&g_sd_preview.curr_l, &g_sd_preview.curr_r) != 0U)
    {
        g_sd_preview.source_curr_valid = 1U;
        g_sd_preview.curr_index = 1U;
    }
    else
    {
        g_sd_preview.curr_l = g_sd_preview.prev_l;
        g_sd_preview.curr_r = g_sd_preview.prev_r;
        g_sd_preview.source_curr_valid = 0U;
        g_sd_preview.curr_index = 0U;
        g_sd_preview.source_exhausted = 1U;
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

        if (sd_preview_decode_next_source_frame(&next_l, &next_r) == 0U)
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
        if (sd_preview_prepare_stream() == 0U)
        {
            return 0U;
        }
    }

    if (sd_preview_ensure_source_window(target_index) == 0U)
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

        if (sd_preview_generate_one(&left, &right) == 0U)
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
    g_sd_preview.state = SD_PREVIEW_STATE_IDLE;
    g_sd_preview.last_error = SD_PREVIEW_ERROR_NONE;
    g_sd_preview.gain = 1.0f;
    g_sd_preview_control_epoch = 0U;
    g_sd_preview_reset_fence = 0U;
    g_sd_preview_reset_pending = 0U;
    g_sd_preview_reset_publish_failed = 0U;
    g_sd_preview_ring_ipc.write_count = 0U;
    g_sd_preview_ring_ipc.read_count = 0U;
    g_sd_preview_ring_ipc.epoch = 0U;
    g_sd_preview_ring_ipc.active = 0U;
    g_sd_preview_ring_ipc.gain = 1.0f;
    sd_preview_reset_source_state();
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

    g_sd_preview.gain = gain;
    union { float f; uint32_t u; } encoded = { .f = gain };
    uint64_t sample_time = 0U;
    if (live_clock_read_audio_sample(&sample_time))
        (void)control_audio_publish_param(0U, CONTROL_AUDIO_PARAM_PREVIEW_GAIN,
                                          encoded.u, 0U, sample_time);
}

float sd_preview_get_gain(void)
{
    return g_sd_preview.gain;
}

uint8_t sd_preview_begin_range(const char *path, uint32_t start_frame, uint32_t end_frame)
{
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

    if (sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    sd_preview_clear_session(0U, 1U);
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

    return 1U;
}

uint8_t sd_preview_begin(const char *path)
{
    return sd_preview_begin_range(path, 0U, 0U);
}

void sd_preview_stop(void)
{
    g_sd_preview.state = SD_PREVIEW_STATE_STOPPING;
    sd_preview_clear_session(1U, 1U);
}

void sd_preview_process(void)
{
    if (sd_preview_ring_service_reset() == 0U)
        return;
    if ((g_sd_preview.state != SD_PREVIEW_STATE_OPENING)
        && (g_sd_preview.state != SD_PREVIEW_STATE_STREAMING))
    {
        return;
    }

    if (sample_cache_has_pending_sd_work() != 0U)
    {
        sd_preview_stop();
        return;
    }

    if (g_sd_preview.stream_initialized == 0U)
    {
        if (sd_preview_prepare_stream() == 0U)
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

uint8_t sd_preview_render_main(float *out_main_l, float *out_main_r, uint32_t frames)
{
    uint32_t i;
    uint8_t mixed = 0U;

    if ((out_main_l == 0) || (out_main_r == 0) || (frames == 0U))
    {
        return 0U;
    }

    if (g_sd_preview_ring_ipc.active == 0U)
    {
        return 0U;
    }

    for (i = 0U; i < frames; ++i)
    {
        float left = 0.0f;
        float right = 0.0f;

        if (sd_preview_ring_pop(&left, &right) == 0U)
        {
            break;
        }

        const float gain = g_sd_preview_ring_ipc.gain;
        out_main_l[i] += left * gain;
        out_main_r[i] += right * gain;
        mixed = 1U;
    }

    return mixed;
}

uint8_t sd_preview_audio_apply_active(uint8_t active, uint32_t epoch)
{
    g_sd_preview_ring_ipc.epoch = epoch;
    g_sd_preview_ring_ipc.active = (uint8_t)(active != 0U);
    return 1U;
}

uint8_t sd_preview_audio_apply_gain(uint32_t gain_bits)
{
    union { uint32_t u; float f; } decoded = { .u = gain_bits };
    g_sd_preview_ring_ipc.gain = decoded.f;
    return 1U;
}
