#include "audio_streamer.h"

#include <string.h>

#include "memory_layout.h"
#include "stm32h7xx_hal.h"
#include "wav_parser.h"

/*
 * Streaming architecture (single streamer instance, scalable pattern):
 *
 *   IRQ consumer (hard real-time):
 *     audio_streamer_get_frame()
 *        - reads exactly one frame from an SPSC ring
 *        - never blocks
 *
 *   Producer (non-IRQ context):
 *     audio_streamer_process()
 *        - runs refill work with bounded budget
 *        - performs blocking FatFs reads/seeks
 *        - writes decoded frames into the ring
 *        - publishes write_pos after each produced chunk so IRQ can consume
 *          new data immediately (not only at end of long refill calls)
 *
 * EOF loop handling:
 *   - producer wraps in-place (seek to data start, continue write)
 *   - tail and head are produced in the same refill pass whenever budget allows
 *   - visible publication per chunk prevents artificial starvation around wrap
 *
 * This SPSC contract is the building block for future multi-voice:
 * each voice owns a private ring + file state; a higher-level stream scheduler
 * services voices in round-robin / deadline order.
 */

#define STREAM_RING_FRAMES   (16384U)
#define STREAM_RING_SAMPLES  (STREAM_RING_FRAMES * 2U)

/* Keep refill comfortably ahead of worst observed SD/FAT latency. */
#define STREAM_TARGET_FILL_FRAMES    (14336U) /* ~299 ms at 48 kHz */

/* Work budget per process() call. */
#define STREAM_PROCESS_BUDGET_FRAMES (4096U)

/* SD read granularity */
#define STREAM_IO_FRAMES             (512U)
#define STREAM_IO_BYTES_MAX          (STREAM_IO_FRAMES * 8U) /* stereo 32-bit */

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
    uint32_t t0 = HAL_GetTick();
    FRESULT fr = f_lseek(&s->fp, s->data_offset);
    uint32_t dt = HAL_GetTick() - t0;

    if(dt > s->sd_read_time_max)
        s->sd_read_time_max = dt;

    if(fr != FR_OK)
    {
        s->error = 1U;
        return false;
    }

    s->file_data_pos = 0U;
    return true;
}

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
 * Produce and publish up to want_frames into the ring.
 *
 * Publication policy:
 * - write_pos is published after each decoded chunk (not only once at function exit)
 *   so IRQ can consume newly produced audio immediately.
 */
static uint32_t write_frames_from_file(audio_streamer_t *s, uint32_t want_frames)
{
    static uint8_t io_buf[STREAM_IO_BYTES_MAX];

    const uint32_t bpf = s->bytes_per_frame;
    uint32_t written = 0U;
    uint32_t wp = s->write_pos;

    while((written < want_frames) && (s->error == 0U))
    {
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
            break;

        uint32_t req_bytes = chunk * bpf;
        uint32_t br = 0U;

        if(!read_bytes(s, io_buf, req_bytes, &br))
            break;

        uint32_t got_frames = (bpf != 0U) ? (br / bpf) : 0U;

        if(got_frames == 0U)
        {
            if(!seek_to_data_start(s))
                break;
            continue;
        }

        for(uint32_t i = 0U; i < got_frames; i++)
        {
            const uint8_t *frame = &io_buf[i * bpf];
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
            stream_ring[idx + 0U] = l;
            stream_ring[idx + 1U] = r;

            wp++;
            if(wp >= STREAM_RING_FRAMES)
                wp = 0U;
        }

        /* Publish this decoded chunk immediately for IRQ visibility. */
        __DMB();
        s->write_pos = wp;

        written += got_frames;

        if(got_frames < chunk)
        {
            if(!seek_to_data_start(s))
                break;
        }
    }

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

    s->read_pos = 0U;
    s->write_pos = 0U;

    if(!seek_to_data_start(s))
        return false;

    /* Prime ring before RUN state. */
    {
        uint32_t primed = 0U;
        while(primed < STREAM_TARGET_FILL_FRAMES)
        {
            uint32_t rp = s->read_pos;
            uint32_t wp = s->write_pos;
            uint32_t space = ring_space_frames(rp, wp);
            uint32_t need = STREAM_TARGET_FILL_FRAMES - primed;
            uint32_t to_write = (need > STREAM_IO_FRAMES) ? STREAM_IO_FRAMES : need;

            if(to_write > space)
                to_write = space;
            if(to_write == 0U)
                break;

            uint32_t w = write_frames_from_file(s, to_write);
            if(w == 0U)
                break;

            primed += w;
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

    /*
     * Refill policy:
     * - Refill toward target occupancy using bounded SD work chunks.
     * - Work remains budget-bounded so main loop is still responsive.
     */
    uint32_t rp = s->read_pos;
    uint32_t wp = s->write_pos;
    uint32_t used = ring_used_frames(rp, wp);

    if(used >= STREAM_TARGET_FILL_FRAMES)
        return;

    uint32_t desired = STREAM_TARGET_FILL_FRAMES;
    uint32_t budget = STREAM_PROCESS_BUDGET_FRAMES;

    while((budget > 0U) && (s->error == 0U))
    {
        rp = s->read_pos;
        wp = s->write_pos;
        used = ring_used_frames(rp, wp);

        if(used >= desired)
            break;

        uint32_t need = desired - used;
        uint32_t space = ring_space_frames(rp, wp);
        uint32_t to_write = need;

        if(to_write > STREAM_IO_FRAMES)
            to_write = STREAM_IO_FRAMES;
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
