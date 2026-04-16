#include "Storage/wav_audio_stream.h"

#include <string.h>

#include "Storage/wav_audio_codec.h"

static void wav_audio_stream_reset_source_state(wav_audio_stream_t *stream)
{
    if (stream == 0)
    {
        return;
    }

    stream->stream_initialized = 0U;
    stream->source_exhausted = 0U;
    stream->stream_ended = 0U;
    stream->source_prev_valid = 0U;
    stream->source_curr_valid = 0U;
    stream->io_error = 0U;
    stream->data_remaining = 0U;
    stream->io_pos = 0U;
    stream->io_len = 0U;
    stream->prev_index = 0U;
    stream->curr_index = 0U;
    stream->prev_l = 0.0f;
    stream->prev_r = 0.0f;
    stream->curr_l = 0.0f;
    stream->curr_r = 0.0f;
    stream->phase = 0.0;
    stream->phase_step = 1.0;
}

void wav_audio_stream_init(wav_audio_stream_t *stream,
                           FIL *fp,
                           const wav_info_t *info,
                           uint32_t target_rate)
{
    if (stream == 0)
    {
        return;
    }

    memset(stream, 0, sizeof(*stream));
    stream->fp = fp;
    if (info != 0)
    {
        stream->info = *info;
    }
    stream->target_rate = (target_rate == 0U) ? 48000U : target_rate;
    wav_audio_stream_reset_source_state(stream);
}

static uint8_t wav_audio_stream_refill_io_buffer(wav_audio_stream_t *stream)
{
    if ((stream == 0) || (stream->fp == 0))
    {
        return 0U;
    }

    if (stream->data_remaining == 0U)
    {
        stream->source_exhausted = 1U;
        return 0U;
    }

    uint32_t request = stream->data_remaining;
    if (request > sizeof(stream->io_buf))
    {
        request = (uint32_t)sizeof(stream->io_buf);
    }
    request -= (request % stream->info.block_align);
    if (request == 0U)
    {
        stream->data_remaining = 0U;
        stream->source_exhausted = 1U;
        return 0U;
    }

    UINT br = 0U;
    const FRESULT fr = f_read(stream->fp, stream->io_buf, request, &br);
    if ((fr != FR_OK) || (br < stream->info.block_align))
    {
        stream->data_remaining = 0U;
        stream->source_exhausted = 1U;
        stream->io_error = 1U;
        return 0U;
    }

    stream->io_pos = 0U;
    stream->io_len = br - (br % stream->info.block_align);
    if (stream->io_len == 0U)
    {
        stream->data_remaining = 0U;
        stream->source_exhausted = 1U;
        stream->io_error = 1U;
        return 0U;
    }

    if (br > stream->data_remaining)
    {
        stream->data_remaining = 0U;
    }
    else
    {
        stream->data_remaining -= br;
    }

    return 1U;
}

static uint8_t wav_audio_stream_decode_next_source_frame(wav_audio_stream_t *stream,
                                                         float *out_l,
                                                         float *out_r)
{
    const uint32_t block_align = stream->info.block_align;

    if ((stream == 0) || (out_l == 0) || (out_r == 0) || (block_align == 0U))
    {
        return 0U;
    }

    while ((stream->io_pos + block_align) > stream->io_len)
    {
        if (wav_audio_stream_refill_io_buffer(stream) == 0U)
        {
            return 0U;
        }
    }

    wav_audio_codec_decode_stereo_frame(&stream->io_buf[stream->io_pos],
                                        stream->info.channels,
                                        stream->info.bits_per_sample,
                                        out_l,
                                        out_r);
    stream->io_pos += block_align;
    return 1U;
}

static uint8_t wav_audio_stream_prepare(wav_audio_stream_t *stream)
{
    if (stream == 0)
    {
        return 0U;
    }

    if (stream->stream_initialized != 0U)
    {
        return 1U;
    }

    if (stream->info.block_align == 0U)
    {
        return 0U;
    }

    wav_audio_stream_reset_source_state(stream);
    stream->data_remaining = stream->info.data_size - (stream->info.data_size % stream->info.block_align);
    stream->phase_step = (stream->info.sample_rate == 0U)
                              ? 1.0
                              : ((double)stream->info.sample_rate / (double)stream->target_rate);

    if (wav_audio_stream_decode_next_source_frame(stream, &stream->prev_l, &stream->prev_r) == 0U)
    {
        return 0U;
    }
    stream->source_prev_valid = 1U;
    stream->prev_index = 0U;

    if (wav_audio_stream_decode_next_source_frame(stream, &stream->curr_l, &stream->curr_r) != 0U)
    {
        stream->source_curr_valid = 1U;
        stream->curr_index = 1U;
    }
    else
    {
        stream->curr_l = stream->prev_l;
        stream->curr_r = stream->prev_r;
        stream->source_curr_valid = 0U;
        stream->curr_index = 0U;
        stream->source_exhausted = 1U;
    }

    stream->phase = 0.0;
    stream->stream_initialized = 1U;
    return 1U;
}

uint8_t wav_audio_stream_start(wav_audio_stream_t *stream, uint32_t data_offset)
{
    if (stream == 0)
    {
        return 0U;
    }

    if ((stream->fp == 0) || (stream->info.block_align == 0U))
    {
        return 0U;
    }

    if (f_lseek(stream->fp, data_offset) != FR_OK)
    {
        return 0U;
    }

    wav_audio_stream_reset_source_state(stream);
    stream->data_remaining = stream->info.data_size - (stream->info.data_size % stream->info.block_align);
    return 1U;
}

static uint8_t wav_audio_stream_ensure_source_window(wav_audio_stream_t *stream, uint32_t target_index)
{
    while ((stream->source_curr_valid != 0U) && (stream->curr_index < (target_index + 1U)))
    {
        float next_l = 0.0f;
        float next_r = 0.0f;

        stream->prev_l = stream->curr_l;
        stream->prev_r = stream->curr_r;
        stream->prev_index = stream->curr_index;

        if (wav_audio_stream_decode_next_source_frame(stream, &next_l, &next_r) == 0U)
        {
            stream->source_curr_valid = 0U;
            stream->source_exhausted = 1U;
            break;
        }

        stream->curr_l = next_l;
        stream->curr_r = next_r;
        stream->curr_index++;
        stream->source_curr_valid = 1U;
    }

    if ((stream->source_curr_valid == 0U) && (target_index > stream->curr_index))
    {
        stream->stream_ended = 1U;
        return 0U;
    }

    return 1U;
}

uint8_t wav_audio_stream_next_frame(wav_audio_stream_t *stream, float *out_left, float *out_right)
{
    if ((stream == 0) || (out_left == 0) || (out_right == 0))
    {
        return 0U;
    }

    const uint32_t target_index = (uint32_t)stream->phase;

    if (stream->stream_initialized == 0U)
    {
        if (wav_audio_stream_prepare(stream) == 0U)
        {
            return 0U;
        }
    }

    if (wav_audio_stream_ensure_source_window(stream, target_index) == 0U)
    {
        return 0U;
    }

    if ((stream->source_curr_valid == 0U) && (target_index > stream->curr_index))
    {
        return 0U;
    }

    {
        const float frac = (float)(stream->phase - (double)target_index);
        wav_audio_codec_resample_linear(stream->prev_l,
                                        stream->prev_r,
                                        stream->curr_l,
                                        stream->curr_r,
                                        frac,
                                        out_left,
                                        out_right);
    }

    stream->phase += stream->phase_step;
    return 1U;
}
