#include "sampler_stream.h"

#include <stdio.h>

#include "memory_layout.h"
#include "sd_audio_block_ring.h"

#define DBG(...) printf(__VA_ARGS__)

AUDIO_COLD_SDRAM float stream_buffer[STREAM_BUFFER_FRAMES * 2U];
volatile uint32_t g_stream_write_pos = 0U;
volatile uint32_t g_stream_read_pos = 0U;
volatile uint32_t g_stream_underrun_count = 0U;

typedef struct
{
    const uint8_t *current_block;
    uint32_t read_index;
    uint8_t pending_frame[6];
    uint32_t pending_size;
} stream_reader_t;

static stream_reader_t g_stream;

static float pcm24_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
    if((v & 0x00800000L) != 0)
        v |= (int32_t)0xFF000000L;
    return (float)v * (1.0f / 8388608.0f);
}

static uint32_t stream_next_index(uint32_t pos)
{
    pos += 1U;
    if(pos >= (STREAM_BUFFER_FRAMES * 2U))
        pos = 0U;
    return pos;
}

void sampler_stream_init(void)
{
    for(uint32_t i = 0U; i < (STREAM_BUFFER_FRAMES * 2U); i++)
        stream_buffer[i] = 0.0f;

    g_stream.current_block = 0;
    g_stream.read_index = 0U;
    g_stream.pending_size = 0U;
    g_stream_write_pos = 0U;
    g_stream_read_pos = 0U;
    g_stream_underrun_count = 0U;
}

uint32_t sampler_stream_fill_samples(void)
{
    uint32_t wp = g_stream_write_pos;
    uint32_t rp = g_stream_read_pos;

    if(wp >= rp)
        return wp - rp;

    return (STREAM_BUFFER_FRAMES * 2U) - (rp - wp);
}

void sampler_stream_update(void)
{
    static uint32_t last_fill = 0U;
    uint8_t *block;

    if(g_stream.current_block == 0)
    {
        block = audio_block_ring_get_read_ptr(&sd_audio_block_ring);
        if(block == 0)
            return;

        g_stream.current_block = block;
        g_stream.read_index = 0U;
    }

    while(g_stream.current_block != 0)
    {
        uint32_t wp = g_stream_write_pos;
        uint32_t next_wp = stream_next_index(stream_next_index(wp));

        if(next_wp == g_stream_read_pos)
            return;

        while(g_stream.read_index < AUDIO_BLOCK_SIZE)
        {
            wp = g_stream_write_pos;
            next_wp = stream_next_index(stream_next_index(wp));

            if(next_wp == g_stream_read_pos)
                return;

            g_stream.pending_frame[g_stream.pending_size++] =
                g_stream.current_block[g_stream.read_index++];

            if(g_stream.pending_size < 6U)
                continue;

            float l = pcm24_to_float(&g_stream.pending_frame[0]);
            float r = pcm24_to_float(&g_stream.pending_frame[3]);

            stream_buffer[wp] = l;
            wp = stream_next_index(wp);
            stream_buffer[wp] = r;
            wp = stream_next_index(wp);

            g_stream_write_pos = wp;
            g_stream.pending_size = 0U;
        }

        audio_block_ring_consume(&sd_audio_block_ring);
        g_stream.current_block = 0;
        g_stream.read_index = 0U;
    }

    {
        uint32_t fill = sampler_stream_fill_samples();
        if(fill != last_fill)
        {
            last_fill = fill;
            DBG("[STREAM] fill=%lu\r\n", (unsigned long)fill);
        }
    }
}
