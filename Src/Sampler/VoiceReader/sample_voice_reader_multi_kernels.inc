static uint8_t sample_voice_reader_prepare_multi_fwd_1x(
    sample_voice_reader_state_t *state,
    sample_audio_format_t expected_format,
    uint32_t frames,
    const float **out_source,
    uint32_t *out_frames,
    sample_audio_segment_status_t *out_status)
{
    *out_source = 0;
    *out_frames = 0U;
    *out_status = SAMPLE_AUDIO_SEGMENT_NOT_READY;
    if ((state == 0) || (frames == 0U) || (state->active == 0U)
        || (state->plan_valid == 0U)
        || (state->plan.kernel_type != SAMPLE_KERNEL_FWD_1X)
        || (state->plan.direction != 0U)
        || (state->plan.step_q16 != SAMPLE_Q16_ONE)
        || (state->format != expected_format))
    {
        return 0U;
    }

    const uint32_t forward_end = sample_voice_reader_forward_end_frame(state);
    const uint8_t loop_forward =
        ((state->plan.loop_mode == SAMPLE_PLAY_LOOP_FORWARD)
         && (forward_end > state->plan.loop_begin)) ? 1U : 0U;
    if (state->frame_pos >= forward_end)
    {
        if (loop_forward != 0U)
        {
            state->frame_pos = state->plan.loop_begin;
            state->position = (float)state->frame_pos;
            sample_voice_reader_release_audio_cursor(state);
        }
        else
        {
            state->frame_pos = forward_end;
            state->position = (float)forward_end;
            state->active = 0U;
            sample_voice_reader_release_audio_cursor(state);
            *out_status = SAMPLE_AUDIO_SEGMENT_DONE;
            return 1U;
        }
    }

    if (state->audio_cursor.current_acquired == 0U)
    {
        if (sample_voice_reader_acquire_audio_page(state, state->frame_pos) == 0U)
        {
            *out_status = SAMPLE_AUDIO_SEGMENT_UNDERRUN;
            return 1U;
        }
    }
    if ((state->frame_pos < state->audio_cursor.current_start_frame)
        || (state->frame_pos
            >= (state->audio_cursor.current_start_frame
                + state->audio_cursor.current_frame_count)))
    {
        sample_voice_reader_release_audio_cursor(state);
        if (sample_voice_reader_acquire_audio_page(state, state->frame_pos) == 0U)
        {
            *out_status = SAMPLE_AUDIO_SEGMENT_UNDERRUN;
            return 1U;
        }
    }

    const uint32_t offset = state->frame_pos - state->audio_cursor.current_start_frame;
    uint32_t todo = state->audio_cursor.current_frame_count - offset;
    const uint32_t boundary_remaining = forward_end - state->frame_pos;
    if (todo > boundary_remaining)
    {
        todo = boundary_remaining;
    }
    if (todo > frames)
    {
        todo = frames;
    }
    if (todo == 0U)
    {
        *out_status = SAMPLE_AUDIO_SEGMENT_UNDERRUN;
        return 1U;
    }
    *out_source = &state->audio_cursor.current_base[
        offset * state->audio_cursor.stride_floats];
    *out_frames = todo;
    *out_status = SAMPLE_AUDIO_SEGMENT_OK;
    return 1U;
}

static void sample_voice_reader_commit_multi_fwd_1x(sample_voice_reader_state_t *state,
                                                     uint32_t rendered)
{
    const uint32_t forward_end = sample_voice_reader_forward_end_frame(state);
    state->frame_pos += rendered;
    state->position = (float)state->frame_pos;
    if ((state->plan.loop_mode == SAMPLE_PLAY_LOOP_FORWARD)
        && (forward_end > state->plan.loop_begin)
        && (state->frame_pos >= forward_end))
    {
        state->frame_pos = state->plan.loop_begin;
        state->position = (float)state->frame_pos;
        sample_voice_reader_release_audio_cursor(state);
        return;
    }
    if (state->frame_pos >= forward_end)
    {
        state->active = 0U;
        sample_voice_reader_release_audio_cursor(state);
        return;
    }
    state->audio_cursor.current_offset_frames =
        state->frame_pos - state->audio_cursor.current_start_frame;
    if (state->audio_cursor.current_offset_frames
        >= state->audio_cursor.current_frame_count)
    {
        sample_voice_reader_release_audio_cursor(state);
    }
}

uint8_t sample_voice_reader_render_multi_fwd_1x_stereo(
    sample_voice_reader_t *reader,
    float gain,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint32_t out_offset,
    uint32_t *out_rendered,
    sample_audio_segment_status_t *out_status,
    float *out_last_l,
    float *out_last_r)
{
    if ((reader == 0) || (out_l == 0) || (out_r == 0)
        || (out_rendered == 0) || (out_status == 0))
    {
        return 0U;
    }
    const float *source = 0;
    uint32_t todo = 0U;
    sample_voice_reader_state_t *const state = sample_voice_reader_state(reader);
    if (sample_voice_reader_prepare_multi_fwd_1x(
            state,
            SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED,
            frames,
            &source,
            &todo,
            out_status) == 0U)
    {
        return 0U;
    }
    *out_rendered = todo;
    if (*out_status != SAMPLE_AUDIO_SEGMENT_OK)
    {
        return 1U;
    }
    float last_l = 0.0f;
    float last_r = 0.0f;
    float *const dst_l = &out_l[out_offset];
    float *const dst_r = &out_r[out_offset];
    for (uint32_t i = 0U; i < todo; ++i)
    {
        last_l = source[2U * i] * gain;
        last_r = source[(2U * i) + 1U] * gain;
        dst_l[i] += last_l;
        dst_r[i] += last_r;
    }
    if (out_last_l != 0) *out_last_l = last_l;
    if (out_last_r != 0) *out_last_r = last_r;
    sample_voice_reader_commit_multi_fwd_1x(state, todo);
    return 1U;
}

uint8_t sample_voice_reader_render_multi_fwd_1x_mono(
    sample_voice_reader_t *reader,
    float gain,
    float *out_mono,
    uint32_t frames,
    uint32_t out_offset,
    uint32_t *out_rendered,
    sample_audio_segment_status_t *out_status,
    float *out_last)
{
    if ((reader == 0) || (out_mono == 0) || (out_rendered == 0)
        || (out_status == 0))
    {
        return 0U;
    }
    const float *source = 0;
    uint32_t todo = 0U;
    sample_voice_reader_state_t *const state = sample_voice_reader_state(reader);
    if (sample_voice_reader_prepare_multi_fwd_1x(
            state,
            SAMPLE_AUDIO_FORMAT_FLOAT32_MONO,
            frames,
            &source,
            &todo,
            out_status) == 0U)
    {
        return 0U;
    }
    *out_rendered = todo;
    if (*out_status != SAMPLE_AUDIO_SEGMENT_OK)
    {
        return 1U;
    }
    float last = 0.0f;
    float *const dst = &out_mono[out_offset];
    for (uint32_t i = 0U; i < todo; ++i)
    {
        last = source[i] * gain;
        dst[i] += last;
    }
    if (out_last != 0) *out_last = last;
    sample_voice_reader_commit_multi_fwd_1x(state, todo);
    return 1U;
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
