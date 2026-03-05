#include "audio_streamer.h"

#include <string.h>

#include "memory_layout.h"
#include "stm32h7xx_hal.h"
#include "wav_parser.h"

/*
 * New approach: "high-watermark ring"
 *
 * - Keep the ring buffer almost full at all times.
 * - Never "wait until low" to refill.
 * - When reaching EOF, immediately wrap (seek) and continue reading in the same refill loop
 *   so we always produce frames and the ring never drains around the loop point.
 *
 * This is the simplest structure that scales later to multi-voice:
 * a stream_manager can call N instances' process() and each one tries to stay near-full.
 */

#define STREAM_RING_FRAMES   (16384U)
#define STREAM_RING_SAMPLES  (STREAM_RING_FRAMES * 2U)

/* Keep this many frames of free space reserved (producer never tries to fill 100%) */
#define STREAM_SLACK_FRAMES          (2048U)
/* Target occupancy (frames) we try to maintain */
#define STREAM_TARGET_FILL_FRAMES    (STREAM_RING_FRAMES - STREAM_SLACK_FRAMES)

/* Max frames we attempt to write per process() call (bounded work) */
#define STREAM_PROCESS_BUDGET_FRAMES (4096U)

/* Read granularity (frames). Must fit in io buffer and be friendly for SD */
#define STREAM_IO_FRAMES             (512U)
#define STREAM_IO_BYTES_MAX          (STREAM_IO_FRAMES * 8U) /* stereo 32-bit = 8 bytes/frame */

static AUDIO_COLD_SDRAM float stream_ring[STREAM_RING_SAMPLES];
static audio_streamer_t g_streamer;

static float g_last_out_l = 0.0f;
static float g_last_out_r = 0.0f;

static inline uint32_t ring_used_frames(uint32_t r, uint32_t w)
{
    if(w >= r)
        return w - r;
    return STREAM_RING_FRAMES - (r - w);
}

static inline uint32_t ring_space_frames(uint32_t r, uint32_t w)
{
    if(w >= r)
        return STREAM_RING_FRAMES - (w - r) - 1U;
    return r - w - 1U;
}

static inline float pcm24_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] |
                          ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16));
    if(v & 0x00800000)
        v |= (int32_t)0xFF000000;
    return (float)v * (1.0f / 8388608.0f);
}

static inline float pcm32_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] |
                          ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16) |
                          ((uint32_t)p[3] << 24));
    return (float)v * (1.0f / 2147483648.0f);
}

#if AUDIO_STREAMER_HAS_FATFS

static bool seek_to_data_start(audio_streamer_t *s)
{
    if(f_lseek(&s->fp, s->data_offset) != FR_OK)
    {
        s->error = 1U;
        return false;
    }
    s->file_data_pos = 0U;
    return true;
}

/* Read up to `bytes` from file; br may be < bytes at EOF (FR_OK). */
static bool read_bytes(audio_streamer_t *s, uint8_t *dst, uint32_t bytes, uint32_t *out_br)
{
    UINT br = 0U;
    uint32_t t0 = HAL_GetTick();
    FRESULT fr = f_read(&s->fp, dst, bytes, &br);
    uint32_t dt = HAL_GetTick() - t0;

    if(dt > s->sd_read_time_max)
        s->sd_read_time_max = dt;

    if(fr != FR_OK)
    {
        s->error = 1U;
        return false;
    }

    s->file_data_pos += (uint32_t)br;

    if(out_br)
        *out_br = (uint32_t)br;

    return true;
}

/*
 * Decode and write up to `want_frames` frames into the ring.
 * Returns the number of frames actually written.
 *
 * This function is "loop-tight": it will wrap and continue as needed without
 * returning early just because EOF was hit mid-chunk.
 */
