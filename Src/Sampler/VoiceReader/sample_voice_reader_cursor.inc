static sample_voice_reader_state_t *sample_voice_reader_state(sample_voice_reader_t *reader)
{
    return (sample_voice_reader_state_t *)reader;
}

static uint8_t sample_voice_reader_lease_slot(sample_audio_key_t key,
                                               uint8_t reader_id)
{
    if (key.domain == SAMPLE_AUDIO_DOMAIN_CLASSIC)
        return sample_page_lease_classic_slot(reader_id);
    if (key.domain == SAMPLE_AUDIO_DOMAIN_MULTI)
        return sample_page_lease_multi_slot(reader_id);
    if (key.domain == SAMPLE_AUDIO_DOMAIN_REC)
        return sample_page_lease_rec_slot(reader_id);
    return UINT8_MAX;
}

static void sample_voice_reader_publish_lease(sample_voice_reader_state_t *state,
                                               uint32_t frame_pos)
{
    if ((state == 0) || (state->lease_valid == 0U)
        || (state->frames_per_page == 0U)) return;
    const uint8_t bench_probe = stream_end_to_end_bench_probe_key(state->key);
    const uint32_t bench_need_start = (bench_probe != 0U)
        ? DWT->CYCCNT : 0U;
    PERF_START(need_start);

    const uint32_t page = frame_pos / state->frames_per_page;
    uint32_t pages[SAMPLE_PAGE_LEASE_PAGE_COUNT] = {0};
    uint8_t valid_mask = 0U;
    const uint32_t window_end = (state->plan_valid != 0U
                                 && state->plan.loop_mode == SAMPLE_PLAY_LOOP_FORWARD
                                 && state->plan.loop_end > state->plan.loop_begin)
                                    ? state->plan.loop_end
                                    : state->plan.region_end;
    const uint32_t last_page = (window_end != 0U)
                                   ? ((window_end - 1U)
                                      / state->frames_per_page)
                                   : page;
    pages[SAMPLE_PAGE_LEASE_CURRENT] = page;
    valid_mask |= SAMPLE_PAGE_LEASE_VALID(SAMPLE_PAGE_LEASE_CURRENT);
    if (page < last_page)
    {
        pages[SAMPLE_PAGE_LEASE_NEXT] = page + 1U;
        valid_mask |= SAMPLE_PAGE_LEASE_VALID(SAMPLE_PAGE_LEASE_NEXT);
    }
    if ((state->plan_valid != 0U)
        && (state->plan.loop_mode == SAMPLE_PLAY_LOOP_FORWARD)
        && (state->plan.loop_end > state->plan.loop_begin))
    {
        const uint32_t loop_page = state->plan.loop_begin / state->frames_per_page;
        const uint32_t last = (state->plan.loop_end - 1U) / state->frames_per_page;
        pages[SAMPLE_PAGE_LEASE_LOOP_START] = loop_page;
        valid_mask |= SAMPLE_PAGE_LEASE_VALID(SAMPLE_PAGE_LEASE_LOOP_START);
        if (loop_page < last)
        {
            pages[SAMPLE_PAGE_LEASE_LOOP_START_NEXT] = loop_page + 1U;
            valid_mask |= SAMPLE_PAGE_LEASE_VALID(SAMPLE_PAGE_LEASE_LOOP_START_NEXT);
        }
    }
    PERF_END(PERF_CPU_READER_NEED, need_start);
    if ((state->lease_valid_mask != valid_mask)
        || (memcmp(state->lease_pages, pages, sizeof(pages)) != 0))
    {
        (void)sample_page_lease_audio_publish(state->lease_slot,
                                              state->key,
                                              state->registration_epoch,
                                              pages,
                                              valid_mask);
        memcpy(state->lease_pages, pages, sizeof(pages));
        state->lease_valid_mask = valid_mask;
    }
    if (bench_probe != 0U)
        stream_end_to_end_bench_probe_need(state->key, page,
                                           bench_need_start, DWT->CYCCNT);
}

