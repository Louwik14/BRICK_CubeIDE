#include "Sampler/sample_voice_reader.h"

#include <string.h>

#include "Storage/wav_audio_codec.h"

#if BRICK6_SAMPLER_DIAG_ENABLE
static sample_voice_reader_diag_snapshot_t g_sample_voice_reader_diag;
#define SAMPLE_VOICE_READER_DIAG_INC(field) (++g_sample_voice_reader_diag.field)
#else
#define SAMPLE_VOICE_READER_DIAG_INC(field) ((void)0)
#endif

#define SAMPLE_Q16_ONE (65536U)

typedef struct
{
    uint8_t cache_voice_id;
    uint16_t sample_id;
    float position;
    float step;
    uint32_t frame_pos;
    uint8_t active;
    sample_play_plan_t plan;
    sample_audio_cursor_t audio_cursor;
    uint8_t plan_valid;
} sample_voice_reader_state_t;

static sample_voice_reader_state_t *sample_voice_reader_state(sample_voice_reader_t *reader)
{
    return (sample_voice_reader_state_t *)reader;
}

static void sample_voice_reader_release_audio_cursor(sample_voice_reader_state_t *state)
{
    if (state == 0)
    {
        return;
    }

    if ((state->audio_cursor.current_acquired != 0U) && (state->sample_id < SAMPLE_PAGE_CACHE_MAX_SAMPLES))
    {
        sample_page_cache_release_page_ref(state->sample_id, &state->audio_cursor.current_page_ref);
    }

    memset(&state->audio_cursor, 0, sizeof(state->audio_cursor));
}

static uint8_t sample_voice_reader_acquire_audio_page(sample_voice_reader_state_t *state,
                                                      uint32_t frame_pos)
{
    if ((state == 0) || (state->sample_id >= SAMPLE_PAGE_CACHE_MAX_SAMPLES))
    {
        return 0U;
    }

    sample_page_span_t span;
    if (sample_page_cache_try_acquire_page(state->sample_id, frame_pos / SAMPLE_PAGE_FRAMES, &span) == 0U)
    {
        return 0U;
    }

    state->audio_cursor.current_page_ref.page_index = span.page_index;
    state->audio_cursor.current_page_ref.page_generation = span.page_generation;
    state->audio_cursor.current_page_ref.slot_index = span.slot_index;
    state->audio_cursor.current_base = span.frames_interleaved;
    state->audio_cursor.current_start_frame = span.start_frame;
    state->audio_cursor.current_frame_count = span.frame_count;
    state->audio_cursor.current_offset_frames = frame_pos - span.start_frame;
    state->audio_cursor.current_acquired = 1U;
    state->audio_cursor.active = 1U;
    return 1U;
}

static uint32_t sample_voice_reader_forward_end_frame(const sample_voice_reader_state_t *state)
{
    if (state == 0)
    {
        return 0U;
    }

    if ((state->plan.loop_mode != 0U) && (state->plan.loop_end > state->plan.loop_begin))
    {
        return state->plan.loop_end;
    }

    return state->plan.region_end;
}

static uint8_t sample_voice_reader_pingpong_span_valid(const sample_voice_reader_state_t *state)
{
    if (state == 0)
    {
        return 0U;
    }

    return (state->plan.loop_end > (state->plan.loop_begin + 1U)) ? 1U : 0U;
}

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

    sample_voice_reader_state_t *const state = sample_voice_reader_state(reader);
    sample_voice_reader_release_audio_cursor(state);
    memset(state, 0, sizeof(*state));
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

    sample_voice_reader_reset(reader);
    reader->sample_id = sample_id;
    reader->cache_voice_id = cache_voice_id;
    reader->position = (float)start_frame;
    reader->step = 1.0f;
    reader->frame_pos = start_frame;
    reader->active = 1U;
}

