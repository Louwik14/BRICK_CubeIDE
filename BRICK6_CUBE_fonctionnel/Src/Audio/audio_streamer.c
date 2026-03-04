#include "audio_streamer.h"

#include <stdio.h>
#include <string.h>

#include "memory_layout.h"
#include "wav_parser.h"
#include "sd_reader.h"
#include "stm32h7xx_hal.h"

#define AUDIO_STREAM_BUFFER_BYTES       (32U * 1024U)
#define AUDIO_STREAM_FRAME_BYTES        6U
#define AUDIO_STREAM_PAYLOAD_BYTES      ((AUDIO_STREAM_BUFFER_BYTES / AUDIO_STREAM_FRAME_BYTES) * AUDIO_STREAM_FRAME_BYTES)

typedef struct
{
    volatile uint8_t full;
    volatile uint8_t needs_refill;
    uint32_t valid_bytes;
    volatile uint32_t read_offset;
} audio_stream_buffer_state_t;

static DMA_BUFFER uint8_t g_pcm_buffer_a[AUDIO_STREAM_BUFFER_BYTES];
static DMA_BUFFER uint8_t g_pcm_buffer_b[AUDIO_STREAM_BUFFER_BYTES];

static audio_stream_buffer_state_t g_state_a;
static audio_stream_buffer_state_t g_state_b;

static volatile uint8_t g_running = 0U;
static volatile uint8_t g_active_buffer = 0U;

static audio_streamer_stats_t g_stats;

static float pcm24_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
    if((v & 0x00800000L) != 0)
    {
        v |= (int32_t)0xFF000000L;
    }
    return (float)v * (1.0f / 8388608.0f);
}

static bool refill_buffer(uint8_t which)
{
    uint8_t *dst = (which == 0U) ? g_pcm_buffer_a : g_pcm_buffer_b;
    audio_stream_buffer_state_t *st = (which == 0U) ? &g_state_a : &g_state_b;
    uint32_t read_bytes = 0U;
    uint32_t t0 = HAL_GetTick();
    uint32_t dt;

    if(sd_reader_read_looping(dst, AUDIO_STREAM_PAYLOAD_BYTES, &read_bytes) == false)
    {
        st->full = 0U;
        st->needs_refill = 1U;
        st->valid_bytes = 0U;
        st->read_offset = 0U;
        return false;
    }

    dt = HAL_GetTick() - t0;
    if(dt > g_stats.max_sd_read_time)
    {
        g_stats.max_sd_read_time = dt;
    }

    st->valid_bytes = read_bytes - (read_bytes % AUDIO_STREAM_FRAME_BYTES);
    st->read_offset = 0U;
    st->full = (st->valid_bytes > 0U) ? 1U : 0U;
    st->needs_refill = 0U;

    g_stats.buffer_A_full = g_state_a.full;
    g_stats.buffer_B_full = g_state_b.full;

    return (st->full != 0U);
}

bool audio_streamer_start_first_wav(void)
{
    char wav_path[64];
    FIL fp;
    wav_parser_info_t info;

    audio_streamer_stop();
    memset(&g_stats, 0, sizeof(g_stats));

    if(!wav_parser_find_first_wav(wav_path, sizeof(wav_path)))
    {
        printf("[STREAM] no WAV found\r\n");
        return false;
    }

    if(f_open(&fp, wav_path, FA_READ) != FR_OK)
    {
        printf("[STREAM] open failed: %s\r\n", wav_path);
        return false;
    }

    if(!wav_parser_read_header(&fp, &info))
    {
        (void)f_close(&fp);
        printf("[STREAM] bad WAV format (need PCM stereo 24-bit 48k)\r\n");
        return false;
    }

    (void)f_close(&fp);

    if(!sd_reader_open(wav_path, info.data_offset, info.data_size))
    {
        printf("[STREAM] sd_reader_open failed\r\n");
        return false;
    }

    memset(&g_state_a, 0, sizeof(g_state_a));
    memset(&g_state_b, 0, sizeof(g_state_b));

    if(!refill_buffer(0U))
    {
        printf("[STREAM] prefill A failed\r\n");
        sd_reader_close();
        return false;
    }

    if(!refill_buffer(1U))
    {
        printf("[STREAM] prefill B failed\r\n");
        sd_reader_close();
        return false;
    }

    g_active_buffer = 0U;
    g_running = 1U;

    printf("[STREAM] started: %s\r\n", wav_path);
    return true;
}

void audio_streamer_stop(void)
{
    g_running = 0U;
    g_active_buffer = 0U;
    memset(&g_state_a, 0, sizeof(g_state_a));
    memset(&g_state_b, 0, sizeof(g_state_b));
    sd_reader_close();
}

void audio_streamer_task(void)
{
    if(g_running == 0U)
    {
        return;
    }

    if(g_state_a.needs_refill != 0U)
    {
        (void)refill_buffer(0U);
    }

    if(g_state_b.needs_refill != 0U)
    {
        (void)refill_buffer(1U);
    }
}

void audio_streamer_get_frame(float *l, float *r)
{
    audio_stream_buffer_state_t *active;
    audio_stream_buffer_state_t *other;
    uint8_t *active_data;

    if((l == NULL) || (r == NULL) || (g_running == 0U))
    {
        if(l != NULL)
        {
            *l = 0.0f;
        }
        if(r != NULL)
        {
            *r = 0.0f;
        }
        return;
    }

    active = (g_active_buffer == 0U) ? &g_state_a : &g_state_b;
    other = (g_active_buffer == 0U) ? &g_state_b : &g_state_a;
    active_data = (g_active_buffer == 0U) ? g_pcm_buffer_a : g_pcm_buffer_b;

    if((active->full == 0U) || ((active->read_offset + AUDIO_STREAM_FRAME_BYTES) > active->valid_bytes))
    {
        active->full = 0U;
        active->needs_refill = 1U;
        active->read_offset = 0U;

        if(other->full != 0U)
        {
            g_active_buffer = (g_active_buffer == 0U) ? 1U : 0U;
            active = (g_active_buffer == 0U) ? &g_state_a : &g_state_b;
            active_data = (g_active_buffer == 0U) ? g_pcm_buffer_a : g_pcm_buffer_b;
        }
        else
        {
            g_stats.underrun_count++;
            g_stats.buffer_A_full = g_state_a.full;
            g_stats.buffer_B_full = g_state_b.full;
            *l = 0.0f;
            *r = 0.0f;
            return;
        }
    }

    *l = pcm24_to_float(&active_data[active->read_offset + 0U]);
    *r = pcm24_to_float(&active_data[active->read_offset + 3U]);
    active->read_offset += AUDIO_STREAM_FRAME_BYTES;

    if(active->read_offset >= active->valid_bytes)
    {
        active->full = 0U;
        active->needs_refill = 1U;
        active->read_offset = 0U;
    }

    g_stats.buffer_A_full = g_state_a.full;
    g_stats.buffer_B_full = g_state_b.full;
}

const audio_streamer_stats_t *audio_streamer_get_stats(void)
{
    return &g_stats;
}
