#include "Sampler/sample_voice_reader.h"

#include <string.h>

#include "Storage/wav_audio_codec.h"

#if BRICK6_SAMPLER_DIAG_ENABLE
static sample_voice_reader_diag_snapshot_t g_sample_voice_reader_diag;
#define SAMPLE_VOICE_READER_DIAG_INC(field) (++g_sample_voice_reader_diag.field)
#else
#define SAMPLE_VOICE_READER_DIAG_INC(field) ((void)0)
#endif

static float sample_voice_reader_span_sample_l(const sample_cache_span_t *span, uint32_t frame_index)
{
    return span->l[(frame_index - span->start_frame) * span->frame_stride];
}

static float sample_voice_reader_span_sample_r(const sample_cache_span_t *span, uint32_t frame_index)
{
    if (span->is_mono != 0U)
    {
        return sample_voice_reader_span_sample_l(span, frame_index);
    }

    return span->r[(frame_index - span->start_frame) * span->frame_stride];
}

static void sample_voice_reader_normalize_position(float *io_position,
                                                   uint8_t *io_reverse,
                                                   float loop_start,
                                                   float loop_end,
                                                   float loop_length,
                                                   uint8_t loop_mode)
{
    if ((io_position == 0) || (io_reverse == 0))
    {
        return;
    }

    if ((loop_mode == 1U) && (*io_reverse == 0U) && (loop_length > 0.0f))
    {
        while (*io_position >= loop_end)
        {
            *io_position = loop_start + (*io_position - loop_end);
        }
    }
    else if ((loop_mode == 2U) && (loop_length > 0.0f))
    {
        while ((*io_reverse == 0U) && (*io_position >= loop_end))
        {
            *io_position = ((2.0f * loop_end) - *io_position) - 1.0f;
            *io_reverse = 1U;
        }

        while ((*io_reverse != 0U) && (*io_position < loop_start))
        {
            *io_position = (2.0f * loop_start) - *io_position;
            *io_reverse = 0U;
        }
    }
}

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
    SAMPLE_VOICE_READER_DIAG_INC(seek_calls);
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
    SAMPLE_VOICE_READER_DIAG_INC(render_pitch_forward_calls);
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
        sample_voice_reader_normalize_position(&reader->position,
                                               &reverse,
                                               loop_start,
                                               loop_end,
                                               loop_length,
                                               loop_mode);

        if ((((loop_mode == 0U) || (loop_length <= 0.0f)) && (reverse == 0U) && (reader->position >= (float)region_end))
            || ((((loop_mode == 0U) || (loop_length <= 0.0f)) && (reverse != 0U)
                 && (reader->position < (float)region_start))))
        {
            break;
        }

        const uint32_t base_frame = (uint32_t)reader->position;
        sample_cache_span_t span;
        SAMPLE_VOICE_READER_DIAG_INC(span_acquire_calls);
        if (sample_cache_try_acquire_span(reader->sample_id, base_frame, frames - produced, &span) == 0U)
        {
            if (out_underrun != 0)
            {
                *out_underrun = 1U;
            }
            break;
        }

        const uint32_t span_end = span.start_frame + span.frames;
        uint32_t segment_frames = 0U;
        uint8_t needs_neighbor_span = 0U;
        uint32_t neighbor_frame_index = 0U;
        float scan_position = reader->position;
        uint8_t scan_reverse = reverse;
        while ((produced + segment_frames) < frames)
        {
            const uint32_t scan_base_frame = (uint32_t)scan_position;
            if ((scan_base_frame < span.start_frame) || (scan_base_frame >= span_end))
            {
                break;
            }

            const uint8_t needs_neighbor =
                ((scan_reverse == 0U) && ((scan_base_frame + 1U) < region_end))
                || ((scan_reverse != 0U) && (scan_base_frame > region_start));
            if (needs_neighbor != 0U)
            {
                neighbor_frame_index =
                    (scan_reverse == 0U) ? (scan_base_frame + 1U) : (scan_base_frame - 1U);
                if ((neighbor_frame_index < span.start_frame) || (neighbor_frame_index >= span_end))
                {
                    needs_neighbor_span = 1U;
                }
            }

            segment_frames++;
            const float raw_next_position =
                scan_position + ((scan_reverse == 0U) ? reader->step : (-reader->step));
            const uint8_t prev_reverse = scan_reverse;
            scan_position = raw_next_position;
            sample_voice_reader_normalize_position(&scan_position,
                                                   &scan_reverse,
                                                   loop_start,
                                                   loop_end,
                                                   loop_length,
                                                   loop_mode);

            if ((((loop_mode == 0U) || (loop_length <= 0.0f)) && (scan_reverse == 0U)
                 && (scan_position >= (float)region_end))
                || ((((loop_mode == 0U) || (loop_length <= 0.0f)) && (scan_reverse != 0U)
                     && (scan_position < (float)region_start))))
            {
                break;
            }

            if ((scan_reverse != prev_reverse) || (scan_position != raw_next_position))
            {
                break;
            }

            if ((uint32_t)scan_position != scan_base_frame)
            {
                if ((((uint32_t)scan_position) < span.start_frame)
                    || (((uint32_t)scan_position) >= span_end))
                {
                    break;
                }
            }
        }

        sample_cache_span_t neighbor_span;
        memset(&neighbor_span, 0, sizeof(neighbor_span));
        if (needs_neighbor_span != 0U)
        {
            SAMPLE_VOICE_READER_DIAG_INC(neighbor_span_acquire_calls);
            (void)sample_cache_try_acquire_span(reader->sample_id, neighbor_frame_index, 1U, &neighbor_span);
        }

        float position = reader->position;
        uint8_t segment_reverse = reverse;
        SAMPLE_VOICE_READER_DIAG_INC(segments_mixed);
        for (uint32_t i = 0U; i < segment_frames; ++i)
        {
            const uint32_t segment_base_frame = (uint32_t)position;
            const float frac = position - (float)segment_base_frame;
            const float curr_l = sample_voice_reader_span_sample_l(&span, segment_base_frame);
            const float curr_r = sample_voice_reader_span_sample_r(&span, segment_base_frame);
            float next_l = curr_l;
            float next_r = curr_r;
            const uint8_t needs_neighbor =
                ((segment_reverse == 0U) && ((segment_base_frame + 1U) < region_end))
                || ((segment_reverse != 0U) && (segment_base_frame > region_start));
            if (needs_neighbor != 0U)
            {
                const uint32_t current_neighbor_frame =
                    (segment_reverse == 0U) ? (segment_base_frame + 1U) : (segment_base_frame - 1U);
                if ((current_neighbor_frame >= span.start_frame) && (current_neighbor_frame < span_end))
                {
                    next_l = sample_voice_reader_span_sample_l(&span, current_neighbor_frame);
                    next_r = sample_voice_reader_span_sample_r(&span, current_neighbor_frame);
                }
                else if ((neighbor_span.frames != 0U)
                         && (current_neighbor_frame >= neighbor_span.start_frame)
                         && (current_neighbor_frame < (neighbor_span.start_frame + neighbor_span.frames)))
                {
                    next_l = sample_voice_reader_span_sample_l(&neighbor_span, current_neighbor_frame);
                    next_r = sample_voice_reader_span_sample_r(&neighbor_span, current_neighbor_frame);
                }
            }

            float sample_l = 0.0f;
            float sample_r = 0.0f;
            wav_audio_codec_resample_linear(curr_l, curr_r, next_l, next_r, frac, &sample_l, &sample_r);
            const float fade =
                (fade_gain != 0) && ((produced + i) < fade_count) ? fade_gain[produced + i] : 1.0f;
            out_l[produced + i] += sample_l * gain * fade;
            out_r[produced + i] += sample_r * gain * fade;

            position += (segment_reverse == 0U) ? reader->step : (-reader->step);
            sample_voice_reader_normalize_position(&position,
                                                   &segment_reverse,
                                                   loop_start,
                                                   loop_end,
                                                   loop_length,
                                                   loop_mode);
        }

        sample_cache_release_span(reader->sample_id, &neighbor_span);
        sample_cache_release_span(reader->sample_id, &span);
        reader->position = position;
        reverse = segment_reverse;
        reader->frame_pos = (uint32_t)reader->position;
        sample_cache_set_voice_frame_pos(reader->cache_voice_id, reader->frame_pos);
        produced += segment_frames;
    }

    if (io_reverse != 0)
    {
        *io_reverse = reverse;
    }

    return produced;
}

void sample_voice_reader_diag_reset(void)
{
#if BRICK6_SAMPLER_DIAG_ENABLE
    memset(&g_sample_voice_reader_diag, 0, sizeof(g_sample_voice_reader_diag));
#endif
}

void sample_voice_reader_diag_get_snapshot(sample_voice_reader_diag_snapshot_t *out_snapshot)
{
    if (out_snapshot == 0)
    {
        return;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
#if BRICK6_SAMPLER_DIAG_ENABLE
    *out_snapshot = g_sample_voice_reader_diag;
#endif
}
