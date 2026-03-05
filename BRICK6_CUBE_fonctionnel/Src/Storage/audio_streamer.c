#include "audio_streamer.h"

#include <stdio.h>
#include <string.h>

#include "memory_layout.h"
#include "wav_loader.h"
#include "stm32h7xx_hal.h"

#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#    define AUDIO_STREAMER_HAS_FATFS 1
#  endif
#endif

#ifndef AUDIO_STREAMER_HAS_FATFS
#define AUDIO_STREAMER_HAS_FATFS 0
#endif

#define STREAM_BUFFER_FRAMES (4096U)
#define STREAM_BUFFER_SAMPLES (STREAM_BUFFER_FRAMES * 2U)
#define STREAM_IO_BYTES (4096U)

static AUDIO_COLD_SDRAM float stream_buffer_A[STREAM_BUFFER_SAMPLES];
static AUDIO_COLD_SDRAM float stream_buffer_B[STREAM_BUFFER_SAMPLES];

typedef struct
{
    float *data;
    volatile uint32_t frames_valid;
    volatile uint8_t ready;
} stream_buffer_state_t;

static stream_buffer_state_t g_buffers[2] = {
    { stream_buffer_A, 0U, 0U },
    { stream_buffer_B, 0U, 0U }
};

static volatile uint8_t g_active_buffer;
static volatile uint32_t g_read_pos;
static volatile uint8_t g_running;
static volatile uint8_t g_error;

static uint16_t g_bits_per_sample;
static uint32_t g_bytes_per_frame;
static uint32_t g_data_offset;
static uint32_t g_data_size;
static uint32_t g_file_data_pos;

static volatile uint32_t g_underrun_count;
static volatile uint32_t g_sd_read_time_max;
static volatile uint32_t g_buffer_switch_count;

#if AUDIO_STREAMER_HAS_FATFS
static FATFS g_stream_fs;
static uint8_t g_stream_fs_mounted;
static FIL g_fp;
#endif

static float pcm24_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
    if((v & 0x00800000L) != 0)
        v |= (int32_t)0xFF000000L;
    return (float)v * (1.0f / 8388608.0f);
}

static float pcm32_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] |
                          ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16) |
                          ((uint32_t)p[3] << 24));
    return (float)v * (1.0f / 2147483648.0f);
}

#if AUDIO_STREAMER_HAS_FATFS
static bool streamer_read_bytes(uint8_t *dst, uint32_t bytes)
{
    UINT br = 0U;
    uint32_t t0 = HAL_GetTick();
    FRESULT fr = f_read(&g_fp, dst, bytes, &br);
    uint32_t dt = HAL_GetTick() - t0;

    if(dt > g_sd_read_time_max)
        g_sd_read_time_max = dt;

    if((fr != FR_OK) || (br != bytes))
    {
        printf("[STREAM] SD read fail fr=%d br=%u req=%lu\r\n", (int)fr, (unsigned)br, (unsigned long)bytes);
        g_error = 1U;
        return false;
    }

    g_file_data_pos += bytes;
    return true;
}

static bool streamer_seek_to_data_start(void)
{
    if(f_lseek(&g_fp, g_data_offset) != FR_OK)
    {
        printf("[STREAM] seek data start failed\r\n");
        g_error = 1U;
        return false;
    }

    g_file_data_pos = 0U;
    return true;
}

static uint32_t streamer_fill_buffer(uint8_t buffer_index)
{
    static uint8_t io_buf[STREAM_IO_BYTES];
    uint32_t frames_written = 0U;

    while(frames_written < STREAM_BUFFER_FRAMES)
    {
        uint32_t frames_left = STREAM_BUFFER_FRAMES - frames_written;
        uint32_t chunk_frames = (frames_left > 512U) ? 512U : frames_left;
        uint32_t chunk_bytes = chunk_frames * g_bytes_per_frame;

        if((g_data_size > g_file_data_pos) && ((g_data_size - g_file_data_pos) < chunk_bytes))
            chunk_bytes = g_data_size - g_file_data_pos;

        if(chunk_bytes == 0U)
        {
            if(!streamer_seek_to_data_start())
                break;
            continue;
        }

        if(!streamer_read_bytes(io_buf, chunk_bytes))
            break;

        chunk_frames = chunk_bytes / g_bytes_per_frame;
        for(uint32_t i = 0U; i < chunk_frames; i++)
        {
            const uint8_t *frame = &io_buf[i * g_bytes_per_frame];
            float l;
            float r;

            if(g_bits_per_sample == 24U)
            {
                l = pcm24_to_float(&frame[0]);
                r = pcm24_to_float(&frame[3]);
            }
            else
            {
                l = pcm32_to_float(&frame[0]);
                r = pcm32_to_float(&frame[4]);
            }

            g_buffers[buffer_index].data[(frames_written + i) * 2U + 0U] = l;
            g_buffers[buffer_index].data[(frames_written + i) * 2U + 1U] = r;
        }

        frames_written += chunk_frames;

        if(chunk_frames == 0U)
            break;
    }

    g_buffers[buffer_index].frames_valid = frames_written;
    g_buffers[buffer_index].ready = (frames_written > 0U) ? 1U : 0U;
    return frames_written;
}
#endif