uint8_t sample_voice_reader_bind_play_plan(sample_voice_reader_t *reader,
                                           const sample_play_plan_t *plan,
                                           uint8_t cache_voice_id)
{
    if ((reader == 0) || (plan == 0))
    {
        return 0U;
    }

    sample_voice_reader_state_t *const state = sample_voice_reader_state(reader);
    sample_voice_reader_reset(reader);
    state->sample_id = plan->sample_id;
    state->cache_voice_id = cache_voice_id;
    state->position = (float)plan->start_frame;
    state->step = (float)plan->step_q16 / (float)SAMPLE_Q16_ONE;
    state->frame_pos = plan->start_frame;
    state->active = 1U;
    state->plan = *plan;
    state->plan_valid = 1U;
    sample_cache_set_voice_direction(cache_voice_id, (plan->direction != 0U) ? -1 : 1);

    if (sample_voice_reader_acquire_audio_page(state, plan->start_frame) == 0U)
    {
        sample_voice_reader_reset(reader);
        return 0U;
    }

    return 1U;
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

uint8_t sample_voice_reader_begin_segment(sample_voice_reader_t *reader,
                                          uint32_t max_frames,
                                          sample_audio_segment_t *out_segment)
{
    SAMPLE_VOICE_READER_DIAG_INC(begin_segment_calls);
    if (out_segment == 0)
    {
        return 0U;
    }

    memset(out_segment, 0, sizeof(*out_segment));
    out_segment->status = SAMPLE_AUDIO_SEGMENT_NOT_READY;

    if ((reader == 0) || (reader->active == 0U) || (max_frames == 0U))
    {
        return 0U;
    }

    sample_voice_reader_state_t *const state = sample_voice_reader_state(reader);
    if ((state->plan_valid == 0U)
        || ((state->plan.kernel_type != SAMPLE_KERNEL_FWD_1X)
            && (state->plan.kernel_type != SAMPLE_KERNEL_REV_1X)))
    {
        return 0U;
    }

    const uint32_t forward_end = sample_voice_reader_forward_end_frame(state);
    if ((state->plan.kernel_type == SAMPLE_KERNEL_FWD_1X) && (state->frame_pos >= forward_end))
    {
        out_segment->status = SAMPLE_AUDIO_SEGMENT_DONE;
        return 1U;
    }

    if (state->audio_cursor.current_acquired == 0U)
    {
        if (sample_voice_reader_acquire_audio_page(state, state->frame_pos) == 0U)
        {
            out_segment->status = SAMPLE_AUDIO_SEGMENT_UNDERRUN;
            return 1U;
        }
    }

    uint32_t available = 0U;
    if (state->plan.kernel_type == SAMPLE_KERNEL_REV_1X)
    {
        available = state->audio_cursor.current_offset_frames + 1U;
        const uint32_t region_remaining = (state->frame_pos - state->plan.region_begin) + 1U;
        if (available > region_remaining)
        {
            available = region_remaining;
        }
    }
    else
    {
        available = state->audio_cursor.current_frame_count - state->audio_cursor.current_offset_frames;
        const uint32_t region_remaining = forward_end - state->frame_pos;
        if (available > region_remaining)
        {
            available = region_remaining;
        }
    }
    if (available > max_frames)
    {
        available = max_frames;
    }
    if (available == 0U)
    {
        out_segment->status = SAMPLE_AUDIO_SEGMENT_UNDERRUN;
        return 1U;
    }

    out_segment->l = &state->audio_cursor.current_base[state->audio_cursor.current_offset_frames * 2U];
    out_segment->r = &state->audio_cursor.current_base[(state->audio_cursor.current_offset_frames * 2U) + 1U];
    out_segment->frames = available;
    out_segment->frame_stride = 2U;
    out_segment->start_frame = state->frame_pos;
    out_segment->is_mono = 0U;
    out_segment->kernel_type = state->plan.kernel_type;
    out_segment->status = SAMPLE_AUDIO_SEGMENT_OK;
    return 1U;
}

void sample_voice_reader_commit_segment(sample_voice_reader_t *reader,
                                        uint32_t consumed_frames)
{
    SAMPLE_VOICE_READER_DIAG_INC(commit_segment_calls);
    if ((reader == 0) || (reader->active == 0U))
    {
        return;
    }

    sample_voice_reader_state_t *const state = sample_voice_reader_state(reader);
    if ((state->plan_valid == 0U) || ((state->plan.kernel_type != SAMPLE_KERNEL_FWD_1X)
                                      && (state->plan.kernel_type != SAMPLE_KERNEL_REV_1X)))
    {
        return;
    }

    const uint32_t forward_end = sample_voice_reader_forward_end_frame(state);
    uint8_t wrapped_loop = 0U;
    uint8_t bounced_pingpong = 0U;

    if (state->plan.kernel_type == SAMPLE_KERNEL_REV_1X)
    {
        const uint32_t region_remaining = (state->frame_pos - state->plan.region_begin) + 1U;
        if (consumed_frames >= region_remaining)
        {
            if ((state->plan.loop_mode == 2U) && (sample_voice_reader_pingpong_span_valid(state) != 0U))
            {
                state->frame_pos = state->plan.loop_begin + 1U;
                state->plan.direction = 0U;
                state->plan.kernel_type = SAMPLE_KERNEL_FWD_1X;
                sample_cache_set_voice_direction(state->cache_voice_id, 1);
                state->position = (float)state->frame_pos;
                bounced_pingpong = 1U;
            }
            else
            {
                state->frame_pos = state->plan.region_begin;
                state->position = (float)((int32_t)state->plan.region_begin - 1);
                state->active = 0U;
            }
        }
        else
        {
            state->frame_pos -= consumed_frames;
            state->position = (float)state->frame_pos;
        }
    }
    else
    {
        state->frame_pos += consumed_frames;
        if ((state->plan.loop_mode == 2U) && (state->frame_pos >= forward_end))
        {
            if (sample_voice_reader_pingpong_span_valid(state) != 0U)
            {
                state->frame_pos = forward_end - 2U;
                state->plan.direction = 1U;
                state->plan.kernel_type = SAMPLE_KERNEL_REV_1X;
                sample_cache_set_voice_direction(state->cache_voice_id, -1);
                state->position = (float)state->frame_pos;
                bounced_pingpong = 1U;
            }
            else
            {
                state->active = 0U;
                state->position = (float)forward_end;
            }
        }
        else if ((state->plan.loop_mode != 0U) && (forward_end > state->plan.loop_begin)
                 && (state->frame_pos >= forward_end))
        {
            state->frame_pos = state->plan.loop_begin;
            wrapped_loop = 1U;
            sample_cache_set_voice_direction(state->cache_voice_id, 1);
        }
        if ((wrapped_loop == 0U) && (bounced_pingpong == 0U))
        {
            state->position = (float)state->frame_pos;
        }
    }
    sample_cache_update_voice_frame_pos(state->cache_voice_id, state->frame_pos);

    if (state->audio_cursor.current_acquired == 0U)
    {
        return;
    }

    const uint32_t previous_offset_frames = state->audio_cursor.current_offset_frames;
    if (state->plan.kernel_type == SAMPLE_KERNEL_REV_1X)
    {
        if (consumed_frames > previous_offset_frames)
        {
            state->audio_cursor.current_offset_frames = 0U;
        }
        else
        {
            state->audio_cursor.current_offset_frames -= consumed_frames;
        }
    }
    else
    {
        state->audio_cursor.current_offset_frames += consumed_frames;
    }

    if (((state->plan.kernel_type == SAMPLE_KERNEL_FWD_1X) && (state->plan.loop_mode == 0U)
         && (state->frame_pos >= forward_end))
        || ((state->plan.kernel_type == SAMPLE_KERNEL_REV_1X) && (state->active == 0U)))
    {
        sample_voice_reader_release_audio_cursor(state);
        return;
    }

    if (wrapped_loop != 0U)
    {
        sample_voice_reader_release_audio_cursor(state);
        (void)sample_voice_reader_acquire_audio_page(state, state->frame_pos);
        return;
    }

    if (bounced_pingpong != 0U)
    {
        sample_voice_reader_release_audio_cursor(state);
        (void)sample_voice_reader_acquire_audio_page(state, state->frame_pos);
        return;
    }

    if ((state->plan.kernel_type == SAMPLE_KERNEL_REV_1X) && (consumed_frames <= previous_offset_frames))
    {
        return;
    }

    if ((state->plan.kernel_type == SAMPLE_KERNEL_FWD_1X)
        && (state->audio_cursor.current_offset_frames < state->audio_cursor.current_frame_count))
    {
        return;
    }

    sample_voice_reader_release_audio_cursor(state);
    (void)sample_voice_reader_acquire_audio_page(state, state->frame_pos);
}

void sample_voice_reader_mix_fwd_1x(const sample_audio_segment_t *segment,
                                    float gain,
                                    const float *fade_gain,
                                    uint32_t fade_count,
                                    float *out_l,
                                    float *out_r,
                                    uint32_t out_offset)
{
    SAMPLE_VOICE_READER_DIAG_INC(mix_fwd_1x_calls);
    if ((segment == 0) || (out_l == 0) || (out_r == 0) || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *src_l = segment->l;
    const float *src_r = segment->r;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        const float sample_gain = gain * fade;
        out_l[out_offset + i] += src_l[i * segment->frame_stride] * sample_gain;
        out_r[out_offset + i] += src_r[i * segment->frame_stride] * sample_gain;
    }
}

void sample_voice_reader_mix_rev_1x(const sample_audio_segment_t *segment,
                                    float gain,
                                    const float *fade_gain,
                                    uint32_t fade_count,
                                    float *out_l,
                                    float *out_r,
                                    uint32_t out_offset)
{
    SAMPLE_VOICE_READER_DIAG_INC(mix_rev_1x_calls);
    if ((segment == 0) || (out_l == 0) || (out_r == 0) || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *src_l = segment->l;
    const float *src_r = segment->r;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        const float sample_gain = gain * fade;
        out_l[out_offset + i] += src_l[-((int32_t)i * (int32_t)segment->frame_stride)] * sample_gain;
        out_r[out_offset + i] += src_r[-((int32_t)i * (int32_t)segment->frame_stride)] * sample_gain;
    }
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
        sample_cache_update_voice_frame_pos(reader->cache_voice_id, reader->frame_pos);
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
