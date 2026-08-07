#include "Sampler/sample_voice_reader.h"

#include <string.h>

#define SAMPLE_Q16_ONE (65536U)

typedef struct
{
    uint8_t cache_voice_id;
    uint8_t cache_voice_valid;
    uint16_t sample_id;
    sample_audio_key_t key;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
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

    if (state->audio_cursor.neighbor_acquired != 0U)
    {
        sample_page_cache_release_page_ref_key(state->key, &state->audio_cursor.neighbor_page_ref);
    }

    if (state->audio_cursor.current_acquired != 0U)
    {
        sample_page_cache_release_page_ref_key(state->key, &state->audio_cursor.current_page_ref);
    }

    memset(&state->audio_cursor, 0, sizeof(state->audio_cursor));
}

static uint8_t sample_voice_reader_acquire_audio_page(sample_voice_reader_state_t *state,
                                                      uint32_t frame_pos)
{
    if (state == 0)
    {
        return 0U;
    }

    const sample_audio_format_t expected_format = sample_audio_format_or_stereo(state->format);
    sample_page_span_t span;
    if (sample_page_cache_try_acquire_page_key(state->key,
                                               sample_audio_format_page_index_from_frame(expected_format, frame_pos),
                                               &span) == 0U)
    {
        return 0U;
    }

    if (sample_audio_format_or_stereo(span.format) != expected_format)
    {
        sample_page_cache_release_page_key(state->key, span.page_index);
        return 0U;
    }

    state->audio_cursor.current_page_ref.key = span.key;
    state->audio_cursor.current_page_ref.page_index = span.page_index;
    state->audio_cursor.current_page_ref.page_generation = span.page_generation;
    state->audio_cursor.current_page_ref.format = span.format;
    state->audio_cursor.current_page_ref.stride_floats = span.stride_floats;
    state->audio_cursor.current_page_ref.frames_per_page = span.frames_per_page;
    state->audio_cursor.current_page_ref.registration_epoch = span.registration_epoch;
    state->audio_cursor.current_page_ref.slot_index = span.slot_index;
    state->audio_cursor.current_base = span.frames_interleaved;
    state->audio_cursor.current_start_frame = span.start_frame;
    state->audio_cursor.current_frame_count = span.frame_count;
    state->audio_cursor.current_offset_frames = frame_pos - span.start_frame;
    state->audio_cursor.format = span.format;
    state->audio_cursor.stride_floats = span.stride_floats;
    state->audio_cursor.frames_per_page = span.frames_per_page;
    state->audio_cursor.registration_epoch = span.registration_epoch;
    state->audio_cursor.current_acquired = 1U;
    state->audio_cursor.active = 1U;
    return 1U;
}

