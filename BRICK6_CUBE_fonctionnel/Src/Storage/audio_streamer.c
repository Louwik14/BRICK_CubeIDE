#include "audio_streamer.h"

#include <stdio.h>
#include <string.h>

#include "memory_layout.h"
#include "stm32h7xx_hal.h"
#include "wav_parser.h"

#define STREAM_BUFFER_FRAMES (4096U)
#define STREAM_BUFFER_SAMPLES (STREAM_BUFFER_FRAMES * 2U)
#define STREAM_IO_BYTES (4096U)

static AUDIO_COLD_SDRAM float stream_buffer_A[STREAM_BUFFER_SAMPLES];
static AUDIO_COLD_SDRAM float stream_buffer_B[STREAM_BUFFER_SAMPLES];

static audio_streamer_t g_streamer;

static volatile uint32_t *streamer_frames_valid_ptr(audio_streamer_t *s, uint8_t buffer_index)
{
    return (buffer_index == 0U) ? &s->frames_valid_A : &s->frames_valid_B;
}

static volatile uint8_t *streamer_ready_ptr(audio_streamer_t *s, uint8_t buffer_index)
{
    return (buffer_index == 0U) ? &s->ready_A : &s->ready_B;
}

static float *streamer_buffer_ptr(audio_streamer_t *s, uint8_t buffer_index)
{
    return (buffer_index == 0U) ? s->bufferA : s->bufferB;
}

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
static bool streamer_read_bytes(audio_streamer_t *s, uint8_t *dst, uint32_t bytes)
{
    UINT br = 0U;
    uint32_t t0 = HAL_GetTick();
    FRESULT fr = f_read(&s->fp, dst, bytes, &br);
    uint32_t dt = HAL_GetTick() - t0;

    if(dt > s->sd_read_time_max)
        s->sd_read_time_max = dt;

    if((fr != FR_OK) || (br != bytes))
    {
        printf("[STREAM] SD read fail fr=%d br=%u req=%lu\r\n", (int)fr, (unsigned)br, (unsigned long)bytes);
        s->error = 1U;
        return false;
    }

    s->file_data_pos += bytes;
    return true;
}

static bool streamer_seek_to_data_start(audio_streamer_t *s)
{
    if(f_lseek(&s->fp, s->data_offset) != FR_OK)
    {
        printf("[STREAM] seek data start failed\r\n");
        s->error = 1U;
        return false;
    }

    s->file_data_pos = 0U;
    return true;
}

static uint32_t streamer_fill_buffer(audio_streamer_t *s, uint8_t buffer_index)
{
    static uint8_t io_buf[STREAM_IO_BYTES];
    uint32_t frames_written = 0U;
    volatile uint32_t *frames_valid = streamer_frames_valid_ptr(s, buffer_index);
    volatile uint8_t *ready = streamer_ready_ptr(s, buffer_index);
    float *buffer = streamer_buffer_ptr(s, buffer_index);

    while(frames_written < STREAM_BUFFER_FRAMES)
    {
        uint32_t frames_left = STREAM_BUFFER_FRAMES - frames_written;
        uint32_t chunk_frames = (frames_left > 512U) ? 512U : frames_left;
        uint32_t chunk_bytes = chunk_frames * s->bytes_per_frame;

        if((s->data_size > s->file_data_pos) && ((s->data_size - s->file_data_pos) < chunk_bytes))
            chunk_bytes = s->data_size - s->file_data_pos;

        if(chunk_bytes == 0U)
        {
            if(!streamer_seek_to_data_start(s))
                break;
            continue;
        }

        if(!streamer_read_bytes(s, io_buf, chunk_bytes))
            break;

        chunk_frames = chunk_bytes / s->bytes_per_frame;
        for(uint32_t i = 0U; i < chunk_frames; i++)
        {
            const uint8_t *frame = &io_buf[i * s->bytes_per_frame];
            float l;
            float r;

            if(s->bits_per_sample == 24U)
            {
                l = pcm24_to_float(&frame[0]);
                r = pcm24_to_float(&frame[3]);
            }
            else
            {
                l = pcm32_to_float(&frame[0]);
                r = pcm32_to_float(&frame[4]);
            }

            buffer[(frames_written + i) * 2U + 0U] = l;
            buffer[(frames_written + i) * 2U + 1U] = r;
        }

        frames_written += chunk_frames;

        if(chunk_frames == 0U)
            break;
    }

    *frames_valid = frames_written;
    *ready = (frames_written > 0U) ? 1U : 0U;
    return frames_written;
}

