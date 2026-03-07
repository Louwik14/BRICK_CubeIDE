#include "audio_streamer.h"

#include <stdio.h>
#include <string.h>

#include "memory_layout.h"
#include "stm32h7xx_hal.h"
#include "wav_parser.h"

#define STREAM_RING_FRAMES (16384U)
#define STREAM_RING_SAMPLES (STREAM_RING_FRAMES * 2U)

#define STREAM_REFILL_THRESHOLD_FRAMES (6144U)
#define STREAM_PREFILL_TARGET_FRAMES   (8192U)
#define STREAM_IO_FRAMES (4096U)

#define STREAM_DEBUG 1
#define STREAM_ASSERT_DEBUG 1

#if STREAM_DEBUG
#define STREAM_LOG(...) printf(__VA_ARGS__)
#else
#define STREAM_LOG(...)
#endif

volatile uint32_t stream_frames_out = 0;

static AUDIO_COLD_SDRAM float stream_rings[AUDIO_STREAMER_MAX_STREAMERS][STREAM_RING_SAMPLES];

static audio_streamer_t g_streamers[AUDIO_STREAMER_MAX_STREAMERS];

static bool streamer_id_valid(uint8_t streamer_id)
{
    return (streamer_id < AUDIO_STREAMER_MAX_STREAMERS);
}

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

static void streamer_debug_check_ring(audio_streamer_t *s)
{
#if STREAM_ASSERT_DEBUG
    if((s->read_pos >= STREAM_RING_FRAMES) ||
       (s->write_pos >= STREAM_RING_FRAMES))
    {
        s->pos_oob_count++;
        s->read_pos %= STREAM_RING_FRAMES;
        s->write_pos %= STREAM_RING_FRAMES;
    }

    const uint32_t used = streamer_ring_used_frames(s);
    const uint32_t space = streamer_ring_space_frames(s);

    if((used + space + 1U) != STREAM_RING_FRAMES)
        s->ring_incoherence_count++;
#endif

    const uint32_t used_after = streamer_ring_used_frames(s);

    if(used_after < s->ring_level_min_frames)
        s->ring_level_min_frames = used_after;

    if(used_after > s->ring_level_max_frames)
        s->ring_level_max_frames = used_after;
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
    const uint32_t t0 = HAL_GetTick();

    FRESULT fr = f_read(&s->fp, dst, bytes, &br);

    const uint32_t dt = HAL_GetTick() - t0;
    if(dt > s->sd_read_time_max_ms)
        s->sd_read_time_max_ms = dt;

    if(fr != FR_OK)
    {
        STREAM_LOG("[STREAM ERR] f_read=%d req=%lu br=%u\n",
                   fr,
                   (unsigned long)bytes,
                   br);

        s->error = 1U;
        return false;
    }

    if(((uint32_t)br) != bytes)
        s->partial_read_count++;

    s->file_data_pos += (uint32_t)br;

    if(out_bytes_read)
        *out_bytes_read = (uint32_t)br;

    return true;
}

static bool streamer_seek_to_data_start(audio_streamer_t *s)
{
    if(f_lseek(&s->fp, s->data_offset) != FR_OK)
    {
        STREAM_LOG("[STREAM ERR] seek fail\n");
        s->error = 1U;
        return false;
    }

    s->file_data_pos = 0U;
    s->file_restart_count++;

    return true;
}

static bool streamer_seek_to_start_frame(audio_streamer_t *s, uint32_t start_frame)
{
    const uint32_t frame_offset_bytes = start_frame * s->bytes_per_frame;

    if(frame_offset_bytes >= s->data_size)
        return false;

    if(f_lseek(&s->fp, s->data_offset + frame_offset_bytes) != FR_OK)
    {
        STREAM_LOG("[STREAM ERR] seek start frame fail\n");
        s->error = 1U;
        return false;
    }

    s->file_data_pos = frame_offset_bytes;
    return true;
}