static void sample_voice_reader_release_audio_cursor(sample_voice_reader_state_t *state)
{
    if (state == 0)
    {
        return;
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

    sample_voice_reader_publish_lease(state, frame_pos);
    const uint8_t bench_probe = stream_end_to_end_bench_probe_key(state->key);
    const uint32_t bench_resolve_start = (bench_probe != 0U)
        ? DWT->CYCCNT : 0U;
    PERF_START(resolve_start);
    const sample_audio_format_t expected_format = sample_audio_format_or_stereo(state->format);
    sample_page_span_t span;
    const uint32_t page_index =
        sample_audio_format_page_index_from_frame(expected_format, frame_pos);
    const uint8_t acquired = sample_page_cache_audio_resolve_page_key(
        state->key, page_index, &span);
    if (bench_probe != 0U)
        stream_end_to_end_bench_probe_resolve(
            state->key, page_index, bench_resolve_start,
            DWT->CYCCNT, acquired);
    if (acquired == 0U)
    {
        PERF_COUNT(PERF_N_AUDIO_PAGE_MISSING);
        PERF_END(PERF_CPU_READER_RESOLVE, resolve_start);
        return 0U;
    }
    PERF_END(PERF_CPU_READER_RESOLVE, resolve_start);
    PERF_COUNT(PERF_N_CACHE_HIT);

    if (sample_audio_format_or_stereo(span.format) != expected_format)
    {
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

    sample_voice_reader_publish_lease(state, page_index * state->frames_per_page);
    if ((state->audio_cursor.neighbor_acquired != 0U)
        && (state->audio_cursor.neighbor_page_ref.page_index == page_index))
    {
        return 1U;
    }

    if (state->audio_cursor.neighbor_acquired != 0U)
    {
        state->audio_cursor.neighbor_acquired = 0U;
        state->audio_cursor.neighbor_base = 0;
        state->audio_cursor.neighbor_start_frame = 0U;
        state->audio_cursor.neighbor_frame_count = 0U;
    }

    sample_page_span_t span;
    if (sample_page_cache_audio_resolve_page_key(state->key, page_index, &span) == 0U)
    {
        return 0U;
    }

    if (sample_audio_format_or_stereo(span.format) != state->audio_cursor.format)
    {
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

static uint32_t sample_voice_reader_forward_frames_before_boundary_q16(
    float position,
    float step,
    uint32_t source_start_frame,
    uint32_t limit_frame,
    uint32_t max_frames,
    uint32_t *out_position_q16,
    uint32_t *out_step_q16)
{
    const uint32_t position_q16 =
        (uint32_t)(((position - (float)source_start_frame)
                    * (float)SAMPLE_Q16_ONE) + 0.5f);
    uint32_t step_q16 =
        (uint32_t)((step * (float)SAMPLE_Q16_ONE) + 0.5f);
    if (step_q16 == 0U)
    {
        step_q16 = 1U;
    }
    *out_position_q16 = position_q16;
    *out_step_q16 = step_q16;
    if ((limit_frame <= source_start_frame) || (max_frames == 0U))
    {
        return 0U;
    }

    const uint64_t limit_q16 =
        ((uint64_t)(limit_frame - source_start_frame)) << 16U;
    if ((uint64_t)position_q16 >= limit_q16)
    {
        return 0U;
    }
    /* Positions are pos + n*step and must stay strictly below limit. */
    const uint64_t distance_q16 = limit_q16 - (uint64_t)position_q16;
    uint64_t frames =
        (distance_q16 + (uint64_t)step_q16 - 1ULL) / (uint64_t)step_q16;
    if (frames > (uint64_t)max_frames)
    {
        frames = max_frames;
    }
    return (uint32_t)frames;
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

    const uint32_t current_page_end =
        state->audio_cursor.current_start_frame
        + state->audio_cursor.current_frame_count;
    const uint32_t segment_limit =
        (forward_end < current_page_end) ? forward_end : current_page_end;
    uint32_t position_q16 = 0U;
    uint32_t step_q16 = 0U;
    const uint32_t segment_frames =
        sample_voice_reader_forward_frames_before_boundary_q16(
            state->position,
            state->step,
            state->audio_cursor.current_start_frame,
            segment_limit,
            max_frames,
            &position_q16,
            &step_q16);
    uint8_t needs_neighbor = 0U;
    uint32_t neighbor_page_index = UINT32_MAX;

    if (segment_frames == 0U)
    {
        out_segment->status = SAMPLE_AUDIO_SEGMENT_UNDERRUN;
        return 1U;
    }

    const uint64_t last_position_q16 =
        (uint64_t)position_q16
        + ((uint64_t)step_q16 * (uint64_t)(segment_frames - 1U));
    const uint32_t last_base_frame =
        state->audio_cursor.current_start_frame
        + (uint32_t)(last_position_q16 >> 16U);
    if ((last_base_frame + 1U) < forward_end)
    {
        const uint32_t neighbor_frame = last_base_frame + 1U;
        if (neighbor_frame >= current_page_end)
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
            || (neighbor_frame >= current_page_end))
        {
            needs_neighbor = 1U;
            neighbor_page_index = sample_audio_format_page_index_from_frame(
                state->audio_cursor.format,
                neighbor_frame);
        }
    }

    if ((needs_neighbor != 0U) && (sample_voice_reader_acquire_neighbor_page(state, neighbor_page_index) == 0U))
    {
        out_segment->status = SAMPLE_AUDIO_SEGMENT_UNDERRUN;
        return 1U;
    }

    const uint64_t next_position_q16 = last_position_q16 + (uint64_t)step_q16;
    const uint64_t forward_end_q16 =
        ((uint64_t)(forward_end - state->audio_cursor.current_start_frame)) << 16U;
    const uint8_t loop_neighbor_in_current =
        (loop_forward != 0U)
            && (state->audio_cursor.current_start_frame <= state->plan.loop_begin)
            && (state->plan.loop_begin
                < (state->audio_cursor.current_start_frame + state->audio_cursor.current_frame_count))
            && (next_position_q16 >= forward_end_q16)
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

void sample_voice_reader_reset(sample_voice_reader_t *reader)
{
    if (reader == 0)
    {
        return;
    }

    sample_voice_reader_state_t *const state = sample_voice_reader_state(reader);
    sample_voice_reader_release_audio_cursor(state);
    if (state->lease_valid != 0U)
        sample_page_lease_audio_clear(state->lease_slot);
    if (state->musical_credit != 0U)
    {
        if (g_sample_voice_reader_musical_credits != 0U)
            --g_sample_voice_reader_musical_credits;
        state->musical_credit = 0U;
    }
    memset(state, 0, sizeof(*state));
}

void sample_voice_reader_bind(sample_voice_reader_t *reader,
                              uint16_t sample_id,
                              uint8_t reader_id,
                              uint32_t start_frame)
{
    if (reader == 0)
    {
        return;
    }

    const uint8_t lease_slot = sample_page_lease_classic_slot(reader_id);
    if (lease_slot == UINT8_MAX) return;

    sample_voice_reader_reset(reader);
    reader->sample_id = sample_id;
    reader->key = sample_audio_key_classic(sample_id);
    reader->format = SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED;
    reader->stride_floats = SAMPLE_AUDIO_FORMAT_STEREO_STRIDE_FLOATS;
    reader->frames_per_page = SAMPLE_AUDIO_FORMAT_STEREO_FRAMES_PER_PAGE;
    reader->lease_slot = lease_slot;
    reader->lease_valid = 1U;
    reader->position = (float)start_frame;
    reader->step = 1.0f;
    reader->frame_pos = start_frame;
    reader->active = 1U;
}

static uint8_t sample_voice_reader_bind_play_plan_slot(
    sample_voice_reader_t *reader, const sample_play_plan_t *plan,
    uint8_t lease_slot, uint8_t require_ready)
{
    if ((reader == 0) || (plan == 0)
        || ((plan->kernel_type != SAMPLE_KERNEL_FWD_1X)
            && (plan->kernel_type != SAMPLE_KERNEL_PITCH_FWD_LINEAR)))
    {
        return 0U;
    }

    if (lease_slot == UINT8_MAX || lease_slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT)
        return 0U;

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
    state->lease_slot = lease_slot;
    state->lease_valid = 1U;
    state->position = (float)plan->start_frame;
    state->step = (float)plan->step_q16 / (float)SAMPLE_Q16_ONE;
    state->frame_pos = plan->start_frame;
    state->active = 1U;
    state->plan = *plan;
    state->plan_valid = 1U;
    sample_voice_reader_publish_lease(state, plan->start_frame);

    if ((require_ready != 0U)
        && (sample_voice_reader_acquire_audio_page(
                state, plan->start_frame) == 0U))
    {
        sample_voice_reader_reset(reader);
        return 0U;
    }

    return 1U;
}

uint8_t sample_voice_reader_bind_play_plan(sample_voice_reader_t *reader,
                                           const sample_play_plan_t *plan,
                                           uint8_t reader_id)
{
    const uint8_t lease_slot = (plan != NULL)
        ? sample_voice_reader_lease_slot(plan->key, reader_id) : UINT8_MAX;
    return sample_voice_reader_bind_play_plan_slot(
        reader, plan, lease_slot, 1U);
}

uint8_t sample_voice_reader_bind_play_plan_deferred(
    sample_voice_reader_t *reader, const sample_play_plan_t *plan,
    uint8_t reader_id)
{
    const uint8_t lease_slot = (plan != NULL)
        ? sample_voice_reader_lease_slot(plan->key, reader_id) : UINT8_MAX;
    return sample_voice_reader_bind_play_plan_slot(
        reader, plan, lease_slot, 0U);
}

uint8_t sample_voice_reader_bind_preview_play_plan(
    sample_voice_reader_t *reader, const sample_play_plan_t *plan)
{
    return sample_voice_reader_bind_play_plan_slot(
        reader, plan, SAMPLE_PAGE_LEASE_PREVIEW_SLOT, 1U);
}

uint8_t sample_voice_reader_bind_musical_play_plan(sample_voice_reader_t *reader,
                                                   const sample_play_plan_t *plan,
                                                   uint8_t reader_id)
{
    if (reader == 0)
        return 0U;
    const sample_voice_reader_state_t *const state = sample_voice_reader_state(reader);
    if ((state->musical_credit == 0U)
        && (g_sample_voice_reader_musical_credits
            >= SAMPLE_STREAM_TARGET_MAX_VOICES))
        return 0U;
    if (sample_voice_reader_bind_play_plan(reader, plan, reader_id) == 0U)
        return 0U;
    sample_voice_reader_state(reader)->musical_credit = 1U;
    ++g_sample_voice_reader_musical_credits;
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
    sample_voice_reader_publish_lease(sample_voice_reader_state(reader), frame_pos);
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

    sample_voice_reader_reset(reader);
}

uint8_t sample_voice_reader_begin_block(sample_voice_reader_t *reader,
                                        uint32_t max_frames,
                                        sample_reader_block_t *out_block)
{
    if (out_block == 0)
    {
        return 0U;
    }

    memset(out_block, 0, sizeof(*out_block));
    out_block->format = SAMPLE_AUDIO_FORMAT_INVALID;
    out_block->status = SAMPLE_READER_BLOCK_NOT_READY;
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
    out_block->frame_step = (int32_t)segment.frame_stride;
    out_block->format = segment.format;
    out_block->frames_per_page = segment.frames_per_page;
    out_block->is_mono = segment.is_mono;
    switch (segment.status)
    {
        case SAMPLE_AUDIO_SEGMENT_OK:
            out_block->status = SAMPLE_READER_BLOCK_OK;
            break;

        case SAMPLE_AUDIO_SEGMENT_DONE:
            out_block->status = SAMPLE_READER_BLOCK_DONE;
            break;

        case SAMPLE_AUDIO_SEGMENT_UNDERRUN:
            out_block->status = SAMPLE_READER_BLOCK_UNDERRUN;
            break;

        case SAMPLE_AUDIO_SEGMENT_NOT_READY:
        default:
            out_block->status = SAMPLE_READER_BLOCK_NOT_READY;
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
            && (state->plan.kernel_type != SAMPLE_KERNEL_PITCH_FWD_LINEAR)))
    {
        return 0U;
    }

    if (state->plan.kernel_type == SAMPLE_KERNEL_PITCH_FWD_LINEAR)
    {
        return sample_voice_reader_prepare_pitch_forward_segment(state, max_frames, out_segment);
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

    uint32_t available = state->audio_cursor.current_frame_count - state->audio_cursor.current_offset_frames;
    const uint32_t region_remaining = forward_end - state->frame_pos;
    if (available > region_remaining)
    {
        available = region_remaining;
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
                                      && (state->plan.kernel_type != SAMPLE_KERNEL_PITCH_FWD_LINEAR)))
    {
        return;
    }

    if (state->plan.kernel_type == SAMPLE_KERNEL_PITCH_FWD_LINEAR)
    {
        const uint32_t forward_end = sample_voice_reader_forward_end_frame(state);
        const uint8_t loop_forward =
            ((state->plan.loop_mode == 1U) && (forward_end > state->plan.loop_begin)) ? 1U : 0U;
        uint8_t wrapped_loop = 0U;

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

        sample_voice_reader_publish_lease(state, state->frame_pos);
        if (state->active == 0U)
        {
            sample_voice_reader_release_audio_cursor(state);
            return;
        }

        if ((wrapped_loop != 0U) || (state->audio_cursor.current_acquired == 0U)
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

    state->frame_pos += consumed_frames;
    if ((state->plan.loop_mode == SAMPLE_PLAY_LOOP_FORWARD) && (forward_end > state->plan.loop_begin)
        && (state->frame_pos >= forward_end))
    {
        state->frame_pos = state->plan.loop_begin;
        wrapped_loop = 1U;
    }
    state->position = (float)state->frame_pos;
    sample_voice_reader_publish_lease(state, state->frame_pos);

    if (state->audio_cursor.current_acquired == 0U)
    {
        return;
    }

    state->audio_cursor.current_offset_frames += consumed_frames;

    if ((state->plan.loop_mode == SAMPLE_PLAY_LOOP_NONE) && (state->frame_pos >= forward_end))
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

    if (state->audio_cursor.current_offset_frames < state->audio_cursor.current_frame_count)
    {
        return;
    }

    sample_voice_reader_release_audio_cursor(state);
    (void)sample_voice_reader_acquire_audio_page(state, state->frame_pos);
}
