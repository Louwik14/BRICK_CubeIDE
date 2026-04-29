#include "Sampler/sample_voice_reader.h"

#include <string.h>

#include "Storage/wav_audio_codec.h"

void sample_voice_reader_reset(sample_voice_reader_t *reader)
{
    if (reader == 0)
    {
        return;
    }

    memset(reader, 0, sizeof(*reader));
}

void sample_voice_reader_bind(sample_voice_reader_t *reader,
                              uint16_t sample_id,
                              uint8_t cache_voice_id,
                              uint32_t start_frame)
{
    if (reader == 0)
    {
        return;
    }

    reader->sample_id = sample_id;
    reader->cache_voice_id = cache_voice_id;
    reader->position = (float)start_frame;
    reader->step = 1.0f;
    reader->frame_pos = start_frame;
    reader->active = 1U;
}

void sample_voice_reader_set_step(sample_voice_reader_t *reader, float step)
{
    if (reader == 0)
    {
        return;
    }

    reader->step = (step > 0.0f) ? step : 1.0f;
}

void sample_voice_reader_seek(sample_voice_reader_t *reader, uint32_t frame_pos)
{
    if ((reader == 0) || (reader->active == 0U))
    {
        return;
    }

    reader->frame_pos = frame_pos;
    reader->position = (float)frame_pos;
    sample_cache_set_voice_frame_pos(reader->cache_voice_id, frame_pos);
}

void sample_voice_reader_stop(sample_voice_reader_t *reader)
{
    if (reader == 0)
    {
        return;
    }

    if (reader->active != 0U)
    {
        sample_cache_stop_voice(reader->cache_voice_id);
    }
    sample_voice_reader_reset(reader);
}

uint8_t sample_voice_reader_begin_block(sample_voice_reader_t *reader,
                                        uint32_t max_frames,
                                        sample_cache_block_t *out_block)
{
    if ((reader == 0) || (out_block == 0) || (reader->active == 0U))
    {
        return 0U;
    }

    return sample_cache_begin_read_block(reader->cache_voice_id, max_frames, out_block);
}

void sample_voice_reader_commit_block(sample_voice_reader_t *reader,
                                      uint32_t consumed_frames)
{
    if ((reader == 0) || (reader->active == 0U))
    {
        return;
    }

    sample_cache_commit_read_block(reader->cache_voice_id, consumed_frames);
    reader->frame_pos += consumed_frames;
    reader->position = (float)reader->frame_pos;
}

uint32_t sample_voice_reader_render_pitch_forward(sample_voice_reader_t *reader,
                                                  uint32_t region_start,
                                                  uint32_t region_end,
                                                  uint8_t *io_reverse,
                                                  uint8_t loop_mode,
                                                  float gain,
                                                  const float *fade_gain,
                                                  uint32_t fade_count,
                                                  float *out_l,
                                                  float *out_r,
                                                  uint32_t frames,
                                                  uint8_t *out_underrun)
{
    if (out_underrun != 0)
    {
        *out_underrun = 0U;
    }

    if ((reader == 0) || (reader->active == 0U) || (out_l == 0) || (out_r == 0) || (frames == 0U))
    {
        return 0U;
    }

    uint32_t produced = 0U;
    const float loop_start = (float)region_start;
    const float loop_end = (float)region_end;
    const float loop_length = (float)(region_end - region_start);
    uint8_t reverse = ((io_reverse != 0) && (*io_reverse != 0U)) ? 1U : 0U;
    while (produced < frames)
    {
        if ((loop_mode == 1U) && (reverse == 0U) && (loop_length > 0.0f))
        {
            while (reader->position >= loop_end)
            {
                reader->position = loop_start + (reader->position - loop_end);
            }
        }
        else if ((loop_mode == 2U) && (loop_length > 0.0f))
        {
            while ((reverse == 0U) && (reader->position >= loop_end))
            {
                reader->position = ((2.0f * loop_end) - reader->position) - 1.0f;
                reverse = 1U;
            }

            while ((reverse != 0U) && (reader->position < loop_start))
            {
                reader->position = (2.0f * loop_start) - reader->position;
                reverse = 0U;
            }
        }

        if ((((loop_mode == 0U) || (loop_length <= 0.0f)) && (reverse == 0U) && (reader->position >= (float)region_end))
            || ((((loop_mode == 0U) || (loop_length <= 0.0f)) && (reverse != 0U)
                 && (reader->position < (float)region_start))))
        {
            break;
        }

        const uint32_t base_frame = (uint32_t)reader->position;
        const float frac = reader->position - (float)base_frame;
        float curr_l = 0.0f;
        float curr_r = 0.0f;
        float next_l = 0.0f;
        float next_r = 0.0f;
        const uint32_t neighbor_frame = (reverse == 0U) ? (base_frame + 1U)
                                                        : ((base_frame > 0U) ? (base_frame - 1U) : 0U);

        if (sample_cache_peek_frame(reader->sample_id, base_frame, &curr_l, &curr_r) == 0U)
        {
            if (out_underrun != 0)
            {
                *out_underrun = 1U;
            }
            break;
        }

        if (((reverse == 0U) && (neighbor_frame < region_end))
            || ((reverse != 0U) && (base_frame > region_start)))
        {
            if (sample_cache_peek_frame(reader->sample_id, neighbor_frame, &next_l, &next_r) == 0U)
            {
                next_l = curr_l;
                next_r = curr_r;
            }
        }
        else
        {
            next_l = curr_l;
            next_r = curr_r;
        }

        float sample_l = 0.0f;
        float sample_r = 0.0f;
        wav_audio_codec_resample_linear(curr_l, curr_r, next_l, next_r, frac, &sample_l, &sample_r);
        const float fade = (fade_gain != 0) && (produced < fade_count) ? fade_gain[produced] : 1.0f;
        out_l[produced] += sample_l * gain * fade;
        out_r[produced] += sample_r * gain * fade;

        reader->position += (reverse == 0U) ? reader->step : (-reader->step);
        if ((loop_mode == 1U) && (reverse == 0U) && (loop_length > 0.0f))
        {
            while (reader->position >= loop_end)
            {
                reader->position = loop_start + (reader->position - loop_end);
            }
        }
        else if ((loop_mode == 2U) && (loop_length > 0.0f))
        {
            while ((reverse == 0U) && (reader->position >= loop_end))
            {
                reader->position = ((2.0f * loop_end) - reader->position) - 1.0f;
                reverse = 1U;
            }

            while ((reverse != 0U) && (reader->position < loop_start))
            {
                reader->position = (2.0f * loop_start) - reader->position;
                reverse = 0U;
            }
        }
        reader->frame_pos = (uint32_t)reader->position;
        sample_cache_set_voice_frame_pos(reader->cache_voice_id, reader->frame_pos);
        produced++;
    }

    if (io_reverse != 0)
    {
        *io_reverse = reverse;
    }

    return produced;
}
