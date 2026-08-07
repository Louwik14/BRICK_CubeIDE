#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stream/Multi internal FLOAT32 contract.  This is deliberately separate
 * from the Sampler RAM format: the two pools have different ownership and
 * lifetime rules even when their sample representations match.
 */
typedef enum
{
    SAMPLE_AUDIO_FORMAT_INVALID = 0,
    SAMPLE_AUDIO_FORMAT_FLOAT32_MONO,
    SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED
} sample_audio_format_t;

#define SAMPLE_AUDIO_FORMAT_PAGE_BYTES              (16U * 1024U)
#define SAMPLE_AUDIO_FORMAT_FLOAT_BYTES             (4U)
#define SAMPLE_AUDIO_FORMAT_MONO_STRIDE_FLOATS      (1U)
#define SAMPLE_AUDIO_FORMAT_STEREO_STRIDE_FLOATS   (2U)
#define SAMPLE_AUDIO_FORMAT_MONO_FRAMES_PER_PAGE    (4096U)
#define SAMPLE_AUDIO_FORMAT_STEREO_FRAMES_PER_PAGE (2048U)
#define SAMPLE_AUDIO_FORMAT_MIN_READY_FRAMES        (12288U)
#define SAMPLE_AUDIO_FORMAT_MONO_PRESOCLE_PAGES     (3U)
#define SAMPLE_AUDIO_FORMAT_STEREO_PRESOCLE_PAGES   (6U)
#define SAMPLE_AUDIO_FORMAT_MONO_WINDOW_PAGES       (3U)
#define SAMPLE_AUDIO_FORMAT_STEREO_WINDOW_PAGES     (6U)
#define SAMPLE_AUDIO_FORMAT_STREAM_HORIZON_FRAMES   SAMPLE_AUDIO_FORMAT_MIN_READY_FRAMES

static inline uint8_t sample_audio_format_is_valid(sample_audio_format_t format)
{
    return ((format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
            || (format == SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED)) ? 1U : 0U;
}

static inline sample_audio_format_t sample_audio_format_or_stereo(sample_audio_format_t format)
{
    return (sample_audio_format_is_valid(format) != 0U)
               ? format
               : SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED;
}

static inline uint16_t sample_audio_format_channels(sample_audio_format_t format)
{
    return (format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
               ? 1U
               : ((format == SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED) ? 2U : 0U);
}

static inline uint32_t sample_audio_format_stride_floats(sample_audio_format_t format)
{
    return (format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
               ? SAMPLE_AUDIO_FORMAT_MONO_STRIDE_FLOATS
               : ((format == SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED)
                      ? SAMPLE_AUDIO_FORMAT_STEREO_STRIDE_FLOATS
                      : 0U);
}

static inline uint32_t sample_audio_format_bytes_per_float_frame(sample_audio_format_t format)
{
    return sample_audio_format_stride_floats(format) * SAMPLE_AUDIO_FORMAT_FLOAT_BYTES;
}

static inline uint32_t sample_audio_format_frames_per_page(sample_audio_format_t format)
{
    return (format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
               ? SAMPLE_AUDIO_FORMAT_MONO_FRAMES_PER_PAGE
               : ((format == SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED)
                      ? SAMPLE_AUDIO_FORMAT_STEREO_FRAMES_PER_PAGE
                      : 0U);
}

static inline uint32_t sample_audio_format_page_index_from_frame(sample_audio_format_t format,
                                                                  uint32_t frame)
{
    const uint32_t frames_per_page = sample_audio_format_frames_per_page(format);
    return (frames_per_page != 0U) ? (uint32_t)((uint64_t)frame / frames_per_page) : 0U;
}

static inline uint32_t sample_audio_format_page_start_frame(sample_audio_format_t format,
                                                             uint32_t page)
{
    return (uint32_t)((uint64_t)page * sample_audio_format_frames_per_page(format));
}

static inline uint32_t sample_audio_format_required_page_count(sample_audio_format_t format,
                                                                uint32_t frame_count)
{
    const uint32_t frames_per_page = sample_audio_format_frames_per_page(format);
    if ((frames_per_page == 0U) || (frame_count == 0U))
    {
        return 0U;
    }
    return (uint32_t)(((uint64_t)frame_count + frames_per_page - 1ULL) / frames_per_page);
}

static inline uint32_t sample_audio_format_presocle_pages(sample_audio_format_t format)
{
    return sample_audio_format_required_page_count(format, SAMPLE_AUDIO_FORMAT_MIN_READY_FRAMES);
}

static inline uint32_t sample_audio_format_window_pages(sample_audio_format_t format)
{
    return sample_audio_format_presocle_pages(format);
}

static inline sample_audio_format_t sample_audio_format_from_channels(uint16_t channels)
{
    return (channels == 1U)
               ? SAMPLE_AUDIO_FORMAT_FLOAT32_MONO
               : ((channels == 2U) ? SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED
                                   : SAMPLE_AUDIO_FORMAT_INVALID);
}

/* wav_info.block_align is source-file metadata, never this FLOAT32 stride. */
static inline uint8_t sample_audio_format_matches_channels(sample_audio_format_t format,
                                                            uint16_t channels)
{
    return (sample_audio_format_from_channels(channels) == format) ? 1U : 0U;
}

#ifdef __cplusplus
}
#endif
