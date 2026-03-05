#include "audio_streamer.h"

#include <string.h>

#include "memory_layout.h"
#include "stm32h7xx_hal.h"
#include "wav_parser.h"

#define STREAM_RING_FRAMES (16384U)
#define STREAM_RING_SAMPLES (STREAM_RING_FRAMES * 2U)
#define REFILL_THRESHOLD_FRAMES (512U)
#define IO_CHUNK_BYTES (4096U)
#define IO_CHUNK_MAX (4096U)

static AUDIO_COLD_SDRAM float s_ring[STREAM_RING_SAMPLES];
static audio_streamer_t s_streamer;
static float s_last_l = 0.0f;
static float s_last_r = 0.0f;

static inline uint32_t ring_space(uint32_t r, uint32_t w)
{
    return (w >= r) ? (STREAM_RING_FRAMES - (w - r) - 1U) : (r - w - 1U);
}

static inline float pcm24_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
    if((v & 0x00800000) != 0)
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

static bool seek_to_data(audio_streamer_t *s)
{
    FRESULT fr = f_lseek(&s->fp, s->data_offset);
    if(fr != FR_OK)
    {
        s->error = 1U;
        return false;
    }
    s->file_data_pos = 0U;
    return true;
}

static bool read_chunk(audio_streamer_t *s, uint8_t *buf, uint32_t req_bytes, uint32_t *out_br)
{
    UINT br = 0U;
    uint32_t t0 = HAL_GetTick();
    FRESULT fr = f_read(&s->fp, buf, req_bytes, &br);
    uint32_t dt = HAL_GetTick() - t0;

    if(dt > s->sd_read_time_max)
        s->sd_read_time_max = dt;

    if(fr != FR_OK)
    {
        s->error = 1U;
        return false;
    }

    s->file_data_pos += (uint32_t)br;
    *out_br = (uint32_t)br;
    return true;
}

static uint32_t produce_frames(audio_streamer_t *s)
{
    static uint8_t io_buf[IO_CHUNK_MAX];
    const uint32_t bpf = s->bytes_per_frame;

    uint32_t rp = s->read_pos;
    uint32_t wp = s->write_pos;
    uint32_t space = ring_space(rp, wp);
    if(space <= REFILL_THRESHOLD_FRAMES)
        return 0U;

    uint32_t max_frames = IO_CHUNK_BYTES / bpf;
    if(max_frames > (space - REFILL_THRESHOLD_FRAMES))
        max_frames = space - REFILL_THRESHOLD_FRAMES;
    if(max_frames == 0U)
        return 0U;

    uint32_t file_bytes_left = s->data_size - s->file_data_pos;
    uint32_t file_frames_left = file_bytes_left / bpf;
    if(file_frames_left == 0U)
    {
        if(!seek_to_data(s))
            return 0U;
        file_frames_left = s->data_size / bpf;
    }

    if(max_frames > file_frames_left)
        max_frames = file_frames_left;

    uint32_t req = max_frames * bpf;
    uint32_t br = 0U;
    if(!read_chunk(s, io_buf, req, &br))
        return 0U;

    uint32_t got = br / bpf;
    for(uint32_t i = 0U; i < got; i++)
    {
        const uint8_t *f = &io_buf[i * bpf];
        uint32_t idx = wp * 2U;

        if(s->bits_per_sample == 24U)
        {
            s_ring[idx] = pcm24_to_float(&f[0]);
            s_ring[idx + 1U] = pcm24_to_float(&f[3]);
        }
        else
        {
            s_ring[idx] = pcm32_to_float(&f[0]);
            s_ring[idx + 1U] = pcm32_to_float(&f[4]);
        }

        wp++;
        if(wp >= STREAM_RING_FRAMES)
            wp = 0U;
    }

    __DMB();
    s->write_pos = wp;

    if((got < max_frames) && (s->error == 0U))
    {
        if(!seek_to_data(s))
            return got;

        uint32_t more = produce_frames(s);
        return got + more;
    }

    return got;
}