static uint8_t sample_voice_reader_acquire_neighbor_page(sample_voice_reader_state_t *state,
                                                         uint32_t page_index)
{
    if (state == 0)
    {
        return 0U;
    }

    if ((state->audio_cursor.neighbor_acquired != 0U)
        && (state->audio_cursor.neighbor_page_ref.page_index == page_index))
    {
        return 1U;
    }

    if (state->audio_cursor.neighbor_acquired != 0U)
    {
        sample_page_cache_release_page_ref_key(state->key, &state->audio_cursor.neighbor_page_ref);
        state->audio_cursor.neighbor_acquired = 0U;
        state->audio_cursor.neighbor_base = 0;
        state->audio_cursor.neighbor_start_frame = 0U;
        state->audio_cursor.neighbor_frame_count = 0U;
    }

    sample_page_span_t span;
    if (sample_page_cache_try_acquire_page_key(state->key, page_index, &span) == 0U)
    {
        return 0U;
    }

    if (sample_audio_format_or_stereo(span.format) != state->audio_cursor.format)
    {
        sample_page_cache_release_page_key(state->key, span.page_index);
        return 0U;
    }

    state->audio_cursor.neighbor_page_ref.key = span.key;
    state->audio_cursor.neighbor_page_ref.page_index = span.page_index;
    state->audio_cursor.neighbor_page_ref.page_generation = span.page_generation;
    state->audio_cursor.neighbor_page_ref.format = span.format;
    state->audio_cursor.neighbor_page_ref.stride_floats = span.stride_floats;
    state->audio_cursor.neighbor_page_ref.frames_per_page = span.frames_per_page;
    state->audio_cursor.neighbor_page_ref.registration_epoch = span.registration_epoch;
    state->audio_cursor.neighbor_page_ref.slot_index = span.slot_index;
    state->audio_cursor.neighbor_base = span.frames_interleaved;
    state->audio_cursor.neighbor_start_frame = span.start_frame;
    state->audio_cursor.neighbor_frame_count = span.frame_count;
    state->audio_cursor.neighbor_acquired = 1U;
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

static uint8_t sample_voice_reader_prepare_pitch_forward_segment(sample_voice_reader_state_t *state,
                                                                 uint32_t max_frames,
                                                                 sample_audio_segment_t *out_segment)
{
    if ((state == 0) || (out_segment == 0) || (max_frames == 0U))
    {
        return 0U;
    }

    const uint32_t forward_end = sample_voice_reader_forward_end_frame(state);
    const uint8_t loop_forward = ((state->plan.loop_mode == 1U) && (forward_end > state->plan.loop_begin)) ? 1U : 0U;

    if (state->position >= (float)forward_end)
    {
        out_segment->status = SAMPLE_AUDIO_SEGMENT_DONE;
        return 1U;
    }

    if (state->audio_cursor.current_acquired == 0U)
    {
        if (sample_voice_reader_acquire_audio_page(state, (uint32_t)state->position) == 0U)
        {
            out_segment->status = SAMPLE_AUDIO_SEGMENT_UNDERRUN;
            return 1U;
        }
    }

    uint32_t segment_frames = 0U;
    uint8_t needs_neighbor = 0U;
    uint32_t neighbor_page_index = UINT32_MAX;
    float scan_position = state->position;

    while (segment_frames < max_frames)
    {
        if (scan_position >= (float)forward_end)
        {
            break;
        }

        const uint32_t base_frame = (uint32_t)scan_position;
        if ((base_frame < state->audio_cursor.current_start_frame)
            || (base_frame >= (state->audio_cursor.current_start_frame + state->audio_cursor.current_frame_count)))
        {
            break;
        }

        if ((base_frame + 1U) < forward_end)
        {
            const uint32_t neighbor_frame = base_frame + 1U;
            if ((neighbor_frame < state->audio_cursor.current_start_frame)
                || (neighbor_frame
                    >= (state->audio_cursor.current_start_frame + state->audio_cursor.current_frame_count)))
            {
                needs_neighbor = 1U;
                neighbor_page_index = sample_audio_format_page_index_from_frame(
                    state->audio_cursor.format,
                    neighbor_frame);
            }
        }
        else if (loop_forward != 0U)
        {
            const uint32_t neighbor_frame = state->plan.loop_begin;
            if ((neighbor_frame < state->audio_cursor.current_start_frame)
                || (neighbor_frame
                    >= (state->audio_cursor.current_start_frame + state->audio_cursor.current_frame_count)))
            {
                needs_neighbor = 1U;
                neighbor_page_index = sample_audio_format_page_index_from_frame(
                    state->audio_cursor.format,
                    neighbor_frame);
            }
        }

        segment_frames++;
        const float next_position = scan_position + state->step;
        if (next_position >= (float)forward_end)
        {
            break;
        }

        const uint32_t next_base = (uint32_t)next_position;
        if ((next_base < state->audio_cursor.current_start_frame)
            || (next_base >= (state->audio_cursor.current_start_frame + state->audio_cursor.current_frame_count)))
        {
            break;
        }

        scan_position = next_position;
    }

    if (segment_frames == 0U)
    {
        out_segment->status = SAMPLE_AUDIO_SEGMENT_UNDERRUN;
        return 1U;
    }

    if ((needs_neighbor != 0U) && (sample_voice_reader_acquire_neighbor_page(state, neighbor_page_index) == 0U))
    {
        out_segment->status = SAMPLE_AUDIO_SEGMENT_UNDERRUN;
        return 1U;
    }

    const float last_segment_position = state->position + (state->step * (float)(segment_frames - 1U));
    const uint8_t loop_neighbor_in_current =
        (loop_forward != 0U)
            && (state->audio_cursor.current_start_frame <= state->plan.loop_begin)
            && (state->plan.loop_begin
                < (state->audio_cursor.current_start_frame + state->audio_cursor.current_frame_count))
            && ((last_segment_position + state->step) >= (float)forward_end)
            ? 1U
            : 0U;
    const uint8_t is_mono = (state->audio_cursor.format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO) ? 1U : 0U;
    out_segment->l = state->audio_cursor.current_base;
    out_segment->r = (is_mono != 0U) ? out_segment->l : (state->audio_cursor.current_base + 1U);
    out_segment->neighbor_l = (loop_neighbor_in_current != 0U)
                                  ? state->audio_cursor.current_base
                                  : state->audio_cursor.neighbor_base;
    out_segment->neighbor_r = (loop_neighbor_in_current != 0U)
                                  ? ((is_mono != 0U) ? state->audio_cursor.current_base
                                                     : (state->audio_cursor.current_base + 1U))
                                  : ((state->audio_cursor.neighbor_base != 0)
                                         ? ((is_mono != 0U) ? state->audio_cursor.neighbor_base
                                                           : (state->audio_cursor.neighbor_base + 1U))
                                         : 0);
    out_segment->frames = segment_frames;
    out_segment->frame_stride = state->audio_cursor.stride_floats;
    out_segment->start_frame = (uint32_t)state->position;
    out_segment->source_start_frame = state->audio_cursor.current_start_frame;
    out_segment->source_frame_count = state->audio_cursor.current_frame_count;
    out_segment->neighbor_start_frame = (loop_neighbor_in_current != 0U)
                                           ? state->audio_cursor.current_start_frame
                                           : state->audio_cursor.neighbor_start_frame;
    out_segment->neighbor_frame_count = (loop_neighbor_in_current != 0U)
                                           ? state->audio_cursor.current_frame_count
                                           : state->audio_cursor.neighbor_frame_count;
    out_segment->source_limit_frame = forward_end;
    out_segment->source_region_begin = state->plan.region_begin;
    out_segment->source_position = state->position;
    out_segment->source_step = state->step;
    out_segment->format = state->audio_cursor.format;
    out_segment->stride_floats = state->audio_cursor.stride_floats;
    out_segment->frames_per_page = state->audio_cursor.frames_per_page;
    out_segment->registration_epoch = state->audio_cursor.registration_epoch;
    out_segment->is_mono = is_mono;
    out_segment->kernel_type = SAMPLE_KERNEL_PITCH_FWD_LINEAR;
    out_segment->status = SAMPLE_AUDIO_SEGMENT_OK;
    return 1U;
}

static uint8_t sample_voice_reader_prepare_pitch_reverse_segment(sample_voice_reader_state_t *state,
                                                                 uint32_t max_frames,
                                                                 sample_audio_segment_t *out_segment)
{
    if ((state == 0) || (out_segment == 0) || (max_frames == 0U))
    {
        return 0U;
    }

    if (state->position < (float)state->plan.region_begin)
    {
        out_segment->status = SAMPLE_AUDIO_SEGMENT_DONE;
        return 1U;
    }

    if (state->audio_cursor.current_acquired == 0U)
    {
        if (sample_voice_reader_acquire_audio_page(state, (uint32_t)state->position) == 0U)
        {
            out_segment->status = SAMPLE_AUDIO_SEGMENT_UNDERRUN;
            return 1U;
        }
    }

    uint32_t segment_frames = 0U;
    uint8_t needs_neighbor = 0U;
    uint32_t neighbor_page_index = UINT32_MAX;
    float scan_position = state->position;

    while (segment_frames < max_frames)
    {
        if (scan_position < (float)state->plan.region_begin)
        {
            break;
        }

        const uint32_t base_frame = (uint32_t)scan_position;
        if ((base_frame < state->audio_cursor.current_start_frame)
            || (base_frame >= (state->audio_cursor.current_start_frame + state->audio_cursor.current_frame_count)))
        {
            break;
        }

        if (base_frame > state->plan.region_begin)
        {
            const uint32_t neighbor_frame = base_frame - 1U;
            if ((neighbor_frame < state->audio_cursor.current_start_frame)
                || (neighbor_frame
                    >= (state->audio_cursor.current_start_frame + state->audio_cursor.current_frame_count)))
            {
                needs_neighbor = 1U;
                neighbor_page_index = sample_audio_format_page_index_from_frame(
                    state->audio_cursor.format,
                    neighbor_frame);
            }
        }

        segment_frames++;
        const float next_position = scan_position - state->step;
        if (next_position < (float)state->plan.region_begin)
        {
            break;
        }

        const uint32_t next_base = (uint32_t)next_position;
        if ((next_base < state->audio_cursor.current_start_frame)
            || (next_base >= (state->audio_cursor.current_start_frame + state->audio_cursor.current_frame_count)))
        {
            break;
        }

        scan_position = next_position;
    }

    if (segment_frames == 0U)
    {
        out_segment->status = SAMPLE_AUDIO_SEGMENT_UNDERRUN;
        return 1U;
    }

    if ((needs_neighbor != 0U) && (sample_voice_reader_acquire_neighbor_page(state, neighbor_page_index) == 0U))
    {
        out_segment->status = SAMPLE_AUDIO_SEGMENT_UNDERRUN;
        return 1U;
    }

    const uint8_t is_mono = (state->audio_cursor.format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO) ? 1U : 0U;
    out_segment->l = state->audio_cursor.current_base;
    out_segment->r = (is_mono != 0U) ? out_segment->l : (state->audio_cursor.current_base + 1U);
    out_segment->neighbor_l = state->audio_cursor.neighbor_base;
    out_segment->neighbor_r = (state->audio_cursor.neighbor_base != 0)
                                  ? ((is_mono != 0U) ? state->audio_cursor.neighbor_base
                                                    : (state->audio_cursor.neighbor_base + 1U))
                                  : 0;
    out_segment->frames = segment_frames;
    out_segment->frame_stride = state->audio_cursor.stride_floats;
    out_segment->start_frame = (uint32_t)state->position;
    out_segment->source_start_frame = state->audio_cursor.current_start_frame;
    out_segment->source_frame_count = state->audio_cursor.current_frame_count;
    out_segment->neighbor_start_frame = state->audio_cursor.neighbor_start_frame;
    out_segment->neighbor_frame_count = state->audio_cursor.neighbor_frame_count;
    out_segment->source_limit_frame = state->plan.region_end;
    out_segment->source_region_begin = state->plan.region_begin;
    out_segment->source_position = state->position;
    out_segment->source_step = state->step;
    out_segment->format = state->audio_cursor.format;
    out_segment->stride_floats = state->audio_cursor.stride_floats;
    out_segment->frames_per_page = state->audio_cursor.frames_per_page;
    out_segment->registration_epoch = state->audio_cursor.registration_epoch;
    out_segment->is_mono = is_mono;
    out_segment->kernel_type = SAMPLE_KERNEL_PITCH_REV_LINEAR;
    out_segment->status = SAMPLE_AUDIO_SEGMENT_OK;
    return 1U;
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
    reader->key = sample_audio_key_classic(sample_id);
    reader->format = SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED;
    reader->stride_floats = SAMPLE_AUDIO_FORMAT_STEREO_STRIDE_FLOATS;
    reader->frames_per_page = SAMPLE_AUDIO_FORMAT_STEREO_FRAMES_PER_PAGE;
    reader->cache_voice_id = cache_voice_id;
    reader->cache_voice_valid = 1U;
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
    state->key = plan->key;
    state->format = sample_audio_format_or_stereo(plan->format);
    state->stride_floats = (plan->stride_floats != 0U)
                               ? plan->stride_floats
                               : (uint16_t)sample_audio_format_stride_floats(state->format);
    state->frames_per_page = (plan->frames_per_page != 0U)
                                 ? plan->frames_per_page
                                 : sample_audio_format_frames_per_page(state->format);
    state->registration_epoch = plan->registration_epoch;
    state->cache_voice_id = cache_voice_id;
    state->cache_voice_valid = (cache_voice_id != UINT8_MAX) ? 1U : 0U;
    state->position = (float)plan->start_frame;
    state->step = (float)plan->step_q16 / (float)SAMPLE_Q16_ONE;
    state->frame_pos = plan->start_frame;
    state->active = 1U;
    state->plan = *plan;
    state->plan_valid = 1U;
    if (state->cache_voice_valid != 0U)
    {
        sample_cache_set_voice_direction(cache_voice_id, (plan->direction != 0U) ? -1 : 1);
    }

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

    {
        sample_voice_reader_state_t *const state = sample_voice_reader_state(reader);
        sample_voice_reader_release_audio_cursor(state);
    }
    reader->frame_pos = frame_pos;
    reader->position = (float)frame_pos;
    if (reader->cache_voice_valid != 0U)
    {
        sample_cache_set_voice_frame_pos(reader->cache_voice_id, frame_pos);
    }
}

void sample_voice_reader_update_frame_pos(sample_voice_reader_t *reader, uint32_t frame_pos)
{
    if ((reader == 0) || (reader->active == 0U))
    {
        return;
    }

    sample_voice_reader_state_t *const state = sample_voice_reader_state(reader);
    reader->frame_pos = frame_pos;
    reader->position = (float)frame_pos;
    if ((state->audio_cursor.current_acquired != 0U)
        && (frame_pos >= state->audio_cursor.current_start_frame)
        && (frame_pos < (state->audio_cursor.current_start_frame
                         + state->audio_cursor.current_frame_count)))
    {
        state->audio_cursor.current_offset_frames = frame_pos - state->audio_cursor.current_start_frame;
    }
    else if (state->audio_cursor.current_acquired != 0U)
    {
        sample_voice_reader_release_audio_cursor(state);
    }
}

void sample_voice_reader_stop(sample_voice_reader_t *reader)
{
    if (reader == 0)
    {
        return;
    }

    if ((reader->active != 0U) && (reader->cache_voice_valid != 0U))
    {
        sample_cache_stop_voice(reader->cache_voice_id);
    }
    sample_voice_reader_reset(reader);
}

uint8_t sample_voice_reader_begin_block(sample_voice_reader_t *reader,
                                        uint32_t max_frames,
                                        sample_cache_block_t *out_block)
{
    if (out_block == 0)
    {
        return 0U;
    }

    memset(out_block, 0, sizeof(*out_block));
    out_block->format = SAMPLE_AUDIO_FORMAT_INVALID;
    out_block->status = SAMPLE_CACHE_BLOCK_NOT_READY;
    if ((reader == 0) || (reader->active == 0U) || (max_frames == 0U))
    {
        return 0U;
    }

    sample_audio_segment_t segment;
    if (sample_voice_reader_begin_segment(reader, max_frames, &segment) == 0U)
    {
        return 0U;
    }

    out_block->l = segment.l;
    out_block->r = segment.r;
    out_block->frames = segment.frames;
    out_block->frame_stride = segment.frame_stride;
    out_block->frame_step = ((segment.kernel_type == SAMPLE_KERNEL_REV_1X)
                             || (segment.kernel_type == SAMPLE_KERNEL_PITCH_REV_LINEAR))
                                ? -(int32_t)segment.frame_stride
                                : (int32_t)segment.frame_stride;
    out_block->format = segment.format;
    out_block->frames_per_page = segment.frames_per_page;
    out_block->is_mono = segment.is_mono;
    switch (segment.status)
    {
        case SAMPLE_AUDIO_SEGMENT_OK:
            out_block->status = SAMPLE_CACHE_BLOCK_OK;
            break;

        case SAMPLE_AUDIO_SEGMENT_DONE:
            out_block->status = SAMPLE_CACHE_BLOCK_DONE;
            break;

        case SAMPLE_AUDIO_SEGMENT_UNDERRUN:
            out_block->status = SAMPLE_CACHE_BLOCK_UNDERRUN;
            break;

        case SAMPLE_AUDIO_SEGMENT_NOT_READY:
        default:
            out_block->status = SAMPLE_CACHE_BLOCK_NOT_READY;
            break;
    }
    return 1U;
}

void sample_voice_reader_commit_block(sample_voice_reader_t *reader,
                                      uint32_t consumed_frames)
{
    if ((reader == 0) || (reader->active == 0U))
    {
        return;
    }

    sample_voice_reader_commit_segment(reader, consumed_frames);
}

uint8_t sample_voice_reader_begin_segment(sample_voice_reader_t *reader,
                                          uint32_t max_frames,
                                          sample_audio_segment_t *out_segment)
{
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
            && (state->plan.kernel_type != SAMPLE_KERNEL_REV_1X)
            && (state->plan.kernel_type != SAMPLE_KERNEL_PITCH_FWD_LINEAR)
            && (state->plan.kernel_type != SAMPLE_KERNEL_PITCH_REV_LINEAR)))
    {
        return 0U;
    }

    if (state->plan.kernel_type == SAMPLE_KERNEL_PITCH_FWD_LINEAR)
    {
        return sample_voice_reader_prepare_pitch_forward_segment(state, max_frames, out_segment);
    }

    if (state->plan.kernel_type == SAMPLE_KERNEL_PITCH_REV_LINEAR)
    {
        return sample_voice_reader_prepare_pitch_reverse_segment(state, max_frames, out_segment);
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

    const uint8_t is_mono = (state->audio_cursor.format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO) ? 1U : 0U;
    const uint32_t frame_offset = state->audio_cursor.current_offset_frames
                                  * state->audio_cursor.stride_floats;
    out_segment->l = &state->audio_cursor.current_base[frame_offset];
    out_segment->r = (is_mono != 0U) ? out_segment->l : &state->audio_cursor.current_base[frame_offset + 1U];
    out_segment->frames = available;
    out_segment->frame_stride = state->audio_cursor.stride_floats;
    out_segment->start_frame = state->frame_pos;
    out_segment->format = state->audio_cursor.format;
    out_segment->stride_floats = state->audio_cursor.stride_floats;
    out_segment->frames_per_page = state->audio_cursor.frames_per_page;
    out_segment->registration_epoch = state->audio_cursor.registration_epoch;
    out_segment->is_mono = is_mono;
    out_segment->kernel_type = state->plan.kernel_type;
    out_segment->status = SAMPLE_AUDIO_SEGMENT_OK;
    return 1U;
}

