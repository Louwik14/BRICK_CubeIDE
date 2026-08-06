#include "Sampler/sample_stream_time.h"

#include "stm32h7xx.h"

typedef struct
{
    volatile uint32_t sequence;
    volatile uint32_t low;
    volatile uint32_t high;
} sample_stream_audio_clock_t;

static sample_stream_audio_clock_t g_sample_stream_audio_clock;

void sample_stream_time_advance_from_audio_irq(uint32_t rendered_frames)
{
    g_sample_stream_audio_clock.sequence++;
    __DMB();

    const uint32_t previous_low = g_sample_stream_audio_clock.low;
    const uint32_t next_low = previous_low + rendered_frames;
    g_sample_stream_audio_clock.low = next_low;
    if (next_low < previous_low)
    {
        g_sample_stream_audio_clock.high++;
    }

    __DMB();
    g_sample_stream_audio_clock.sequence++;
}

sample_stream_audio_frame_t sample_stream_time_now(void)
{
    uint32_t sequence_before;
    uint32_t sequence_after;
    uint32_t low;
    uint32_t high;

    do
    {
        sequence_before = g_sample_stream_audio_clock.sequence;
        __DMB();
        high = g_sample_stream_audio_clock.high;
        low = g_sample_stream_audio_clock.low;
        __DMB();
        sequence_after = g_sample_stream_audio_clock.sequence;
    } while ((sequence_before != sequence_after) || ((sequence_before & 1U) != 0U));

    return ((uint64_t)high << 32) | low;
}