static bool streamer_prepare_start(audio_streamer_t *s)
{
    wav_info_t info;
    FIL fp_meta;
    FRESULT fr;

    if(s->fs_mounted == 0U)
    {
        fr = f_mount(&s->fs, "0:", 1U);
        if(fr != FR_OK)
        {
            printf("[STREAM] f_mount failed: %d\r\n", (int)fr);
            return false;
        }
        s->fs_mounted = 1U;
    }

    fr = f_open(&fp_meta, s->pending_path, FA_READ);
    if(fr != FR_OK)
    {
        printf("[STREAM] f_open(meta) failed: %d\r\n", (int)fr);
        return false;
    }

    if(!wav_parser_parse_info(&fp_meta, &info))
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

    s->bits_per_sample = info.bits_per_sample;
    s->bytes_per_frame = 2U * (uint32_t)(s->bits_per_sample / 8U);
    s->data_offset = info.data_offset;
    s->data_size = info.data_size;

    fr = f_open(&s->fp, s->pending_path, FA_READ);
    if(fr != FR_OK)
    {
        printf("[STREAM] f_open(stream) failed: %d\r\n", (int)fr);
        return false;
    }
    s->file_open = 1U;

    if(!streamer_seek_to_data_start(s))
        return false;

    if(streamer_fill_buffer(s, 0U) == 0U)
    {
        printf("[STREAM] initial fill A failed\r\n");
        return false;
    }

    if(streamer_fill_buffer(s, 1U) == 0U)
    {
        printf("[STREAM] initial fill B failed\r\n");
        return false;
    }

    s->running = 1U;
    s->start_pending = 0U;

    printf("[STREAM] started: %s bits=%u data=%lu\r\n",
           s->pending_path,
           (unsigned)s->bits_per_sample,
           (unsigned long)s->data_size);
    return true;
}
#endif

bool audio_streamer_start(const char *path)
{
    audio_streamer_t *s = &g_streamer;

    s->bufferA = stream_buffer_A;
    s->bufferB = stream_buffer_B;

    for(uint32_t i = 0U; i < STREAM_BUFFER_SAMPLES; i++)
    {
        stream_buffer_A[i] = 0.0f;
        stream_buffer_B[i] = 0.0f;
    }

    s->active_buffer = 0U;
    s->read_pos = 0U;
    s->running = 0U;
    s->error = 0U;
    s->bits_per_sample = 0U;
    s->bytes_per_frame = 0U;
    s->data_offset = 0U;
    s->data_size = 0U;
    s->file_data_pos = 0U;
    s->underrun_count = 0U;
    s->sd_read_time_max = 0U;
    s->buffer_switch_count = 0U;
    s->frames_valid_A = 0U;
    s->ready_A = 0U;
    s->frames_valid_B = 0U;
    s->ready_B = 0U;

#if AUDIO_STREAMER_HAS_FATFS
    if(path == 0)
        return false;

    if(s->file_open != 0U)
    {
        (void)f_close(&s->fp);
        s->file_open = 0U;
    }

    if((snprintf(s->pending_path, sizeof(s->pending_path), "%s", path) <= 0) ||
       (strlen(s->pending_path) >= sizeof(s->pending_path)))
    {
        printf("[STREAM] bad path\r\n");
        return false;
    }

    s->start_pending = 1U;
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
    audio_streamer_t *s = &g_streamer;

    if(s->start_pending != 0U)
    {
        if(!streamer_prepare_start(s))
        {
            s->error = 1U;
            s->start_pending = 0U;
            s->running = 0U;
        }
    }

    if((s->running == 0U) || (s->error != 0U))
        return;

    {
        uint8_t inactive = (uint8_t)(s->active_buffer ^ 1U);
        volatile uint8_t *inactive_ready = streamer_ready_ptr(s, inactive);

        if(*inactive_ready == 0U)
            (void)streamer_fill_buffer(s, inactive);
    }

    if((HAL_GetTick() - last_log_tick) >= 1000U)
    {
        last_log_tick = HAL_GetTick();
        printf("[STREAM] sw=%lu und=%lu sdmax=%lu ms\r\n",
               (unsigned long)s->buffer_switch_count,
               (unsigned long)s->underrun_count,
               (unsigned long)s->sd_read_time_max);
    }
#endif
}

void audio_streamer_get_frame(float *L, float *R)
{
    audio_streamer_t *s = &g_streamer;
    float outL = 0.0f;
    float outR = 0.0f;

    if((s->running != 0U) && (s->error == 0U))
    {
        uint8_t curr = s->active_buffer;
        volatile uint32_t *curr_frames_valid = streamer_frames_valid_ptr(s, curr);

        if(s->read_pos >= *curr_frames_valid)
        {
            uint8_t next = (uint8_t)(curr ^ 1U);
            volatile uint8_t *next_ready = streamer_ready_ptr(s, next);
            volatile uint32_t *next_frames_valid = streamer_frames_valid_ptr(s, next);

            if((*next_ready != 0U) && (*next_frames_valid > 0U))
            {
                volatile uint8_t *curr_ready = streamer_ready_ptr(s, curr);

                *curr_ready = 0U;
                *curr_frames_valid = 0U;
                s->active_buffer = next;
                s->read_pos = 0U;
                curr = next;
                curr_frames_valid = next_frames_valid;
                s->buffer_switch_count++;
            }
            else
            {
                s->underrun_count++;
                if(L != 0)
                    *L = 0.0f;
                if(R != 0)
                    *R = 0.0f;
                return;
            }
        }

        if(s->read_pos < *curr_frames_valid)
        {
            float *buffer = streamer_buffer_ptr(s, curr);
            uint32_t idx = s->read_pos * 2U;
            outL = buffer[idx + 0U];
            outR = buffer[idx + 1U];
            s->read_pos++;
        }
    }

    if(L != 0)
        *L = outL;
    if(R != 0)
        *R = outR;
}

void audio_streamer_get_stats(audio_streamer_stats_t *out_stats)
{
    audio_streamer_t *s = &g_streamer;

    if(out_stats == 0)
        return;

    out_stats->underrun_count = s->underrun_count;
    out_stats->sd_read_time_max = s->sd_read_time_max;
    out_stats->buffer_switch_count = s->buffer_switch_count;
}
