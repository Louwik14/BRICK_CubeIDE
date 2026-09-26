#include "Storage/wav_audio_codec.h"

#include <string.h>

#if defined(__GNUC__)
#define WAV_AUDIO_CODEC_PCM24_HOT __attribute__((optimize("O3"), hot))
#define WAV_AUDIO_CODEC_ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define WAV_AUDIO_CODEC_PCM24_HOT
#define WAV_AUDIO_CODEC_ALWAYS_INLINE inline
#endif

static WAV_AUDIO_CODEC_ALWAYS_INLINE float
wav_audio_codec_pcm24_to_float_impl(const uint8_t *p)
{
    const uint32_t packed = (uint32_t)p[0]
                          | ((uint32_t)p[1] << 8)
                          | ((uint32_t)p[2] << 16);
    const int32_t sample = ((int32_t)(packed << 8)) >> 8;
    return (float)sample * (1.0f / 8388608.0f);
}

static float wav_audio_codec_pcm16_to_float_impl(const uint8_t *p)
{
    int16_t v = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    return (float)v * (1.0f / 32768.0f);
}

float wav_audio_codec_pcm16_to_float(const uint8_t *p)
{
    return wav_audio_codec_pcm16_to_float_impl(p);
}

float wav_audio_codec_pcm24_to_float(const uint8_t *p)
{
    return wav_audio_codec_pcm24_to_float_impl(p);
}

float wav_audio_codec_pcm32_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] |
                          ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16) |
                          ((uint32_t)p[3] << 24));
    return (float)v * (1.0f / 2147483648.0f);
}

float wav_audio_codec_float32_to_float(const uint8_t *p)
{
    const uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                        | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

WAV_AUDIO_CODEC_PCM24_HOT void
wav_audio_codec_decode_pcm24_stereo_block(const uint8_t *src,
                                          float *dst,
                                          uint32_t frame_count)
{
    while (frame_count >= 2U)
    {
        dst[0] = wav_audio_codec_pcm24_to_float_impl(src);
        dst[1] = wav_audio_codec_pcm24_to_float_impl(src + 3U);
        dst[2] = wav_audio_codec_pcm24_to_float_impl(src + 6U);
        dst[3] = wav_audio_codec_pcm24_to_float_impl(src + 9U);
        src += 12U;
        dst += 4U;
        frame_count -= 2U;
    }
    if (frame_count != 0U)
    {
        dst[0] = wav_audio_codec_pcm24_to_float_impl(src);
        dst[1] = wav_audio_codec_pcm24_to_float_impl(src + 3U);
    }
}

void wav_audio_codec_decode_stereo_frame(const uint8_t *frame,
                                         wav_sample_encoding_t encoding,
                                         uint16_t channels,
                                         uint16_t bits_per_sample,
                                         float *out_left,
                                         float *out_right)
{
    float left = 0.0f;
    float right = 0.0f;

    if ((frame == 0) || (out_left == 0) || (out_right == 0) || (channels == 0U))
    {
        return;
    }

    if (channels == 1U)
    {
        if (encoding == WAV_SAMPLE_ENCODING_IEEE_FLOAT)
        {
            left = right = wav_audio_codec_float32_to_float(frame);
        }
        else if (bits_per_sample == 16U)
        {
            left = right = wav_audio_codec_pcm16_to_float(frame);
        }
        else if (bits_per_sample == 24U)
        {
            left = right = wav_audio_codec_pcm24_to_float(frame);
        }
        else
        {
            left = right = wav_audio_codec_pcm32_to_float(frame);
        }
    }
    else
    {
        if (encoding == WAV_SAMPLE_ENCODING_IEEE_FLOAT)
        {
            left = wav_audio_codec_float32_to_float(frame);
            right = wav_audio_codec_float32_to_float(&frame[4]);
        }
        else if (bits_per_sample == 16U)
        {
            left = wav_audio_codec_pcm16_to_float(frame);
            right = wav_audio_codec_pcm16_to_float(&frame[2]);
        }
        else if (bits_per_sample == 24U)
        {
            left = wav_audio_codec_pcm24_to_float(frame);
            right = wav_audio_codec_pcm24_to_float(&frame[3]);
        }
        else
        {
            left = wav_audio_codec_pcm32_to_float(frame);
            right = wav_audio_codec_pcm32_to_float(&frame[4]);
        }
    }

    *out_left = left;
    *out_right = right;
}

void wav_audio_codec_resample_linear(float prev_left,
                                     float prev_right,
                                     float curr_left,
                                     float curr_right,
                                     float frac,
                                     float *out_left,
                                     float *out_right)
{
    if ((out_left == 0) || (out_right == 0))
    {
        return;
    }

    if (frac < 0.0f)
    {
        frac = 0.0f;
    }
    else if (frac > 1.0f)
    {
        frac = 1.0f;
    }

    *out_left = prev_left + ((curr_left - prev_left) * frac);
    *out_right = prev_right + ((curr_right - prev_right) * frac);
}