bool audio_streamer_start(const char *path)
{
    for(uint32_t i = 0U; i < STREAM_BUFFER_SAMPLES; i++)
    {
        stream_buffer_A[i] = 0.0f;
        stream_buffer_B[i] = 0.0f;
    }

    g_active_buffer = 0U;
    g_read_pos = 0U;
    g_running = 0U;
    g_error = 0U;
    g_bits_per_sample = 0U;
    g_bytes_per_frame = 0U;
    g_data_offset = 0U;
    g_data_size = 0U;
    g_file_data_pos = 0U;
    g_underrun_count = 0U;
    g_sd_read_time_max = 0U;
    g_buffer_switch_count = 0U;
    g_buffers[0].frames_valid = 0U;
    g_buffers[0].ready = 0U;
    g_buffers[1].frames_valid = 0U;
    g_buffers[1].ready = 0U;

#if AUDIO_STREAMER_HAS_FATFS
    wav_info_t info;
    FIL fp_meta;
    FRESULT fr;

    if(path == 0)
        return false;

    if(g_stream_fs_mounted == 0U)
    {
        fr = f_mount(&g_stream_fs, "0:", 1U);
        if(fr != FR_OK)
        {
            printf("[STREAM] f_mount failed: %d\r\n", (int)fr);
            return false;
        }
        g_stream_fs_mounted = 1U;
    }

    fr = f_open(&fp_meta, path, FA_READ);
    if(fr != FR_OK)
    {
        printf("[STREAM] f_open(meta) failed: %d\r\n", (int)fr);
        return false;
    }

    if(!wav_loader_parse_info(&fp_meta, &info))
    {
        (void)f_close(&fp_meta);
        printf("[STREAM] invalid WAV header\r\n");
        return false;
    }

    (void)f_close(&fp_meta);

    if((info.sample_rate != 48000U) || (info.channels != 2U))
    {
        printf("[STREAM] unsupported sr/ch\r\n");
        return false;
    }

    if(!((info.bits_per_sample == 24U) || (info.bits_per_sample == 32U)))
    {
        printf("[STREAM] unsupported bit depth\r\n");
        return false;
    }

    g_bits_per_sample = info.bits_per_sample;
    g_bytes_per_frame = 2U * (uint32_t)(g_bits_per_sample / 8U);
    g_data_offset = info.data_offset;
    g_data_size = info.data_size;

    fr = f_open(&g_fp, path, FA_READ);
    if(fr != FR_OK)
    {
        printf("[STREAM] f_open(stream) failed: %d\r\n", (int)fr);
        return false;
    }

    if(!streamer_seek_to_data_start())
        return false;

    if(streamer_fill_buffer(0U) == 0U)
    {
        printf("[STREAM] initial fill A failed\r\n");
        return false;
    }

    if(streamer_fill_buffer(1U) == 0U)
    {
        printf("[STREAM] initial fill B failed\r\n");
        return false;
    }

    g_running = 1U;
    printf("[STREAM] started: %s bits=%u data=%lu\r\n",
           path,
           (unsigned)g_bits_per_sample,
           (unsigned long)g_data_size);
    return true;
#else
    (void)path;
    printf("[STREAM] FatFs unavailable in this build\r\n");
    return false;
#endif
}

void audio_streamer_process(void)
{
#if AUDIO_STREAMER_HAS_FATFS
    static uint32_t last_log_tick = 0U;

    if((g_running == 0U) || (g_error != 0U))
        return;

    {
        uint8_t inactive = (uint8_t)(g_active_buffer ^ 1U);
        if(g_buffers[inactive].ready == 0U)
            (void)streamer_fill_buffer(inactive);
    }

    if((HAL_GetTick() - last_log_tick) >= 1000U)
    {
        last_log_tick = HAL_GetTick();
        printf("[STREAM] sw=%lu und=%lu sdmax=%lu ms\r\n",
               (unsigned long)g_buffer_switch_count,
               (unsigned long)g_underrun_count,
               (unsigned long)g_sd_read_time_max);
    }
#endif
}

void audio_streamer_get_frame(float *L, float *R)
{
    float outL = 0.0f;
    float outR = 0.0f;

    if((g_running != 0U) && (g_error == 0U))
    {
        uint8_t curr = g_active_buffer;

        if(g_read_pos >= g_buffers[curr].frames_valid)
        {
            uint8_t next = (uint8_t)(curr ^ 1U);
            if((g_buffers[next].ready != 0U) && (g_buffers[next].frames_valid > 0U))
            {
                g_buffers[curr].ready = 0U;
                g_buffers[curr].frames_valid = 0U;
                g_active_buffer = next;
                g_read_pos = 0U;
                curr = next;
                g_buffer_switch_count++;
            }
            else
            {
                g_underrun_count++;
                if(L != 0)
                    *L = 0.0f;
                if(R != 0)
                    *R = 0.0f;
                return;
            }
        }

        if(g_read_pos < g_buffers[curr].frames_valid)
        {
            uint32_t idx = g_read_pos * 2U;
            outL = g_buffers[curr].data[idx + 0U];
            outR = g_buffers[curr].data[idx + 1U];
            g_read_pos++;
        }
    }

    if(L != 0)
        *L = outL;
    if(R != 0)
        *R = outR;
}

void audio_streamer_get_stats(audio_streamer_stats_t *out_stats)
{
    if(out_stats == 0)
        return;

    out_stats->underrun_count = g_underrun_count;
    out_stats->sd_read_time_max = g_sd_read_time_max;
    out_stats->buffer_switch_count = g_buffer_switch_count;
}