static uint32_t streamer_fill_ring(uint8_t streamer_id, audio_streamer_t *s, uint32_t max_frames)
{
    static uint8_t io_buf[STREAM_IO_FRAMES * 8U];

    uint32_t frames_written = 0U;
    uint32_t bytes_per_frame = s->bytes_per_frame;
    const uint32_t refill_t0 = HAL_GetTick();
    uint32_t refill_bytes = 0U;

    while(frames_written < max_frames)
    {
        streamer_debug_check_ring(s);

        uint32_t space_frames = streamer_ring_space_frames(s);
        uint32_t frames_left = max_frames - frames_written;

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
            chunk_frames = frames_left_file;

        uint32_t read_bytes = chunk_frames * bytes_per_frame;

        uint32_t bytes_read = 0U;

        if(!streamer_read_bytes(s, io_buf, read_bytes, &bytes_read))
            return frames_written;

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

            const uint32_t idx = wp * 2U;
            stream_rings[streamer_id][idx] = l;
            stream_rings[streamer_id][idx + 1U] = r;

            wp = (wp + 1U) % STREAM_RING_FRAMES;
        }

        __DMB();
        s->write_pos = wp;

        frames_written += ready_frames;
        refill_bytes += bytes_read;

        s->total_bytes_read_from_sd += bytes_read;
        s->total_frames_filled_from_sd += ready_frames;

#if STREAM_ASSERT_DEBUG
        if(ready_frames > space_frames)
            s->ring_overflow_detect_count++;
#endif
    }

    const uint32_t refill_dt = HAL_GetTick() - refill_t0;
    s->last_refill_bytes = refill_bytes;
    s->last_refill_frames = frames_written;

    if((frames_written > 0U) || (refill_bytes > 0U))
    {
        s->total_refills++;
        s->total_refill_time_ms += refill_dt;
        if(refill_dt > s->refill_time_max_ms)
            s->refill_time_max_ms = refill_dt;
    }

    streamer_debug_check_ring(s);

    return frames_written;
}

#endif

static bool streamer_prepare_start(audio_streamer_t *s, uint8_t streamer_id)
{
#if AUDIO_STREAMER_HAS_FATFS
    wav_info_t info;
    FIL fp_meta;
    FRESULT fr;

    if(s->fs_mounted == 0U)
    {
        fr = f_mount(&s->fs, "0:", 1U);

        if(fr != FR_OK)
        {
            STREAM_LOG("[STREAM %u] mount fail %d\n", (unsigned)streamer_id, fr);
            return false;
        }

        s->fs_mounted = 1U;
    }

    fr = f_open(&fp_meta, s->pending_path, FA_READ);
    if(fr != FR_OK)
        return false;

    if(!wav_parser_parse_info(&fp_meta, &info))
    {
        f_close(&fp_meta);
        return false;
    }

    uint32_t file_size = f_size(&fp_meta);
    f_close(&fp_meta);

    s->bits_per_sample = info.bits_per_sample;
    s->bytes_per_frame = info.block_align;

    if((info.sample_rate != 48000U) || (info.channels != 2U))
        return false;

    if((info.bits_per_sample != 24U) && (info.bits_per_sample != 32U))
        return false;

    if((s->bytes_per_frame == 0U) ||
       (info.byte_rate != (info.sample_rate * s->bytes_per_frame)))
        return false;

    s->data_offset = info.data_offset;
    s->data_size = info.data_size;

    uint32_t max_data = file_size - s->data_offset;
    if(s->data_size > max_data)
        s->data_size = max_data;

    s->data_size -= (s->data_size % s->bytes_per_frame);

    fr = f_open(&s->fp, s->pending_path, FA_READ);
    if(fr != FR_OK)
        return false;

    s->file_open = 1U;

    if(!streamer_seek_to_start_frame(s, s->start_frame))
        return false;

    (void)streamer_fill_ring(streamer_id, s, STREAM_PREFILL_TARGET_FRAMES);

    s->running = 1U;
    s->start_pending = 0U;
    return true;
#else
    (void)s;
    (void)streamer_id;
    return false;
#endif
}

bool audio_streamer_start(uint8_t streamer_id, const char *path, uint32_t start_frame)
{
    if(!streamer_id_valid(streamer_id) || (path == NULL))
        return false;

    audio_streamer_t *s = &g_streamers[streamer_id];

    if(s->file_open != 0U)
    {
#if AUDIO_STREAMER_HAS_FATFS
        (void)f_close(&s->fp);
#endif
        s->file_open = 0U;
    }

    memset(stream_rings[streamer_id], 0, sizeof(stream_rings[streamer_id]));
    memset(s, 0, sizeof(*s));
    s->ring_level_min_frames = STREAM_RING_FRAMES;
    s->start_frame = start_frame;

#if AUDIO_STREAMER_HAS_FATFS
    strncpy(s->pending_path, path, sizeof(s->pending_path) - 1U);
    s->pending_path[sizeof(s->pending_path) - 1U] = '\0';
    s->start_pending = 1U;
    return true;
#else
    return false;
#endif
}