static bool prepare_start(audio_streamer_t *s)
{
    wav_info_t info;
    FIL meta;

    if((s->fs_mounted == 0U) && (f_mount(&s->fs, "0:", 1U) != FR_OK))
        return false;
    s->fs_mounted = 1U;

    if(f_open(&meta, s->pending_path, FA_READ) != FR_OK)
        return false;
    if(!wav_parser_parse_info(&meta, &info))
    {
        (void)f_close(&meta);
        return false;
    }

    uint32_t size = f_size(&meta);
    (void)f_close(&meta);

    if((info.channels != 2U) || (info.sample_rate != 48000U))
        return false;
    if((info.bits_per_sample != 24U) && (info.bits_per_sample != 32U))
        return false;
    if(size <= info.data_offset)
        return false;

    s->bits_per_sample = info.bits_per_sample;
    s->bytes_per_frame = 2U * (uint32_t)(info.bits_per_sample / 8U);
    s->data_offset = info.data_offset;
    s->data_size = info.data_size;

    uint32_t max_data = size - s->data_offset;
    if(s->data_size > max_data)
        s->data_size = max_data;
    s->data_size -= (s->data_size % s->bytes_per_frame);
    if(s->data_size == 0U)
        return false;

    if(f_open(&s->fp, s->pending_path, FA_READ) != FR_OK)
        return false;

    s->file_open = 1U;
    s->read_pos = 0U;
    s->write_pos = 0U;

    if(!seek_to_data(s))
        return false;

    while((ring_space(s->read_pos, s->write_pos) > REFILL_THRESHOLD_FRAMES) && (s->error == 0U))
    {
        if(produce_frames(s) == 0U)
            break;
    }

    s->running = 1U;
    s->start_pending = 0U;
    return true;
}

#endif

bool audio_streamer_start(const char *path)
{
    audio_streamer_t *s = &s_streamer;

    memset(s_ring, 0, sizeof(s_ring));
    memset(s, 0, sizeof(*s));

#if AUDIO_STREAMER_HAS_FATFS
    if(path == NULL)
        return false;

    strncpy(s->pending_path, path, sizeof(s->pending_path) - 1U);
    s->pending_path[sizeof(s->pending_path) - 1U] = '\0';
    s->start_pending = 1U;

    /*
     * Boot-time robustness:
     * prepare synchronously so playback never starts from an empty ring when
     * audio IRQ is already running.
     */
    if(!prepare_start(s))
    {
        s->error = 1U;
        s->start_pending = 0U;
        s->running = 0U;
        return false;
    }

    return true;
#else
    (void)path;
    return false;
#endif
}

void audio_streamer_process(void)
{
#if AUDIO_STREAMER_HAS_FATFS
    audio_streamer_t *s = &s_streamer;

    if((s->start_pending != 0U) && !prepare_start(s))
    {
        s->error = 1U;
        s->start_pending = 0U;
        s->running = 0U;
    }

    if((s->running == 0U) || (s->error != 0U))
        return;

    while((ring_space(s->read_pos, s->write_pos) > REFILL_THRESHOLD_FRAMES) && (s->error == 0U))
    {
        if(produce_frames(s) == 0U)
            break;
    }
#endif
}

void audio_streamer_get_frame(float *L, float *R)
{
    audio_streamer_t *s = &s_streamer;
    float out_l = s_last_l;
    float out_r = s_last_r;

    if((s->running != 0U) && (s->error == 0U))
    {
        uint32_t rp = s->read_pos;
        uint32_t wp = s->write_pos;

        if(rp != wp)
        {
            uint32_t idx = rp * 2U;
            out_l = s_ring[idx];
            out_r = s_ring[idx + 1U];
            s_last_l = out_l;
            s_last_r = out_r;

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

    if(L) *L = out_l;
    if(R) *R = out_r;
}

void audio_streamer_get_stats(audio_streamer_stats_t *out_stats)
{
    if(out_stats == NULL)
        return;

    out_stats->underrun_count = s_streamer.underrun_count;
    out_stats->sd_read_time_max = s_streamer.sd_read_time_max;
}
