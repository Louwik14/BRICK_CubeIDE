#include "Storage/wav_audio_codec.h"

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
    int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
    if ((v & 0x00800000L) != 0)
    {
        v |= (int32_t)0xFF000000L;
    }
    return (float)v * (1.0f / 8388608.0f);
}

float wav_audio_codec_pcm32_to_float(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] |
                          ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16) |
                          ((uint32_t)p[3] << 24));
    return (float)v * (1.0f / 2147483648.0f);
}

static void wav_audio_codec_decode_pcm16_mono_block(const uint8_t *src,
                                                     float *dst,
                                                     uint32_t frame_count)
{
    for (uint32_t frame = 0U; frame < frame_count; ++frame)
    {
        dst[0] = wav_audio_codec_pcm16_to_float_impl(src);
        src += 2U;
        dst += 1U;
    }
}

static void wav_audio_codec_decode_pcm16_stereo_block(const uint8_t *src,
                                                       float *dst,
                                                       uint32_t frame_count)
{
    for (uint32_t frame = 0U; frame < frame_count; ++frame)
    {
        dst[0] = wav_audio_codec_pcm16_to_float_impl(src);
        dst[1] = wav_audio_codec_pcm16_to_float_impl(src + 2U);
        src += 4U;
        dst += 2U;
    }
}

static void wav_audio_codec_decode_pcm24_mono_block(const uint8_t *src,
                                                     float *dst,
                                                     uint32_t frame_count)
{
    for (uint32_t frame = 0U; frame < frame_count; ++frame)
    {
        dst[0] = wav_audio_codec_pcm24_to_float(src);
        src += 3U;
        dst += 1U;
    }
}

static void wav_audio_codec_decode_pcm24_stereo_block(const uint8_t *src,
                                                       float *dst,
                                                       uint32_t frame_count)
{
    for (uint32_t frame = 0U; frame < frame_count; ++frame)
    {
        dst[0] = wav_audio_codec_pcm24_to_float(src);
        dst[1] = wav_audio_codec_pcm24_to_float(src + 3U);
        src += 6U;
        dst += 2U;
    }
}

static void wav_audio_codec_decode_pcm32_mono_block(const uint8_t *src,
                                                     float *dst,
                                                     uint32_t frame_count)
{
    for (uint32_t frame = 0U; frame < frame_count; ++frame)
    {
        dst[0] = wav_audio_codec_pcm32_to_float(src);
        src += 4U;
        dst += 1U;
    }
}

static void wav_audio_codec_decode_pcm32_stereo_block(const uint8_t *src,
                                                       float *dst,
                                                       uint32_t frame_count)
{
    for (uint32_t frame = 0U; frame < frame_count; ++frame)
    {
        dst[0] = wav_audio_codec_pcm32_to_float(src);
        dst[1] = wav_audio_codec_pcm32_to_float(src + 4U);
        src += 8U;
        dst += 2U;
    }
}

wav_audio_codec_decode_block_fn wav_audio_codec_select_pcm_decode_block(uint16_t channels,
                                                                        uint16_t bits_per_sample)
{
    if (channels == 1U)
    {
        if (bits_per_sample == 16U)
        {
            return wav_audio_codec_decode_pcm16_mono_block;
        }
        if (bits_per_sample == 24U)
        {
            return wav_audio_codec_decode_pcm24_mono_block;
        }
        if (bits_per_sample == 32U)
        {
            return wav_audio_codec_decode_pcm32_mono_block;
        }
    }
    else if (channels == 2U)
    {
        if (bits_per_sample == 16U)
        {
            return wav_audio_codec_decode_pcm16_stereo_block;
        }
        if (bits_per_sample == 24U)
        {
            return wav_audio_codec_decode_pcm24_stereo_block;
        }
        if (bits_per_sample == 32U)
        {
            return wav_audio_codec_decode_pcm32_stereo_block;
        }
    }

    return 0;
}

wav_audio_codec_decode_mono_block_fn wav_audio_codec_select_pcm_decode_mono_block(
    uint16_t bits_per_sample)
{
    if (bits_per_sample == 16U)
    {
        return wav_audio_codec_decode_pcm16_mono_block;
    }
    if (bits_per_sample == 24U)
    {
        return wav_audio_codec_decode_pcm24_mono_block;
    }
    if (bits_per_sample == 32U)
    {
        return wav_audio_codec_decode_pcm32_mono_block;
    }
    return 0;
}

void wav_audio_codec_decode_stereo_frame(const uint8_t *frame,
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
        if (bits_per_sample == 16U)
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
        if (bits_per_sample == 16U)
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
