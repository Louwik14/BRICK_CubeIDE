#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t sample_stream_audio_frame_t;

#define SAMPLE_STREAM_AUDIO_FRAME_NEVER UINT64_MAX

/*
 * Monotonic audio time owned by the audio domain. The IRQ is the only writer;
 * non-audio code consumes a coherent snapshot through the sequence counter.
 */
void sample_stream_time_init(void);
void sample_stream_time_advance_from_audio_irq(uint32_t rendered_frames);
sample_stream_audio_frame_t sample_stream_time_now(void);

static inline sample_stream_audio_frame_t sample_stream_time_deadline_after(
    sample_stream_audio_frame_t now,
    uint32_t output_frames)
{
    if (output_frames == UINT32_MAX)
    {
        return SAMPLE_STREAM_AUDIO_FRAME_NEVER;
    }
    if (now > (SAMPLE_STREAM_AUDIO_FRAME_NEVER - (uint64_t)output_frames))
    {
        return SAMPLE_STREAM_AUDIO_FRAME_NEVER;
    }
    return now + (uint64_t)output_frames;
}

static inline uint32_t sample_stream_time_source_to_output_frames(uint32_t source_frames,
                                                                  uint32_t step_q16)
{
    if (source_frames == 0U)
    {
        return 0U;
    }
    if (step_q16 == 0U)
    {
        return UINT32_MAX;
    }

    const uint64_t output_frames = ((uint64_t)source_frames * 65536ULL) / step_q16;
    return (output_frames > UINT32_MAX) ? UINT32_MAX : (uint32_t)output_frames;
}

#ifdef __cplusplus
}
#endif