void sample_voice_reader_commit_segment(sample_voice_reader_t *reader,
                                        uint32_t consumed_frames)
{
    if ((reader == 0) || (reader->active == 0U))
    {
        return;
    }

    sample_voice_reader_state_t *const state = sample_voice_reader_state(reader);
    if ((state->plan_valid == 0U) || ((state->plan.kernel_type != SAMPLE_KERNEL_FWD_1X)
                                      && (state->plan.kernel_type != SAMPLE_KERNEL_REV_1X)
                                      && (state->plan.kernel_type != SAMPLE_KERNEL_PITCH_FWD_LINEAR)
                                      && (state->plan.kernel_type != SAMPLE_KERNEL_PITCH_REV_LINEAR)))
    {
        return;
    }

    if (state->plan.kernel_type == SAMPLE_KERNEL_PITCH_FWD_LINEAR)
    {
        const uint32_t forward_end = sample_voice_reader_forward_end_frame(state);
        const uint8_t loop_forward =
            ((state->plan.loop_mode == 1U) && (forward_end > state->plan.loop_begin)) ? 1U : 0U;
        const uint8_t pingpong =
            ((state->plan.loop_mode == 2U) && (sample_voice_reader_pingpong_span_valid(state) != 0U)) ? 1U : 0U;
        uint8_t wrapped_loop = 0U;
        uint8_t bounced_pingpong = 0U;

        state->position += state->step * (float)consumed_frames;
        if (loop_forward != 0U)
        {
            const float loop_begin = (float)state->plan.loop_begin;
            const float loop_end = (float)forward_end;
            const float loop_length = loop_end - loop_begin;
            while ((loop_length > 0.0f) && (state->position >= loop_end))
            {
                state->position = loop_begin + (state->position - loop_end);
                wrapped_loop = 1U;
            }
            state->frame_pos = (uint32_t)state->position;
        }
        else if (pingpong != 0U)
        {
            const float loop_begin = (float)state->plan.loop_begin;
            const float loop_end = (float)forward_end;
            while (state->position >= loop_end)
            {
                state->position = ((2.0f * loop_end) - state->position) - 1.0f;
                state->plan.direction = 1U;
                state->plan.kernel_type = SAMPLE_KERNEL_PITCH_REV_LINEAR;
                if (state->cache_voice_valid != 0U)
                {
                    sample_cache_set_voice_direction(state->cache_voice_id, -1);
                }
                bounced_pingpong = 1U;
            }
            while (state->position < loop_begin)
            {
                state->position = (2.0f * loop_begin) - state->position;
                state->plan.direction = 0U;
                state->plan.kernel_type = SAMPLE_KERNEL_PITCH_FWD_LINEAR;
                if (state->cache_voice_valid != 0U)
                {
                    sample_cache_set_voice_direction(state->cache_voice_id, 1);
                }
                bounced_pingpong = 1U;
            }
            state->frame_pos = (uint32_t)state->position;
        }
        else if (state->position >= (float)state->plan.region_end)
        {
            state->position = (float)state->plan.region_end;
            state->frame_pos = state->plan.region_end;
            state->active = 0U;
        }
        else
        {
            state->frame_pos = (uint32_t)state->position;
        }

        if (state->cache_voice_valid != 0U)
        {
            sample_cache_update_voice_frame_pos(state->cache_voice_id, state->frame_pos);
        }
        if (state->active == 0U)
        {
            sample_voice_reader_release_audio_cursor(state);
            return;
        }

        if ((wrapped_loop != 0U) || (bounced_pingpong != 0U) || (state->audio_cursor.current_acquired == 0U)
            || (state->frame_pos < state->audio_cursor.current_start_frame)
            || (state->frame_pos
                >= (state->audio_cursor.current_start_frame + state->audio_cursor.current_frame_count)))
        {
            sample_voice_reader_release_audio_cursor(state);
            (void)sample_voice_reader_acquire_audio_page(state, state->frame_pos);
            return;
        }

        state->audio_cursor.current_offset_frames = state->frame_pos - state->audio_cursor.current_start_frame;
        return;
    }

    if (state->plan.kernel_type == SAMPLE_KERNEL_PITCH_REV_LINEAR)
    {
        const uint8_t pingpong =
            ((state->plan.loop_mode == 2U) && (sample_voice_reader_pingpong_span_valid(state) != 0U)) ? 1U : 0U;
        uint8_t bounced_pingpong = 0U;
        state->position -= state->step * (float)consumed_frames;
        if (pingpong != 0U)
        {
            const float loop_begin = (float)state->plan.region_begin;
            while (state->position < loop_begin)
            {
                state->position = (2.0f * loop_begin) - state->position;
                state->plan.direction = 0U;
                state->plan.kernel_type = SAMPLE_KERNEL_PITCH_FWD_LINEAR;
                if (state->cache_voice_valid != 0U)
                {
                    sample_cache_set_voice_direction(state->cache_voice_id, 1);
                }
                bounced_pingpong = 1U;
            }
            while (state->position >= (float)state->plan.region_end)
            {
                state->position = ((2.0f * (float)state->plan.region_end) - state->position) - 1.0f;
                state->plan.direction = 1U;
                state->plan.kernel_type = SAMPLE_KERNEL_PITCH_REV_LINEAR;
                if (state->cache_voice_valid != 0U)
                {
                    sample_cache_set_voice_direction(state->cache_voice_id, -1);
                }
                bounced_pingpong = 1U;
            }
            state->frame_pos = (uint32_t)state->position;
        }
        else if (state->position < (float)state->plan.region_begin)
        {
            state->position = (float)((int32_t)state->plan.region_begin - 1);
            state->frame_pos = state->plan.region_begin;
            state->active = 0U;
        }
        else
        {
            state->frame_pos = (uint32_t)state->position;
        }

        if (state->cache_voice_valid != 0U)
        {
            sample_cache_update_voice_frame_pos(state->cache_voice_id, state->frame_pos);
        }
        if (state->active == 0U)
        {
            sample_voice_reader_release_audio_cursor(state);
            return;
        }

        if ((bounced_pingpong != 0U) || (state->audio_cursor.current_acquired == 0U)
            || (state->frame_pos < state->audio_cursor.current_start_frame)
            || (state->frame_pos
                >= (state->audio_cursor.current_start_frame + state->audio_cursor.current_frame_count)))
        {
            sample_voice_reader_release_audio_cursor(state);
            (void)sample_voice_reader_acquire_audio_page(state, state->frame_pos);
            return;
        }

        state->audio_cursor.current_offset_frames = state->frame_pos - state->audio_cursor.current_start_frame;
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
                if (state->cache_voice_valid != 0U)
                {
                    sample_cache_set_voice_direction(state->cache_voice_id, 1);
                }
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
                if (state->cache_voice_valid != 0U)
                {
                    sample_cache_set_voice_direction(state->cache_voice_id, -1);
                }
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
            if (state->cache_voice_valid != 0U)
            {
                sample_cache_set_voice_direction(state->cache_voice_id, 1);
            }
        }
        if ((wrapped_loop == 0U) && (bounced_pingpong == 0U))
        {
            state->position = (float)state->frame_pos;
        }
    }
    if (state->cache_voice_valid != 0U)
    {
        sample_cache_update_voice_frame_pos(state->cache_voice_id, state->frame_pos);
    }

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
                                    uint32_t out_offset,
                                    float *out_last_l,
                                    float *out_last_r)
{
    if ((segment == 0) || (out_l == 0) || (out_r == 0) || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *src_l = segment->l;
    const float *src_r = (segment->is_mono != 0U) ? src_l : segment->r;
    float last_l = 0.0f;
    float last_r = 0.0f;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        const float sample_gain = gain * fade;
        last_l = src_l[i * segment->frame_stride] * sample_gain;
        last_r = src_r[i * segment->frame_stride] * sample_gain;
        out_l[out_offset + i] += last_l;
        out_r[out_offset + i] += last_r;
    }
    if ((segment->frames != 0U) && (out_last_l != 0) && (out_last_r != 0))
    {
        *out_last_l = last_l;
        *out_last_r = last_r;
    }
}