void audio_streamer_stop(uint8_t streamer_id)
{
    if(!streamer_id_valid(streamer_id))
        return;

    audio_streamer_t *s = &g_streamers[streamer_id];

#if AUDIO_STREAMER_HAS_FATFS
    if(s->file_open != 0U)
    {
        (void)f_close(&s->fp);
        s->file_open = 0U;
    }
#endif

    s->running = 0U;
    s->start_pending = 0U;
}

void audio_streamer_process(uint8_t streamer_id)
{
#if AUDIO_STREAMER_HAS_FATFS
    if(!streamer_id_valid(streamer_id))
        return;

    audio_streamer_t *s = &g_streamers[streamer_id];

    if(s->start_pending != 0U)
    {
        if(!streamer_prepare_start(s, streamer_id))
        {
            s->error = 1U;
            s->start_pending = 0U;
            s->running = 0U;
        }
    }

    if((s->running == 0U) || (s->error != 0U))
        return;

    streamer_debug_check_ring(s);

    if(streamer_ring_used_frames(s) < STREAM_REFILL_THRESHOLD_FRAMES)
        (void)streamer_fill_ring(streamer_id, s, STREAM_IO_FRAMES);
#else
    (void)streamer_id;
#endif
}

void audio_streamer_get_frame(uint8_t streamer_id, float *L, float *R)
{
    if(!streamer_id_valid(streamer_id))
    {
        if(L) *L = 0.0f;
        if(R) *R = 0.0f;
        return;
    }

    audio_streamer_t *s = &g_streamers[streamer_id];

    float outL = s->last_out_l;
    float outR = s->last_out_r;

    if((s->running != 0U) && (s->error == 0U))
    {
        const uint32_t used_before = streamer_ring_used_frames(s);

        if(used_before >= 1U)
        {
            const uint32_t idx = s->read_pos * 2U;

            outL = stream_rings[streamer_id][idx];
            outR = stream_rings[streamer_id][idx + 1U];

            s->read_pos = (s->read_pos + 1U) % STREAM_RING_FRAMES;
            s->total_frames_read_from_ring += 1U;
            stream_frames_out += 1U;
        }
        else
        {
            s->underrun_count++;
#if STREAM_ASSERT_DEBUG
            if(used_before == 0U)
                s->ring_underflow_logic_count++;
#endif
        }

        streamer_debug_check_ring(s);
    }

    s->last_out_l = outL;
    s->last_out_r = outR;

    if(L) *L = outL;
    if(R) *R = outR;
}

void audio_streamer_get_stats(uint8_t streamer_id, audio_streamer_stats_t *out_stats)
{
    if((out_stats == NULL) || !streamer_id_valid(streamer_id))
        return;

    audio_streamer_t *s = &g_streamers[streamer_id];

    out_stats->underrun_count = s->underrun_count;
    out_stats->ring_level_min_frames = s->ring_level_min_frames;
    out_stats->ring_level_max_frames = s->ring_level_max_frames;
    out_stats->ring_used_frames = streamer_ring_used_frames(s);
    out_stats->total_frames_read_from_ring = s->total_frames_read_from_ring;
    out_stats->total_frames_filled_from_sd = s->total_frames_filled_from_sd;
    out_stats->total_bytes_read_from_sd = s->total_bytes_read_from_sd;
    out_stats->sd_read_time_max_ms = s->sd_read_time_max_ms;
    out_stats->refill_time_max_ms = s->refill_time_max_ms;
    out_stats->total_refills = s->total_refills;
    out_stats->total_refill_time_ms = s->total_refill_time_ms;
    out_stats->file_restart_count = s->file_restart_count;
    out_stats->partial_read_count = s->partial_read_count;
    out_stats->ring_overflow_detect_count = s->ring_overflow_detect_count;
    out_stats->ring_underflow_logic_count = s->ring_underflow_logic_count;
    out_stats->ring_incoherence_count = s->ring_incoherence_count;
    out_stats->pos_oob_count = s->pos_oob_count;
    out_stats->last_refill_bytes = s->last_refill_bytes;
    out_stats->last_refill_frames = s->last_refill_frames;
}
