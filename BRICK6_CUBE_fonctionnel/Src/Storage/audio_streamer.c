#include "audio_streamer.h"

#include <stdio.h>
#include <string.h>

#include "memory_layout.h"
#include "stm32h7xx_hal.h"
#include "wav_parser.h"

#define STREAM_RING_FRAMES (16384U)
#define STREAM_RING_SAMPLES (STREAM_RING_FRAMES * 2U)

#define STREAM_REFILL_THRESHOLD_FRAMES (2048U)
#define STREAM_PREFILL_TARGET_FRAMES   (4096U)
#define STREAM_IO_FRAMES (512U)

#define STREAM_DEBUG 1

#if STREAM_DEBUG
#define STREAM_LOG(...) printf(__VA_ARGS__)
#else
#define STREAM_LOG(...)
#endif


static AUDIO_COLD_SDRAM float stream_ring[STREAM_RING_SAMPLES];

static audio_streamer_t g_streamer;

static float g_last_out_l = 0.0f;
static float g_last_out_r = 0.0f;


static uint32_t streamer_ring_used_frames(const audio_streamer_t *s)
{
    uint32_t r = s->read_pos;
    uint32_t w = s->write_pos;

    if(w >= r)
        return w - r;

    return STREAM_RING_FRAMES - (r - w);
}

static uint32_t streamer_ring_space_frames(const audio_streamer_t *s)
{
    uint32_t r = s->read_pos;
    uint32_t w = s->write_pos;

    if(w >= r)
        return STREAM_RING_FRAMES - (w - r) - 1U;

    return r - w - 1U;
}


static float pcm24_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] |
                          ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16));

    if(v & 0x00800000)
        v |= 0xFF000000;

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


static bool streamer_read_bytes(audio_streamer_t *s,
                                uint8_t *dst,
                                uint32_t bytes,
                                uint32_t *out_bytes_read)
{
    UINT br = 0U;

    FRESULT fr = f_read(&s->fp, dst, bytes, &br);

    if(fr != FR_OK)
    {
        STREAM_LOG("[STREAM ERR] f_read=%d req=%lu br=%u\n",
                   fr,
                   (unsigned long)bytes,
                   br);

        s->error = 1U;
        return false;
    }

    s->file_data_pos += (uint32_t)br;

    if(out_bytes_read)
        *out_bytes_read = (uint32_t)br;

    return true;
}


static bool streamer_seek_to_data_start(audio_streamer_t *s)
{
    STREAM_LOG("\n=== LOOP RESTART ===\n");
    STREAM_LOG("file_pos=%lu data_size=%lu\n",
               (unsigned long)s->file_data_pos,
               (unsigned long)s->data_size);

    if(f_lseek(&s->fp, s->data_offset) != FR_OK)
    {
        STREAM_LOG("[STREAM ERR] seek fail\n");
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

    while(frames_written < max_frames)
    {
        uint32_t space_frames = streamer_ring_space_frames(s);
        uint32_t frames_left  = max_frames - frames_written;

        uint32_t chunk_frames = STREAM_IO_FRAMES;

        if(chunk_frames > space_frames)
            chunk_frames = space_frames;

        if(chunk_frames > frames_left)
            chunk_frames = frames_left;

        if(chunk_frames == 0U)
            break;

        uint32_t bytes_left = s->data_size - s->file_data_pos;
        uint32_t frames_left_file = bytes_left / bytes_per_frame;

        if(frames_left_file == 0U)
        {
            if(!streamer_seek_to_data_start(s))
                return frames_written;

            continue;
        }

        if(chunk_frames > frames_left_file)
        {
            STREAM_LOG("[STREAM END]\n");
            STREAM_LOG("bytes_left=%lu\n", (unsigned long)bytes_left);
            STREAM_LOG("frames_left_file=%lu\n",
                       (unsigned long)frames_left_file);

            chunk_frames = frames_left_file;
        }

        uint32_t read_bytes = chunk_frames * bytes_per_frame;

        uint32_t bytes_read = 0U;

        if(!streamer_read_bytes(s, io_buf, read_bytes, &bytes_read))
            return frames_written;

        if(bytes_read != read_bytes)
        {
            STREAM_LOG("[STREAM WARN] partial read br=%lu req=%lu\n",
                       (unsigned long)bytes_read,
                       (unsigned long)read_bytes);
        }

        uint32_t ready_frames = bytes_read / bytes_per_frame;

        uint32_t wp = s->write_pos;

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

            uint32_t idx = wp * 2U;

            stream_ring[idx]     = l;
            stream_ring[idx + 1] = r;

            wp = (wp + 1U) % STREAM_RING_FRAMES;
        }

        __DMB();
        s->write_pos = wp;

        frames_written += ready_frames;
    }

    return frames_written;
}