void sample_voice_reader_mix_rev_1x(const sample_audio_segment_t *segment,
                                    float gain,
                                    const float *fade_gain,
                                    uint32_t fade_count,
                                    float *out_l,
                                    float *out_r,
                                    uint32_t out_offset,
                                    float *out_last_l,
                                    float *out_last_r)
{
    if ((segment == 0) || (out_l == 0) || (out_r == 0) || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *src_l = segment->l;
    const float *src_r = (segment->is_mono != 0U) ? src_l : segment->r;
    float last_l = 0.0f;
    float last_r = 0.0f;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        const float sample_gain = gain * fade;
        last_l = src_l[-((int32_t)i * (int32_t)segment->frame_stride)] * sample_gain;
        last_r = src_r[-((int32_t)i * (int32_t)segment->frame_stride)] * sample_gain;
        out_l[out_offset + i] += last_l;
        out_r[out_offset + i] += last_r;
    }
    if ((segment->frames != 0U) && (out_last_l != 0) && (out_last_r != 0))
    {
        *out_last_l = last_l;
        *out_last_r = last_r;
    }
}

void sample_voice_reader_mix_fwd_1x_mono(const sample_audio_segment_t *segment,
                                         float gain,
                                         const float *fade_gain,
                                         uint32_t fade_count,
                                         float *out_mono,
                                         uint32_t out_offset,
                                         float *out_last)
{
    if ((segment == 0) || (out_mono == 0) || (segment->is_mono == 0U)
        || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    float last = 0.0f;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        last = segment->l[i * segment->frame_stride] * gain * fade;
        out_mono[out_offset + i] += last;
    }
    if ((segment->frames != 0U) && (out_last != 0))
    {
        *out_last = last;
    }
}