static uint32_t write_frames_from_file(audio_streamer_t *s, uint32_t want_frames)
{
    static uint8_t io_buf[STREAM_IO_BYTES_MAX];

    const uint32_t bpf = s->bytes_per_frame;
    uint32_t written = 0U;

    uint32_t wp = s->write_pos;

    while(written < want_frames)
    {
        /* How many frames remain in file data chunk? */
        uint32_t bytes_left = s->data_size - s->file_data_pos;
        uint32_t file_frames_left = (bpf != 0U) ? (bytes_left / bpf) : 0U;

        if(file_frames_left == 0U)
        {
            if(!seek_to_data_start(s))
                break;
            continue;
        }

        uint32_t chunk = want_frames - written;
        if(chunk > STREAM_IO_FRAMES)
            chunk = STREAM_IO_FRAMES;
        if(chunk > file_frames_left)
            chunk = file_frames_left;

        if(chunk == 0U)
        {
            /* Should not happen, but avoid infinite loops. */
            if(!seek_to_data_start(s))
                break;
            continue;
        }

        uint32_t req_bytes = chunk * bpf;
        uint32_t br = 0U;

        if(!read_bytes(s, io_buf, req_bytes, &br))
            break;

        /* Convert only whole frames. */
        uint32_t got_frames = (bpf != 0U) ? (br / bpf) : 0U;

        if(got_frames == 0U)
        {
            /* Treat as EOF or short-read; try wrapping once. */
            if(!seek_to_data_start(s))
                break;
            continue;
        }

        for(uint32_t i = 0U; i < got_frames; i++)
        {
            const uint8_t *frame = &io_buf[i * bpf];

            float l, r;
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
            stream_ring[idx + 0U] = l;
            stream_ring[idx + 1U] = r;

            wp++;
            if(wp >= STREAM_RING_FRAMES)
                wp = 0U;
        }

        written += got_frames;

        /* If we got fewer than requested, wrap and keep going (gapless). */
        if(got_frames < chunk)
        {
            if(!seek_to_data_start(s))
                break;
        }
    }

    /* Publish write pointer after data writes are visible */
    __DMB();
    s->write_pos = wp;

    return written;
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
            return false;
        s->fs_mounted = 1U;
    }

    fr = f_open(&fp_meta, s->pending_path, FA_READ);
    if(fr != FR_OK)
        return false;

    if(!wav_parser_parse_info(&fp_meta, &info))
    {
        (void)f_close(&fp_meta);
        return false;
    }

    uint32_t file_size = f_size(&fp_meta);
    (void)f_close(&fp_meta);

    s->bits_per_sample = info.bits_per_sample;
    s->bytes_per_frame = 2U * (uint32_t)(info.bits_per_sample / 8U);
    s->data_offset = info.data_offset;
    s->data_size = info.data_size;

    if(file_size <= s->data_offset)
        return false;

    /* Clamp data chunk to file size and align to whole frames */
    {
        uint32_t max_data = file_size - s->data_offset;
        if(s->data_size > max_data)
            s->data_size = max_data;

        if(s->bytes_per_frame != 0U)
            s->data_size -= (s->data_size % s->bytes_per_frame);
    }

    fr = f_open(&s->fp, s->pending_path, FA_READ);
    if(fr != FR_OK)
        return false;

    s->file_open = 1U;

    if(!seek_to_data_start(s))
        return false;

    /* Prefill ring up to target fill before RUN */
    {
        /* Ensure write_pos starts at 0 and fill ring */
        s->read_pos = 0U;
        s->write_pos = 0U;

        uint32_t target = STREAM_TARGET_FILL_FRAMES;
        uint32_t wrote = 0U;

        while(wrote < target)
        {
            uint32_t space = ring_space_frames(s->read_pos, s->write_pos);
            uint32_t need = target - wrote;
            uint32_t to_write = (need > STREAM_IO_FRAMES) ? STREAM_IO_FRAMES : need;
            if(to_write > space)
                to_write = space;
            if(to_write == 0U)
                break;

            uint32_t w = write_frames_from_file(s, to_write);
            if(w == 0U)
                break;
            wrote += w;
        }
    }

    s->running = 1U;
    s->start_pending = 0U;
    return true;
}

#endif /* AUDIO_STREAMER_HAS_FATFS */

bool audio_streamer_start(const char *path)
{
    audio_streamer_t *s = &g_streamer;

    memset(stream_ring, 0, sizeof(stream_ring));
    memset(s, 0, sizeof(*s));

#if AUDIO_STREAMER_HAS_FATFS
    if(path == NULL)
        return false;

    strncpy(s->pending_path, path, sizeof(s->pending_path) - 1U);
    s->pending_path[sizeof(s->pending_path) - 1U] = '\0';

    s->start_pending = 1U;
    return true;
#else
    (void)path;
    return false;
#endif
}

void audio_streamer_process(void)
{
#if AUDIO_STREAMER_HAS_FATFS
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

    /* Bounded refill: keep ring near target fill */
    uint32_t budget = STREAM_PROCESS_BUDGET_FRAMES;

    while(budget > 0U)
    {
        /* Snapshot positions for a consistent decision */
        uint32_t rp = s->read_pos;
        uint32_t wp = s->write_pos;

        uint32_t used = ring_used_frames(rp, wp);
        if(used >= STREAM_TARGET_FILL_FRAMES)
            break;

        uint32_t space = ring_space_frames(rp, wp);
        if(space == 0U)
            break;

        uint32_t need = STREAM_TARGET_FILL_FRAMES - used;
        uint32_t to_write = (need > STREAM_IO_FRAMES) ? STREAM_IO_FRAMES : need;
        if(to_write > space)
            to_write = space;
        if(to_write > budget)
            to_write = budget;

        if(to_write == 0U)
            break;

        uint32_t wrote = write_frames_from_file(s, to_write);
        if(wrote == 0U)
            break;

        if(wrote >= budget)
            budget = 0U;
        else
            budget -= wrote;
    }

#endif
}

void audio_streamer_get_frame(float *L, float *R)
{
    audio_streamer_t *s = &g_streamer;

    float outL = g_last_out_l;
    float outR = g_last_out_r;

    if((s->running != 0U) && (s->error == 0U))
    {
        /* Snapshot write pointer for consistent empty check */
        uint32_t rp = s->read_pos;
        uint32_t wp = s->write_pos;

        if(rp != wp)
        {
            uint32_t idx = rp * 2U;
            outL = stream_ring[idx + 0U];
            outR = stream_ring[idx + 1U];

            g_last_out_l = outL;
            g_last_out_r = outR;

            rp++;
            if(rp >= STREAM_RING_FRAMES)
                rp = 0U;

            s->read_pos = rp;
        }
        else
        {
            s->underrun_count++;
        }
    }

    if(L) *L = outL;
    if(R) *R = outR;
}

void audio_streamer_get_stats(audio_streamer_stats_t *out_stats)
{
    if(out_stats == NULL)
        return;

    out_stats->underrun_count = g_streamer.underrun_count;
    out_stats->sd_read_time_max = g_streamer.sd_read_time_max;
}
