#include "audio_streamer.h"

#include <stdio.h>
#include <string.h>

#include "memory_layout.h"
#include "stm32h7xx_hal.h"
#include "wav_parser.h"

#define STREAM_RING_FRAMES (16384U)
#define STREAM_RING_SAMPLES (STREAM_RING_FRAMES * 2U)
#define STREAM_REFILL_THRESHOLD_FRAMES (512U)
#define STREAM_PREFILL_TARGET_FRAMES (4096U)
#define STREAM_IO_FRAMES (512U)

#define STREAM_DEBUG 1

#if STREAM_DEBUG
#define STREAM_LOG(...) printf(__VA_ARGS__)
#else
#define STREAM_LOG(...)
#endif

static AUDIO_COLD_SDRAM float stream_ring[STREAM_RING_SAMPLES];

static audio_streamer_t g_streamer;

static uint32_t streamer_ring_used_frames(const audio_streamer_t *s)
{
uint32_t read_pos = s->read_pos;
uint32_t write_pos = s->write_pos;


if(write_pos >= read_pos)
    return write_pos - read_pos;

return STREAM_RING_FRAMES - (read_pos - write_pos);


}

static uint32_t streamer_ring_space_frames(const audio_streamer_t *s)
{
uint32_t read_pos = s->read_pos;
uint32_t write_pos = s->write_pos;


if(write_pos >= read_pos)
    return STREAM_RING_FRAMES - (write_pos - read_pos) - 1U;

return read_pos - write_pos - 1U;


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

static bool streamer_read_bytes(audio_streamer_t *s, uint8_t *dst, uint32_t bytes, uint32_t *out_bytes_read)
{
UINT br = 0U;


uint32_t t0 = HAL_GetTick();
FRESULT fr = f_read(&s->fp, dst, bytes, &br);
uint32_t dt = HAL_GetTick() - t0;

if(dt > s->sd_read_time_max)
    s->sd_read_time_max = dt;

if(fr != FR_OK)
{
    STREAM_LOG("[STREAM] SD read error fr=%d req=%lu br=%u\r\n",
               (int)fr, (unsigned long)bytes, (unsigned)br);
    s->error = 1U;
    return false;
}

s->file_data_pos += (uint32_t)br;

#if STREAM_DEBUG
if(br != bytes)
{
    STREAM_LOG("[STREAM] partial read req=%lu br=%u pos=%lu\r\n",
               (unsigned long)bytes,
               (unsigned)br,
               (unsigned long)s->file_data_pos);
}
#endif

if(out_bytes_read != 0)
    *out_bytes_read = (uint32_t)br;

return true;


}

static bool streamer_seek_to_data_start(audio_streamer_t *s)
{
STREAM_LOG("[STREAM] loop restart\r\n");


if(f_lseek(&s->fp, s->data_offset) != FR_OK)
{
    STREAM_LOG("[STREAM] seek data start failed\r\n");
    s->error = 1U;
    return false;
}

s->file_data_pos = 0U;

return true;


}

static uint32_t streamer_fill_ring(audio_streamer_t *s, uint32_t max_frames)
{
static uint8_t io_buf[STREAM_IO_FRAMES * 8U];

uint32_t frames_written = 0U;
uint32_t bytes_per_frame = s->bytes_per_frame;
uint32_t io_bytes = STREAM_IO_FRAMES * bytes_per_frame;

while(frames_written < max_frames)
{
    uint32_t space_frames = streamer_ring_space_frames(s);
    uint32_t frames_left = max_frames - frames_written;

    if((space_frames < STREAM_IO_FRAMES) || (frames_left < STREAM_IO_FRAMES))
        break;

    if((s->data_size != 0U) && (s->file_data_pos >= s->data_size))
    {
        STREAM_LOG(
        "[STREAM LOOP] pos=%lu size=%lu left=%lu\n",
        (unsigned long)s->file_data_pos,
        (unsigned long)s->data_size,
        (unsigned long)(s->data_size - s->file_data_pos)
        );
        STREAM_LOG("[STREAM LOOP] bytes_per_frame=%lu\n", (unsigned long)bytes_per_frame);

        if(!streamer_seek_to_data_start(s))
            return frames_written;
        continue;
    }

    uint32_t bytes_left = s->data_size - s->file_data_pos;
    uint32_t read_bytes = io_bytes;

    if(read_bytes > bytes_left)
        read_bytes = bytes_left;

    read_bytes -= (read_bytes % bytes_per_frame);

    if(read_bytes == 0U)
    {
        STREAM_LOG(
        "[STREAM LOOP] pos=%lu size=%lu left=%lu\n",
        (unsigned long)s->file_data_pos,
        (unsigned long)s->data_size,
        (unsigned long)(s->data_size - s->file_data_pos)
        );
        STREAM_LOG("[STREAM LOOP] bytes_per_frame=%lu read_bytes=%lu\n",
                   (unsigned long)bytes_per_frame,
                   (unsigned long)read_bytes);

        if(!streamer_seek_to_data_start(s))
            return frames_written;
        continue;
    }

    uint32_t bytes_read = 0U;
    if(!streamer_read_bytes(s, io_buf, read_bytes, &bytes_read))
        return frames_written;

    STREAM_LOG(
    "[STREAM READ] req=%lu br=%lu pos=%lu\n",
    (unsigned long)read_bytes,
    (unsigned long)bytes_read,
    (unsigned long)s->file_data_pos
    );
    STREAM_LOG("[STREAM READ] left=%lu bpf=%lu\n",
               (unsigned long)(s->data_size - s->file_data_pos),
               (unsigned long)bytes_per_frame);

    if((bytes_read == 0U) || ((bytes_read % bytes_per_frame) != 0U))
    {
        STREAM_LOG("[STREAM] misaligned read br=%lu frame=%lu\r\n",
                   (unsigned long)bytes_read,
                   (unsigned long)bytes_per_frame);
        s->error = 1U;
        return frames_written;
    }

    uint32_t ready_frames = bytes_read / bytes_per_frame;

    for(uint32_t i = 0U; i < ready_frames; i++)
    {
        const uint8_t *frame = &io_buf[i * bytes_per_frame];
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

        uint32_t idx = s->write_pos * 2U;
        stream_ring[idx] = l;
        stream_ring[idx + 1U] = r;
        s->write_pos = (s->write_pos + 1U) % STREAM_RING_FRAMES;
    }

    STREAM_LOG(
    "[STREAM WRITE] frames=%lu write_pos=%lu\n",
    (unsigned long)ready_frames,
    (unsigned long)s->write_pos
    );

    frames_written += ready_frames;
}

return frames_written;


}
#endif

void audio_streamer_get_frame(float *L, float *R)
{
audio_streamer_t *s = &g_streamer;


float outL = 0.0f;
float outR = 0.0f;

if((s->running != 0U) && (s->error == 0U))
{
    if(s->read_pos != s->write_pos)
    {
        uint32_t idx = s->read_pos * 2U;

        outL = stream_ring[idx];
        outR = stream_ring[idx + 1U];

        s->read_pos = (s->read_pos + 1U) % STREAM_RING_FRAMES;
    }
    else
    {
        s->underrun_count++;


#if STREAM_DEBUG
STREAM_LOG("[STREAM] underrun detected read=%lu write=%lu\r\n",
(unsigned long)s->read_pos,
(unsigned long)s->write_pos);
#endif
}
}


if(L) *L = outL;
if(R) *R = outR;


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

    uint32_t file_size = f_size(&fp_meta);

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

    if(s->data_offset >= file_size)
    {
        printf("[STREAM] invalid data offset\r\n");
        return false;
    }

    uint32_t max_data_from_file = file_size - s->data_offset;

    if(s->data_size > max_data_from_file)
        s->data_size = max_data_from_file;

    s->data_size -= (s->data_size % s->bytes_per_frame);

    if(s->data_size == 0U)
    {
        printf("[STREAM] no complete audio frame\r\n");
        return false;
    }

    fr = f_open(&s->fp, s->pending_path, FA_READ);
    if(fr != FR_OK)
    {
        printf("[STREAM] f_open(stream) failed: %d\r\n", (int)fr);
        return false;
    }

    s->file_open = 1U;

    if(!streamer_seek_to_data_start(s))
        return false;

    if(streamer_fill_ring(s, STREAM_PREFILL_TARGET_FRAMES) == 0U)
    {
        printf("[STREAM] initial ring prefill failed\r\n");
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

bool audio_streamer_start(const char *path)
{
    audio_streamer_t *s = &g_streamer;

    for(uint32_t i = 0U; i < STREAM_RING_SAMPLES; i++)
        stream_ring[i] = 0.0f;

    s->read_pos = 0U;
    s->write_pos = 0U;
    s->running = 0U;
    s->error = 0U;
    s->bits_per_sample = 0U;
    s->bytes_per_frame = 0U;
    s->data_offset = 0U;
    s->data_size = 0U;
    s->file_data_pos = 0U;
    s->underrun_count = 0U;
    s->sd_read_time_max = 0U;

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

    if(streamer_ring_space_frames(s) > STREAM_REFILL_THRESHOLD_FRAMES)
        (void)streamer_fill_ring(s, STREAM_IO_FRAMES);

    if((HAL_GetTick() - last_log_tick) >= 1000U)
    {
        last_log_tick = HAL_GetTick();

        printf("[STREAM] fill=%lu/%u und=%lu sdmax=%lu ms\r\n",
               (unsigned long)streamer_ring_used_frames(s),
               STREAM_RING_FRAMES,
               (unsigned long)s->underrun_count,
               (unsigned long)s->sd_read_time_max);
    }

#endif
}

void audio_streamer_get_stats(audio_streamer_stats_t *out_stats)
{
    audio_streamer_t *s = &g_streamer;

    if(out_stats == 0)
        return;

    out_stats->underrun_count = s->underrun_count;
    out_stats->sd_read_time_max = s->sd_read_time_max;
}