void sample_voice_reader_mix_rev_1x_mono(const sample_audio_segment_t *segment,
                                         float gain,
                                         const float *fade_gain,
                                         uint32_t fade_count,
                                         float *out_mono,
                                         uint32_t out_offset,
                                         float *out_last)
{
    if ((segment == 0) || (out_mono == 0) || (segment->is_mono == 0U)
        || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    float last = 0.0f;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        last = segment->l[-((int32_t)i * (int32_t)segment->frame_stride)] * gain * fade;
        out_mono[out_offset + i] += last;
    }
    if ((segment->frames != 0U) && (out_last != 0))
    {
        *out_last = last;
    }
}

void sample_voice_reader_mix_pitch_fwd_linear(const sample_audio_segment_t *segment,
                                              float gain,
                                              const float *fade_gain,
                                              uint32_t fade_count,
                                              float *out_l,
                                              float *out_r,
                                              uint32_t out_offset,
                                              float *out_last_l,
                                              float *out_last_r)
{
    if ((segment == 0) || (out_l == 0) || (out_r == 0) || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *const src = segment->l;
    const uint32_t frames = segment->frames;
    const uint32_t source_start_frame = segment->source_start_frame;
    const uint32_t source_limit_frame = segment->source_limit_frame;
    const uint32_t source_end_frame = source_start_frame + segment->source_frame_count;
    const uint32_t frame_stride = segment->frame_stride;
    const uint32_t right_offset = (segment->is_mono != 0U) ? 0U : 1U;
    const float gain_base = gain;
    uint32_t pos_q16 =
        (uint32_t)(((segment->source_position - (float)source_start_frame)
                    * (float)SAMPLE_Q16_ONE) + 0.5f);
    uint32_t step_q16 = (uint32_t)((segment->source_step * (float)SAMPLE_Q16_ONE) + 0.5f);
    if (step_q16 == 0U)
    {
        step_q16 = 1U;
    }
    float *dst_l = &out_l[out_offset];
    float *dst_r = &out_r[out_offset];
    const uint8_t has_fade = ((fade_gain != 0) && (fade_count != 0U)) ? 1U : 0U;
    float last_l = 0.0f;
    float last_r = 0.0f;

    if (frames == 0U)
    {
        return;
    }

    uint32_t same_page_frames = frames;
    const uint32_t same_page_limit_frame = (source_end_frame < source_limit_frame)
                                               ? source_end_frame
                                               : source_limit_frame;
    if (same_page_limit_frame <= (source_start_frame + 1U))
    {
        same_page_frames = 0U;
    }
    else
    {
        const uint32_t first_boundary_frame = same_page_limit_frame - 1U;
        const uint64_t first_boundary_q16 =
            ((uint64_t)(first_boundary_frame - source_start_frame)) << 16U;
        if ((uint64_t)pos_q16 >= first_boundary_q16)
        {
            same_page_frames = 0U;
        }
        else
        {
            const uint64_t distance_q16 = first_boundary_q16 - (uint64_t)pos_q16;
            const uint64_t frames_before_boundary = ((distance_q16 - 1ULL) / step_q16) + 1ULL;
            if (frames_before_boundary < (uint64_t)same_page_frames)
            {
                same_page_frames = (uint32_t)frames_before_boundary;
            }
        }
    }

    for (uint32_t i = 0U; i < same_page_frames; ++i)
    {
        const uint32_t base_offset = pos_q16 >> 16U;
        const uint32_t src0 = base_offset * frame_stride;
        const float curr_l = src[src0];
        const float curr_r = src[src0 + right_offset];
        const uint32_t frac_q16 = pos_q16 & (SAMPLE_Q16_ONE - 1U);
        float sample_l = curr_l;
        float sample_r = curr_r;
        if (frac_q16 != 0U)
        {
            const float frac = (float)frac_q16 * (1.0f / (float)SAMPLE_Q16_ONE);
            const float next_l = src[src0 + frame_stride];
            sample_l = curr_l + ((next_l - curr_l) * frac);
            if (right_offset != 0U)
            {
                const float next_r = src[src0 + frame_stride + right_offset];
                sample_r = curr_r + ((next_r - curr_r) * frac);
            }
            else
            {
                sample_r = sample_l;
            }
        }

        const float sample_gain = (has_fade != 0U) ? (gain_base * fade_gain[i]) : gain_base;
        last_l = sample_l * sample_gain;
        last_r = sample_r * sample_gain;
        dst_l[i] += last_l;
        dst_r[i] += last_r;
        pos_q16 += step_q16;
    }

    for (uint32_t i = same_page_frames; i < frames; ++i)
    {
        const uint32_t base_offset = pos_q16 >> 16U;
        const uint32_t base_frame = source_start_frame + base_offset;
        const uint32_t src0 = base_offset * frame_stride;
        const float curr_l = src[src0];
        const float curr_r = src[src0 + right_offset];
        const uint32_t frac_q16 = pos_q16 & (SAMPLE_Q16_ONE - 1U);
        float sample_l = curr_l;
        float sample_r = curr_r;

        if (frac_q16 != 0U)
        {
            uint32_t next_frame = base_frame + 1U;
            if (next_frame >= source_limit_frame)
            {
                next_frame = (segment->neighbor_l != 0) ? segment->source_region_begin : UINT32_MAX;
            }

            const float *next_l_ptr = 0;
            const float *next_r_ptr = 0;
            if ((next_frame >= source_start_frame) && (next_frame < source_end_frame))
            {
                next_l_ptr = &src[(next_frame - source_start_frame) * frame_stride];
                next_r_ptr = &next_l_ptr[right_offset];
            }
            else if ((segment->neighbor_l != 0) && (next_frame >= segment->neighbor_start_frame)
                     && (next_frame < (segment->neighbor_start_frame + segment->neighbor_frame_count)))
            {
                next_l_ptr = &segment->neighbor_l[(next_frame - segment->neighbor_start_frame) * frame_stride];
                next_r_ptr = &segment->neighbor_r[(next_frame - segment->neighbor_start_frame) * frame_stride];
            }

            if ((next_l_ptr != 0) && (next_r_ptr != 0))
            {
                const float frac = (float)frac_q16 * (1.0f / (float)SAMPLE_Q16_ONE);
                sample_l = curr_l + ((*next_l_ptr - curr_l) * frac);
                sample_r = curr_r + ((*next_r_ptr - curr_r) * frac);
            }
        }

        const float sample_gain = (has_fade != 0U) ? (gain_base * fade_gain[i]) : gain_base;
        last_l = sample_l * sample_gain;
        last_r = sample_r * sample_gain;
        dst_l[i] += last_l;
        dst_r[i] += last_r;
        pos_q16 += step_q16;
    }
    if ((frames != 0U) && (out_last_l != 0) && (out_last_r != 0))
    {
        *out_last_l = last_l;
        *out_last_r = last_r;
    }
}

void sample_voice_reader_mix_pitch_rev_linear(const sample_audio_segment_t *segment,
                                              float gain,
                                              const float *fade_gain,
                                              uint32_t fade_count,
                                              float *out_l,
                                              float *out_r,
                                              uint32_t out_offset,
                                              float *out_last_l,
                                              float *out_last_r)
{
    if ((segment == 0) || (out_l == 0) || (out_r == 0) || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *const src = segment->l;
    const uint32_t frames = segment->frames;
    const uint32_t source_start_frame = segment->source_start_frame;
    const uint32_t source_region_begin = segment->source_region_begin;
    const uint32_t frame_stride = segment->frame_stride;
    const uint32_t right_offset = (segment->is_mono != 0U) ? 0U : 1U;
    const float gain_base = gain;
    uint32_t pos_q16 = (uint32_t)(((segment->source_position - (float)source_start_frame) * 65536.0f) + 0.5f);
    const uint32_t step_q16 = (uint32_t)(segment->source_step * 65536.0f + 0.5f);
    float *dst_l = &out_l[out_offset];
    float *dst_r = &out_r[out_offset];
    const uint8_t has_fade = ((fade_gain != 0) && (fade_count != 0U)) ? 1U : 0U;
    float last_l = 0.0f;
    float last_r = 0.0f;

    if (frames == 0U)
    {
        return;
    }

    const uint64_t reverse_span_q16 = (uint64_t)step_q16 * (uint64_t)(frames - 1U);
    const uint32_t end_pos_q16 = (reverse_span_q16 < (uint64_t)pos_q16)
                                     ? (pos_q16 - (uint32_t)reverse_span_q16)
                                     : 0U;
    const uint32_t min_base_offset = end_pos_q16 >> 16U;
    const uint8_t same_span =
        ((min_base_offset > 0U) && ((source_start_frame + min_base_offset) > source_region_begin)) ? 1U : 0U;

    if (same_span != 0U)
    {
        for (uint32_t i = 0U; i < frames; ++i)
        {
            const uint32_t base_offset = pos_q16 >> 16U;
            const uint32_t src0 = base_offset * frame_stride;
            const float sample_l = src[src0];
            const float sample_r = src[src0 + right_offset];
            const float sample_gain = (has_fade != 0U) ? (gain_base * fade_gain[i]) : gain_base;
            last_l = sample_l * sample_gain;
            last_r = sample_r * sample_gain;
            dst_l[i] += last_l;
            dst_r[i] += last_r;
            pos_q16 -= step_q16;
        }
    }
    else
    {
        for (uint32_t i = 0U; i < frames; ++i)
        {
            const uint32_t base_offset = pos_q16 >> 16U;
            const float *read_ptr = &src[base_offset * frame_stride];

            const float sample_l = read_ptr[0];
            const float sample_r = read_ptr[right_offset];
            const float sample_gain = (has_fade != 0U) ? (gain_base * fade_gain[i]) : gain_base;
            last_l = sample_l * sample_gain;
            last_r = sample_r * sample_gain;
            dst_l[i] += last_l;
            dst_r[i] += last_r;
            pos_q16 -= step_q16;
        }
    }
    if ((frames != 0U) && (out_last_l != 0) && (out_last_r != 0))
    {
        *out_last_l = last_l;
        *out_last_r = last_r;
    }
}

void sample_voice_reader_mix_pitch_fwd_linear_mono(const sample_audio_segment_t *segment,
                                                   float gain,
                                                   const float *fade_gain,
                                                   uint32_t fade_count,
                                                   float *out_mono,
                                                   uint32_t out_offset,
                                                   float *out_last)
{
    if ((segment == 0) || (out_mono == 0) || (segment->is_mono == 0U)
        || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *const src = segment->l;
    const uint32_t source_start_frame = segment->source_start_frame;
    const uint32_t source_end_frame = source_start_frame + segment->source_frame_count;
    const uint32_t frame_stride = segment->frame_stride;
    uint32_t pos_q16 = (uint32_t)(((segment->source_position - (float)source_start_frame)
                                  * (float)SAMPLE_Q16_ONE) + 0.5f);
    uint32_t step_q16 = (uint32_t)((segment->source_step * (float)SAMPLE_Q16_ONE) + 0.5f);
    if (step_q16 == 0U)
    {
        step_q16 = 1U;
    }

    float last = 0.0f;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const uint32_t base_offset = pos_q16 >> 16U;
        const uint32_t base_frame = source_start_frame + base_offset;
        const uint32_t src0 = base_offset * frame_stride;
        const float current = src[src0];
        float sample = current;
        const uint32_t frac_q16 = pos_q16 & (SAMPLE_Q16_ONE - 1U);
        if (frac_q16 != 0U)
        {
            uint32_t next_frame = base_frame + 1U;
            if (next_frame >= segment->source_limit_frame)
            {
                next_frame = (segment->neighbor_l != 0) ? segment->source_region_begin : UINT32_MAX;
            }

            const float *next = 0;
            if ((next_frame >= source_start_frame) && (next_frame < source_end_frame))
            {
                next = &src[(next_frame - source_start_frame) * frame_stride];
            }
            else if ((segment->neighbor_l != 0) && (next_frame >= segment->neighbor_start_frame)
                     && (next_frame < (segment->neighbor_start_frame + segment->neighbor_frame_count)))
            {
                next = &segment->neighbor_l[(next_frame - segment->neighbor_start_frame) * frame_stride];
            }
            if (next != 0)
            {
                const float frac = (float)frac_q16 * (1.0f / (float)SAMPLE_Q16_ONE);
                sample = current + ((*next - current) * frac);
            }
        }

        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        last = sample * gain * fade;
        out_mono[out_offset + i] += last;
        pos_q16 += step_q16;
    }
    if ((segment->frames != 0U) && (out_last != 0))
    {
        *out_last = last;
    }
}

void sample_voice_reader_mix_pitch_rev_linear_mono(const sample_audio_segment_t *segment,
                                                   float gain,
                                                   const float *fade_gain,
                                                   uint32_t fade_count,
                                                   float *out_mono,
                                                   uint32_t out_offset,
                                                   float *out_last)
{
    if ((segment == 0) || (out_mono == 0) || (segment->is_mono == 0U)
        || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    const float *const src = segment->l;
    const uint32_t frame_stride = segment->frame_stride;
    uint32_t pos_q16 = (uint32_t)(((segment->source_position - (float)segment->source_start_frame)
                                  * (float)SAMPLE_Q16_ONE) + 0.5f);
    uint32_t step_q16 = (uint32_t)((segment->source_step * (float)SAMPLE_Q16_ONE) + 0.5f);
    if (step_q16 == 0U)
    {
        step_q16 = 1U;
    }

    float last = 0.0f;
    for (uint32_t i = 0U; i < segment->frames; ++i)
    {
        const uint32_t base_offset = pos_q16 >> 16U;
        const float fade = ((fade_gain != 0) && (i < fade_count)) ? fade_gain[i] : 1.0f;
        last = src[base_offset * frame_stride] * gain * fade;
        out_mono[out_offset + i] += last;
        pos_q16 -= step_q16;
    }
    if ((segment->frames != 0U) && (out_last != 0))
    {
        *out_last = last;
    }
}

uint8_t sample_voice_reader_render_fwd_1x_ready_simple(sample_voice_reader_t *reader,
                                                       float gain,
                                                       float *out_l,
                                                       float *out_r,
                                                       uint32_t frames,
                                                       uint32_t out_offset,
                                                       uint32_t *out_rendered,
                                                       float *out_last_l,
                                                       float *out_last_r)
{
    if (out_rendered != 0)
    {
        *out_rendered = 0U;
    }
    if ((reader == 0) || (out_l == 0) || (out_r == 0) || (frames == 0U)
        || (reader->active == 0U))
    {
        return 0U;
    }

    sample_voice_reader_state_t *const state = sample_voice_reader_state(reader);
    if ((state->plan_valid == 0U)
        || (state->plan.kernel_type != SAMPLE_KERNEL_FWD_1X)
        || (state->plan.direction != 0U)
        || (state->plan.loop_mode != SAMPLE_PLAY_LOOP_NONE)
        || (state->plan.step_q16 != SAMPLE_Q16_ONE))
    {
        return 0U;
    }

    const uint32_t forward_end = sample_voice_reader_forward_end_frame(state);
    if (state->frame_pos >= forward_end)
    {
        state->position = (float)forward_end;
        state->active = 0U;
        sample_voice_reader_release_audio_cursor(state);
        return 0U;
    }

    if (state->audio_cursor.current_acquired == 0U)
    {
        if (sample_voice_reader_acquire_audio_page(state, state->frame_pos) == 0U)
        {
            return 0U;
        }
    }

    if ((state->frame_pos < state->audio_cursor.current_start_frame)
        || (state->frame_pos
            >= (state->audio_cursor.current_start_frame + state->audio_cursor.current_frame_count)))
    {
        sample_voice_reader_release_audio_cursor(state);
        if (sample_voice_reader_acquire_audio_page(state, state->frame_pos) == 0U)
        {
            return 0U;
        }
    }

    const uint32_t offset_frames = state->frame_pos - state->audio_cursor.current_start_frame;
    uint32_t todo = state->audio_cursor.current_frame_count - offset_frames;
    const uint32_t region_remaining = forward_end - state->frame_pos;
    if (todo > region_remaining)
    {
        todo = region_remaining;
    }
    if (todo > frames)
    {
        todo = frames;
    }
    if (todo == 0U)
    {
        return 0U;
    }

    const uint32_t frame_stride = state->audio_cursor.stride_floats;
    const uint32_t right_offset = (state->audio_cursor.format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO) ? 0U : 1U;
    const float *src = &state->audio_cursor.current_base[offset_frames * frame_stride];
    float *dst_l = &out_l[out_offset];
    float *dst_r = &out_r[out_offset];
    float last_l = 0.0f;
    float last_r = 0.0f;
    for (uint32_t i = 0U; i < todo; ++i)
    {
        last_l = src[i * frame_stride] * gain;
        last_r = src[(i * frame_stride) + right_offset] * gain;
        dst_l[i] += last_l;
        dst_r[i] += last_r;
    }

    state->frame_pos += todo;
    state->position = (float)state->frame_pos;
    if (state->cache_voice_valid != 0U)
    {
        sample_cache_update_voice_frame_pos(state->cache_voice_id, state->frame_pos);
    }
    state->audio_cursor.current_offset_frames = state->frame_pos - state->audio_cursor.current_start_frame;

    if (state->frame_pos >= forward_end)
    {
        state->active = 0U;
        sample_voice_reader_release_audio_cursor(state);
    }
    else if (state->audio_cursor.current_offset_frames >= state->audio_cursor.current_frame_count)
    {
        sample_voice_reader_release_audio_cursor(state);
    }

    if (out_rendered != 0)
    {
        *out_rendered = todo;
    }
    if ((out_last_l != 0) && (out_last_r != 0))
    {
        *out_last_l = last_l;
        *out_last_r = last_r;
    }
    return 1U;
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
                                                  uint8_t *out_underrun,
                                                  float *out_last_l,
                                                  float *out_last_r)
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
    float last_l = 0.0f;
    float last_r = 0.0f;

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
            (void)sample_cache_try_acquire_span(reader->sample_id, neighbor_frame_index, 1U, &neighbor_span);
        }

        float position = reader->position;
        uint8_t segment_reverse = reverse;
        const float *const span_l = span.l;
        const float *const span_r = (span.is_mono != 0U) ? span_l : span.r;
        const uint32_t span_stride = span.frame_stride;
        for (uint32_t i = 0U; i < segment_frames; ++i)
        {
            const uint32_t segment_base_frame = (uint32_t)position;
            const uint32_t source_offset = (segment_base_frame - span.start_frame) * span_stride;
            const float sample_l = span_l[source_offset];
            const float sample_r = span_r[source_offset];

            const float fade =
                (fade_gain != 0) && ((produced + i) < fade_count) ? fade_gain[produced + i] : 1.0f;
            last_l = sample_l * gain * fade;
            last_r = sample_r * gain * fade;
            out_l[produced + i] += last_l;
            out_r[produced + i] += last_r;

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
        if (reader->cache_voice_valid != 0U)
        {
            sample_cache_update_voice_frame_pos(reader->cache_voice_id, reader->frame_pos);
        }
        produced += segment_frames;
    }

    if (io_reverse != 0)
    {
        *io_reverse = reverse;
    }
    if ((produced != 0U) && (out_last_l != 0) && (out_last_r != 0))
    {
        *out_last_l = last_l;
        *out_last_r = last_r;
    }

    return produced;
}

uint8_t sample_voice_reader_render_fwd_1x_ready_simple_mono(sample_voice_reader_t *reader,
                                                            float gain,
                                                            float *out_mono,
                                                            uint32_t frames,
                                                            uint32_t out_offset,
                                                            uint32_t *out_rendered,
                                                            float *out_last)
{
    if (out_rendered != 0)
    {
        *out_rendered = 0U;
    }
    if ((reader == 0) || (out_mono == 0) || (frames == 0U) || (reader->active == 0U))
    {
        return 0U;
    }

    sample_voice_reader_state_t *const state = sample_voice_reader_state(reader);
    if ((state->plan_valid == 0U)
        || (state->plan.format != SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
        || (state->plan.kernel_type != SAMPLE_KERNEL_FWD_1X)
        || (state->plan.direction != 0U)
        || (state->plan.loop_mode != SAMPLE_PLAY_LOOP_NONE)
        || (state->plan.step_q16 != SAMPLE_Q16_ONE))
    {
        return 0U;
    }

    const uint32_t forward_end = sample_voice_reader_forward_end_frame(state);
    if (state->frame_pos >= forward_end)
    {
        state->position = (float)forward_end;
        state->active = 0U;
        sample_voice_reader_release_audio_cursor(state);
        return 0U;
    }

    if ((state->audio_cursor.current_acquired == 0U)
        && (sample_voice_reader_acquire_audio_page(state, state->frame_pos) == 0U))
    {
        return 0U;
    }
    if ((state->frame_pos < state->audio_cursor.current_start_frame)
        || (state->frame_pos
            >= (state->audio_cursor.current_start_frame + state->audio_cursor.current_frame_count)))
    {
        sample_voice_reader_release_audio_cursor(state);
        if (sample_voice_reader_acquire_audio_page(state, state->frame_pos) == 0U)
        {
            return 0U;
        }
    }

    const uint32_t offset_frames = state->frame_pos - state->audio_cursor.current_start_frame;
    uint32_t todo = state->audio_cursor.current_frame_count - offset_frames;
    const uint32_t region_remaining = forward_end - state->frame_pos;
    if (todo > region_remaining)
    {
        todo = region_remaining;
    }
    if (todo > frames)
    {
        todo = frames;
    }
    if (todo == 0U)
    {
        return 0U;
    }

    const uint32_t frame_stride = state->audio_cursor.stride_floats;
    const float *src = &state->audio_cursor.current_base[offset_frames * frame_stride];
    float *dst = &out_mono[out_offset];
    float last = 0.0f;
    for (uint32_t i = 0U; i < todo; ++i)
    {
        last = src[i * frame_stride] * gain;
        dst[i] += last;
    }

    state->frame_pos += todo;
    state->position = (float)state->frame_pos;
    if (state->cache_voice_valid != 0U)
    {
        sample_cache_update_voice_frame_pos(state->cache_voice_id, state->frame_pos);
    }
    state->audio_cursor.current_offset_frames = state->frame_pos - state->audio_cursor.current_start_frame;
    if (state->frame_pos >= forward_end)
    {
        state->active = 0U;
        sample_voice_reader_release_audio_cursor(state);
    }
    else if (state->audio_cursor.current_offset_frames >= state->audio_cursor.current_frame_count)
    {
        sample_voice_reader_release_audio_cursor(state);
    }

    if (out_rendered != 0)
    {
        *out_rendered = todo;
    }
    if (out_last != 0)
    {
        *out_last = last;
    }
    return 1U;
}