#endif



void audio_streamer_get_frame(float *L, float *R)
{
    audio_streamer_t *s = &g_streamer;

    float outL = g_last_out_l;
    float outR = g_last_out_r;

    if((s->running != 0U) && (s->error == 0U))
    {
        if(s->read_pos != s->write_pos)
        {
            uint32_t idx = s->read_pos * 2U;

            outL = stream_ring[idx];
            outR = stream_ring[idx + 1U];

            g_last_out_l = outL;
            g_last_out_r = outR;

            s->read_pos = (s->read_pos + 1U) % STREAM_RING_FRAMES;
        }
        else
        {
            s->underrun_count++;

            STREAM_LOG("[UNDERRUN] r=%lu w=%lu\n",
                       (unsigned long)s->read_pos,
                       (unsigned long)s->write_pos);
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
            STREAM_LOG("[STREAM] mount fail %d\n", fr);
            return false;
        }

        s->fs_mounted = 1U;
    }

    fr = f_open(&fp_meta, s->pending_path, FA_READ);

    if(fr != FR_OK)
    {
        STREAM_LOG("[STREAM] open meta fail %d\n", fr);
        return false;
    }

    if(!wav_parser_parse_info(&fp_meta, &info))
    {
        f_close(&fp_meta);
        STREAM_LOG("[STREAM] invalid wav\n");
        return false;
    }

    uint32_t file_size = f_size(&fp_meta);
    f_close(&fp_meta);

    s->bits_per_sample = info.bits_per_sample;
    s->bytes_per_frame = 2U * (info.bits_per_sample / 8U);

    s->data_offset = info.data_offset;
    s->data_size   = info.data_size;

    uint32_t max_data = file_size - s->data_offset;

    if(s->data_size > max_data)
        s->data_size = max_data;

    s->data_size -= (s->data_size % s->bytes_per_frame);
    uint32_t total_frames = s->data_size / s->bytes_per_frame;
    total_frames -= (total_frames % 64);
    s->data_size = total_frames * s->bytes_per_frame;

    fr = f_open(&s->fp, s->pending_path, FA_READ);

    if(fr != FR_OK)
    {
        STREAM_LOG("[STREAM] open stream fail %d\n", fr);
        return false;
    }

    s->file_open = 1U;

    if(!streamer_seek_to_data_start(s))
        return false;

    streamer_fill_ring(s, STREAM_PREFILL_TARGET_FRAMES);

    s->running = 1U;
    s->start_pending = 0U;

    STREAM_LOG("[STREAM] started %s\n", s->pending_path);

    return true;
}



bool audio_streamer_start(const char *path)
{
    audio_streamer_t *s = &g_streamer;

    memset(stream_ring, 0, sizeof(stream_ring));
    memset(s, 0, sizeof(*s));

#if AUDIO_STREAMER_HAS_FATFS

    if(path == NULL)
        return false;

    strncpy(s->pending_path, path, sizeof(s->pending_path) - 1);

    s->start_pending = 1U;

    return true;

#else

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

    if(streamer_ring_used_frames(s) < STREAM_REFILL_THRESHOLD_FRAMES)
    {
        streamer_fill_ring(s, STREAM_IO_FRAMES);
    }

    if((HAL_GetTick() - last_log_tick) >= 1000U)
    {
        last_log_tick = HAL_GetTick();

        STREAM_LOG("[STREAM STATE]\n");
        STREAM_LOG("ring=%lu/%u\n",
                   streamer_ring_used_frames(s),
                   STREAM_RING_FRAMES);

        STREAM_LOG("file_pos=%lu\n",
                   (unsigned long)s->file_data_pos);

        STREAM_LOG("underruns=%lu\n\n",
                   (unsigned long)s->underrun_count);
    }

#endif
}



void audio_streamer_get_stats(audio_streamer_stats_t *out_stats)
{
    if(out_stats == NULL)
        return;

    out_stats->underrun_count = g_streamer.underrun_count;
    out_stats->sd_read_time_max = g_streamer.sd_read_time_max;
}
