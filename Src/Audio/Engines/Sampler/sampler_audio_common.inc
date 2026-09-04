uint8_t brick6_sampler_runtime_ram_slice_mode_active(uint8_t track_id)
{
    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track_id, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    if ((ctx == NULL)
        || (ctx->program_route.active == 0U)
        || (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
        || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER))
    {
        return 0U;
    }

    return ((ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_RAM)
            && (brick6_sampler_runtime_audio_slice_count(track_id) != 0U))
               ? 1U
               : 0U;
}

uint8_t brick6_sampler_runtime_audio_slice_count(uint8_t track_id)
{
    return (track_id < SEQ_TRACK_COUNT)
        ? g_sampler_voice[track_id].slice_count : 0U;
}

static void brick6_sampler_runtime_voice_note_output(brick6_sampler_voice_t *voice,
                                                     float out_l,
                                                     float out_r)
{
    if (voice == NULL)
    {
        return;
    }

    voice->last_out_l = out_l;
    voice->last_out_r = out_r;
    voice->last_out_valid = 1U;
}

static void brick6_sampler_runtime_mark_track_activity(uint8_t track_id)
{
    if (track_id < SEQ_TRACK_COUNT)
        g_sampler_render_track_mask |= (uint32_t)(1UL << track_id);
}

static void brick6_sampler_runtime_refresh_track_activity(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
        return;

    uint8_t active = (uint8_t)((g_sampler_voice[track_id].active != 0U)
        || (g_sampler_voice[track_id].reader.active != 0U)
        || (g_sampler_voice[track_id].release_pending != 0U)
        || (g_sampler_clip_runtime[track_id].state
            == (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING));
    for (uint8_t i = 0U; (i < SAMPLER_MULTI_MAX_GLOBAL_VOICES) && (active == 0U); ++i)
    {
        const brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        active = (uint8_t)((voice->owner_track_id == track_id)
            && ((voice->active != 0U)
                || (voice->dsp_slot != MULTI_VOICE_DSP_SLOT_INDEX_INVALID)));
    }
    for (uint8_t i = 0U; (i < STEAL_DECLICK_TAIL_SLOTS) && (active == 0U); ++i)
    {
        active = (uint8_t)((g_sampler_declick_tail[i].active != 0U)
            && (g_sampler_declick_tail[i].owner_track_id == track_id));
    }

    const uint32_t bit = (uint32_t)(1UL << track_id);
    if (active != 0U)
        g_sampler_render_track_mask |= bit;
    else
        g_sampler_render_track_mask &= ~bit;
}

static uint8_t brick6_sampler_runtime_begin_declick_tail(uint8_t track_id,
                                                         brick6_sampler_voice_t *voice)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (voice == NULL) || (voice->active == 0U)
        || (voice->last_out_valid == 0U))
    {
        return 0U;
    }

    if ((fabsf(voice->last_out_l) <= STEAL_DECLICK_EPSILON)
        && (fabsf(voice->last_out_r) <= STEAL_DECLICK_EPSILON))
    {
        voice->last_out_l = 0.0f;
        voice->last_out_r = 0.0f;
        voice->last_out_valid = 0U;
        return 0U;
    }

    for (uint8_t i = 0U; i < STEAL_DECLICK_TAIL_SLOTS; ++i)
    {
        brick6_sampler_declick_tail_t *const tail = &g_sampler_declick_tail[i];
        if (tail->active != 0U)
        {
            continue;
        }

        tail->start_l = voice->last_out_l;
        tail->start_r = voice->last_out_r;
        tail->owner_track_id = track_id;
        tail->remaining = (uint8_t)STEAL_DECLICK_SAMPLES;
        tail->active = 1U;
        brick6_sampler_runtime_mark_track_activity(track_id);
        break;
    }

    voice->last_out_l = 0.0f;
    voice->last_out_r = 0.0f;
    voice->last_out_valid = 0U;
    return 1U;
}

static void brick6_sampler_runtime_start_declick_fade_in(brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return;
    }

    voice->start_fade_remaining = (uint8_t)STEAL_DECLICK_SAMPLES;
}

static uint8_t brick6_sampler_runtime_apply_start_fade(brick6_sampler_voice_t *voice,
                                                       float *fade,
                                                       uint32_t frames)
{
    if ((voice == NULL) || (fade == NULL) || (frames == 0U)
        || (voice->start_fade_remaining == 0U))
    {
        return 0U;
    }

    const float denom = (STEAL_DECLICK_SAMPLES > 1U)
                            ? (1.0f / (float)(STEAL_DECLICK_SAMPLES - 1U))
                            : 1.0f;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        if (voice->start_fade_remaining == 0U)
        {
            break;
        }

        const float gain = (voice->start_fade_remaining < STEAL_DECLICK_SAMPLES)
                               ? ((float)(STEAL_DECLICK_SAMPLES - voice->start_fade_remaining) * denom)
                               : 0.0f;
        fade[i] *= gain;
        voice->start_fade_remaining--;
    }
    return 1U;
}

static void brick6_sampler_runtime_mix_declick_tails(uint8_t track_id,
                                                     float *out_l,
                                                     float *out_r,
                                                     uint32_t frames)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (out_l == NULL) || (out_r == NULL)
        || (frames == 0U))
    {
        return;
    }

    const float denom = (STEAL_DECLICK_SAMPLES > 1U)
                            ? (1.0f / (float)(STEAL_DECLICK_SAMPLES - 1U))
                            : 1.0f;
    for (uint8_t t = 0U; t < STEAL_DECLICK_TAIL_SLOTS; ++t)
    {
        brick6_sampler_declick_tail_t *const tail = &g_sampler_declick_tail[t];
        if ((tail->active == 0U) || (tail->owner_track_id != track_id))
        {
            continue;
        }

        for (uint32_t i = 0U; (i < frames) && (tail->remaining != 0U); ++i)
        {
            const float gain = (tail->remaining > 1U)
                                   ? ((float)(tail->remaining - 1U) * denom)
                                   : 0.0f;
            out_l[i] += tail->start_l * gain;
            out_r[i] += tail->start_r * gain;
            tail->remaining--;
        }

        if (tail->remaining == 0U)
        {
            tail->active = 0U;
            tail->start_l = 0.0f;
            tail->start_r = 0.0f;
            tail->owner_track_id = UINT8_MAX;
        }
    }
    brick6_sampler_runtime_refresh_track_activity(track_id);
}

static void brick6_sampler_runtime_clip_render_shifter_mono(brick6_sampler_voice_t *voice,
                                                             brick6_sampler_clip_runtime_t *clip,
                                                             brick6_sampler_clip_slot_t *slot,
                                                             float *out_mono,
                                                             uint32_t frames)
{
    if ((voice == NULL) || (clip == NULL) || (slot == NULL) || (out_mono == NULL) || (frames == 0U))
    {
        return;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    /* Reuse the existing left scratch as the single mono scratch. */
    memset(slot->stretch_render_l, 0, sizeof(slot->stretch_render_l));
    brick6_sampler_runtime_clip_configure_shifter(clip, slot);
    brick6_sampler_render_sample_segment_cursor_mono(voice,
                                                     slot->stretch_render_l,
                                                     frames);
    brick6_clip_shifter_process_mono(&slot->shifter,
                                     slot->stretch_render_l,
                                     frames);

    for (uint32_t i = 0U; i < frames; ++i)
    {
        out_mono[i] += slot->stretch_render_l[i];
    }
    brick6_sampler_runtime_voice_note_output(voice,
                                             slot->stretch_render_l[frames - 1U],
                                             slot->stretch_render_l[frames - 1U]);
}

static void brick6_sampler_runtime_mix_reader_segment_mono(
    const sample_audio_segment_t *segment,
    float gain,
    const float *fade_gain,
    uint32_t fade_count,
    float *out_mono,
    uint32_t out_offset,
    float *out_last)
{
    if ((segment == NULL) || (segment->is_mono == 0U)
        || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    switch (segment->kernel_type)
    {
        case SAMPLE_KERNEL_REV_1X:
            sample_voice_reader_mix_rev_1x_mono(segment,
                                                gain,
                                                fade_gain,
                                                fade_count,
                                                out_mono,
                                                out_offset,
                                                out_last);
            break;
        case SAMPLE_KERNEL_PITCH_FWD_LINEAR:
            sample_voice_reader_mix_pitch_fwd_linear_mono(segment,
                                                          gain,
                                                          fade_gain,
                                                          fade_count,
                                                          out_mono,
                                                          out_offset,
                                                          out_last);
            break;
        case SAMPLE_KERNEL_PITCH_REV_LINEAR:
            sample_voice_reader_mix_pitch_rev_linear_mono(segment,
                                                          gain,
                                                          fade_gain,
                                                          fade_count,
                                                          out_mono,
                                                          out_offset,
                                                          out_last);
            break;
        case SAMPLE_KERNEL_FWD_1X:
        default:
            sample_voice_reader_mix_fwd_1x_mono(segment,
                                                gain,
                                                fade_gain,
                                                fade_count,
                                                out_mono,
                                                out_offset,
                                                out_last);
            break;
    }
}

static void brick6_sampler_render_sample_segment_cursor_mono(brick6_sampler_voice_t *voice,
                                                             float *out_mono,
                                                             uint32_t frames)
{
    if ((voice == NULL) || (out_mono == NULL) || (frames == 0U)
        || (sample_audio_format_or_stereo(voice->format)
            != SAMPLE_AUDIO_FORMAT_FLOAT32_MONO))
    {
        return;
    }

    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    uint32_t produced = 0U;
    while (produced < frames)
    {
        sample_audio_segment_t segment;
        if ((sample_voice_reader_begin_segment(&voice->reader, frames - produced, &segment) == 0U)
            || (segment.status != SAMPLE_AUDIO_SEGMENT_OK) || (segment.frames == 0U)
            || (segment.is_mono == 0U))
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        BRICK6_SAMPLER_RUNTIME_DIAG_INC(segment_cursor_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);

        float fade_buf[AUDIO_BLOCK_SIZE];
        const float *fade_ptr = NULL;
        uint32_t fade_count = 0U;
        if ((voice->fade_in_frames != 0U) || (voice->fade_out_frames != 0U))
        {
            for (uint32_t i = 0U; i < segment.frames; ++i)
            {
                uint32_t loop_pos = 0U;
                if (segment.kernel_type == SAMPLE_KERNEL_REV_1X)
                {
                    loop_pos = (segment.start_frame >= (voice->region_begin + i))
                                   ? (segment.start_frame - voice->region_begin - i)
                                   : 0U;
                }
                else if (segment.kernel_type == SAMPLE_KERNEL_PITCH_FWD_LINEAR)
                {
                    const uint32_t base_frame =
                        (uint32_t)(segment.source_position + ((float)i * segment.source_step));
                    loop_pos = (base_frame > voice->region_begin)
                                   ? (base_frame - voice->region_begin)
                                   : 0U;
                }
                else if (segment.kernel_type == SAMPLE_KERNEL_PITCH_REV_LINEAR)
                {
                    const uint32_t base_frame =
                        (uint32_t)(segment.source_position - ((float)i * segment.source_step));
                    loop_pos = ((voice->region_end > 0U) && (base_frame < voice->region_end))
                                   ? ((voice->region_end - 1U) - base_frame)
                                   : 0U;
                }
                else
                {
                    loop_pos = (segment.start_frame + i) - voice->region_begin;
                }
                fade_buf[i] = brick6_sampler_runtime_fade_gain(loop_pos,
                                                               voice->loop_frames,
                                                               voice->fade_in_frames,
                                                               voice->fade_out_frames);
            }
            fade_ptr = fade_buf;
            fade_count = segment.frames;
        }
        else if (voice->start_fade_remaining != 0U)
        {
            for (uint32_t i = 0U; i < segment.frames; ++i)
            {
                fade_buf[i] = 1.0f;
            }
            fade_ptr = fade_buf;
            fade_count = segment.frames;
        }
        if (fade_ptr != NULL)
        {
            (void)brick6_sampler_runtime_apply_start_fade(voice, fade_buf, segment.frames);
        }

        float last = 0.0f;
        brick6_sampler_runtime_mix_reader_segment_mono(&segment,
                                                       render_gain,
                                                       fade_ptr,
                                                       fade_count,
                                                       out_mono,
                                                       produced,
                                                       &last);
        brick6_sampler_runtime_voice_note_output(voice, last, last);
        sample_voice_reader_commit_segment(&voice->reader, segment.frames);
        produced += segment.frames;
        voice->position = voice->reader.position;
        if (voice->reader.active == 0U)
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }
    }
}

static void brick6_sampler_runtime_mix_declick_tails_mono(uint8_t track_id,
                                                          float *out_mono,
                                                          uint32_t frames)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (out_mono == NULL) || (frames == 0U))
    {
        return;
    }

    const float denom = (STEAL_DECLICK_SAMPLES > 1U)
                            ? (1.0f / (float)(STEAL_DECLICK_SAMPLES - 1U))
                            : 1.0f;
    for (uint8_t t = 0U; t < STEAL_DECLICK_TAIL_SLOTS; ++t)
    {
        brick6_sampler_declick_tail_t *const tail = &g_sampler_declick_tail[t];
        if ((tail->active == 0U) || (tail->owner_track_id != track_id))
        {
            continue;
        }

        for (uint32_t i = 0U; (i < frames) && (tail->remaining != 0U); ++i)
        {
            const float gain = (tail->remaining > 1U)
                                   ? ((float)(tail->remaining - 1U) * denom)
                                   : 0.0f;
            out_mono[i] += tail->start_l * gain;
            tail->remaining--;
        }

        if (tail->remaining == 0U)
        {
            tail->active = 0U;
            tail->start_l = 0.0f;
            tail->start_r = 0.0f;
            tail->owner_track_id = UINT8_MAX;
        }
    }
    brick6_sampler_runtime_refresh_track_activity(track_id);
}

static void brick6_sampler_runtime_multi_track_reset(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_multi_track_state[track_id].instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_multi_track_state[track_id].gain = 1.0f;
    g_sampler_multi_track_state[track_id].loop_enabled = 0U;
    g_sampler_multi_track_state[track_id].voice_count = 1U;
    g_sampler_multi_track_state[track_id].spread = 0.0f;
}

static void brick6_sampler_runtime_diag_note_trigger(uint8_t track_id,
                                                     const brick6_sampler_voice_t *voice,
                                                     uint32_t start_frame,
                                                     uint32_t sample_length_frames)
{
    if (voice == NULL)
    {
        return;
    }

    g_brick6_sampler_runtime_diag.start_frame = start_frame;
    g_brick6_sampler_runtime_diag.region_begin = voice->region_begin;
    g_brick6_sampler_runtime_diag.region_end = voice->region_end;
    g_brick6_sampler_runtime_diag.sample_length_frames = sample_length_frames;
    g_brick6_sampler_runtime_diag.fade_in_frames = voice->fade_in_frames;
    g_brick6_sampler_runtime_diag.fade_out_frames = voice->fade_out_frames;
    g_brick6_sampler_runtime_diag.sample_id = voice->sample_id;
    g_brick6_sampler_runtime_diag.track_id = track_id;
    g_brick6_sampler_runtime_diag.note = voice->note;
    g_brick6_sampler_runtime_diag.velocity = voice->velocity;
    g_brick6_sampler_runtime_diag.mode = voice->mode;
    g_brick6_sampler_runtime_diag.use_segment_cursor = voice->use_segment_cursor;
}

static uint32_t brick6_sampler_runtime_common_plan_reason(
    brick6_sample_common_plan_result_t result)
{
    return (uint32_t)result;
}

static void brick6_sampler_runtime_note_common_play_plan_result(
    brick6_sample_common_plan_result_t result,
    uint8_t classic)
{
    if (result == BRICK6_SAMPLE_COMMON_PLAN_OK)
    {
        return;
    }

    g_brick6_sampler_runtime_diag.common_plan_last_reason =
        brick6_sampler_runtime_common_plan_reason(result);
    if (classic != 0U)
    {
        g_brick6_sampler_runtime_diag.common_plan_classic_build_fail++;
    }
    else
    {
        g_brick6_sampler_runtime_diag.common_plan_multi_build_fail++;
    }
}

static brick6_sample_common_plan_result_t brick6_sampler_runtime_build_common_play_plan(
    const brick6_sample_common_trigger_t *trigger,
    sample_resolved_source_t *out_source,
    sample_play_plan_t *out_plan)
{
    if (out_source != NULL)
    {
        sample_resolved_source_init(out_source);
    }
    if (out_plan != NULL)
    {
        sample_play_plan_init(out_plan);
    }
    if ((trigger == NULL) || (out_source == NULL) || (out_plan == NULL)
        || (trigger->track_id >= SEQ_TRACK_COUNT))
    {
        return BRICK6_SAMPLE_COMMON_PLAN_INVALID_ARG;
    }

    sample_play_plan_build_options_t options = {
        .min_ready_frames = BRICK6_SAMPLER_MIN_READY_TARGET_FRAMES,
        .target_window_frames = 0U,
        .diagnostics_page = UINT32_MAX,
        .flags = (uint8_t)(SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_REGION
                           | SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_LOOP
                           | SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_DIRECTION
                           | SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_RATE),
        .stop_on_underrun = 1U,
        .diagnostics_reason = 0U,
    };

    if (trigger->kind == BRICK6_SAMPLE_COMMON_TRIGGER_CLASSIC)
    {
        const brick6_sampler_voice_t *const voice = trigger->voice;
        const sample_play_plan_t *const runtime_plan = trigger->runtime_plan;
        if ((voice == NULL)
                || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
                || (runtime_plan == NULL))
        {
            return BRICK6_SAMPLE_COMMON_PLAN_INVALID_ARG;
        }
        if (sample_classic_audio_projection_resolve(voice->sample_id, out_source) == 0U)
        {
            return BRICK6_SAMPLE_COMMON_PLAN_RESOLVE_FAIL;
        }
        out_source->region_begin = voice->region_begin;
        out_source->region_end = voice->region_end;
        out_source->loop_begin = runtime_plan->loop_begin;
        out_source->loop_end = runtime_plan->loop_end;
        out_source->loop_mode = voice->loop_mode;
        out_source->reverse = voice->reverse;
        out_source->rate = (voice->step_signed > 0.0f) ? voice->step_signed : 1.0f;
        out_source->gain = voice->gain * voice->trigger_velocity_gain;
        out_source->fine_tune_cents = (int16_t)(voice->tune * 100.0f);
        out_source->owner_track_id = trigger->track_id;
        out_source->note = voice->note;
        out_source->velocity = voice->velocity;
        out_source->source_kind = voice->source_kind;

        options.start_frame = runtime_plan->start_frame;
        options.end_frame = voice->region_end;
        options.loop_begin = runtime_plan->loop_begin;
        options.loop_end = runtime_plan->loop_end;
        options.rate = out_source->rate;
        options.reverse = voice->reverse;
        options.loop_mode = voice->loop_mode;
    }
    else if (trigger->kind == BRICK6_SAMPLE_COMMON_TRIGGER_MULTI)
    {
        if ((trigger->instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
            || (trigger->note > 127U) || (trigger->velocity > 127U)
            || (trigger->total_frames == 0U))
        {
            return BRICK6_SAMPLE_COMMON_PLAN_INVALID_ARG;
        }
        const multi_sample_audio_source_t *const projected = trigger->multi_source;
        if ((projected == NULL) || (projected->total_frames == 0U))
        {
            return BRICK6_SAMPLE_COMMON_PLAN_RESOLVE_FAIL;
        }
        out_source->key = sample_audio_key_multi(projected->multi_sample_id);
        out_source->total_frames = projected->total_frames;
        out_source->data_offset = projected->data_offset;
        out_source->data_size = projected->data_size;
        out_source->sample_rate = projected->sample_rate;
        out_source->channels = projected->channels;
        out_source->bits_per_sample = projected->bits_per_sample;
        out_source->block_align = projected->block_align;
        out_source->format = (sample_audio_format_t)projected->format;
        out_source->stride_floats = projected->stride_floats;
        out_source->frames_per_page = projected->frames_per_page;
        out_source->registration_epoch = projected->registration_epoch;
        out_source->root_note = projected->root_note;
        out_source->region_begin = 0U;
        out_source->region_end = projected->total_frames;
        out_source->loop_begin = projected->loop_begin;
        out_source->loop_end = projected->loop_end;
        out_source->instrument_id = projected->instrument_id;
        out_source->zone_id = projected->zone_id;
        const float rate = (trigger->step_q16 != 0U)
                               ? ((float)trigger->step_q16 / (float)BRICK6_SAMPLER_Q16_ONE)
                               : 1.0f;
        const uint8_t loop_enabled =
            (g_sampler_multi_track_state[trigger->track_id].loop_enabled != 0U) ? 1U : 0U;
        out_source->region_begin = 0U;
        out_source->region_end = trigger->total_frames;
        if ((out_source->loop_end <= out_source->loop_begin)
            || (out_source->loop_begin < out_source->region_begin)
            || (out_source->loop_end > out_source->region_end))
        {
            out_source->loop_begin = out_source->region_begin;
            out_source->loop_end = out_source->region_end;
        }
        out_source->loop_mode = (loop_enabled != 0U)
            ? (uint8_t)SAMPLE_PLAY_LOOP_FORWARD
            : (uint8_t)SAMPLE_PLAY_LOOP_NONE;
        out_source->reverse = 0U;
        out_source->rate = rate;
        out_source->gain = trigger->render_gain;
        out_source->owner_track_id = trigger->track_id;
        out_source->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_MULTI;

        options.start_frame = 0U;
        options.end_frame = trigger->total_frames;
        options.loop_begin = out_source->loop_begin;
        options.loop_end = out_source->loop_end;
        options.rate = rate;
        options.reverse = 0U;
        options.loop_mode = out_source->loop_mode;
        options.min_ready_frames = sample_audio_format_multi_presocle_pages(
                                       sample_audio_format_or_stereo(out_source->format))
                                   * sample_audio_format_frames_per_page(
                                       sample_audio_format_or_stereo(out_source->format));
    }
    else
    {
        return BRICK6_SAMPLE_COMMON_PLAN_INVALID_ARG;
    }

    if (sample_resolved_source_is_valid(out_source) == 0U)
    {
        return BRICK6_SAMPLE_COMMON_PLAN_SOURCE_INVALID;
    }
    if (sample_play_plan_build_from_source(out_source, &options, out_plan)
        != SAMPLE_PLAY_PLAN_BUILD_OK)
    {
        return BRICK6_SAMPLE_COMMON_PLAN_BUILD_FAIL;
    }
    if (sample_play_plan_is_valid(out_plan) == 0U)
    {
        return BRICK6_SAMPLE_COMMON_PLAN_PLAN_INVALID;
    }
    return BRICK6_SAMPLE_COMMON_PLAN_OK;
}

static uint8_t brick6_sampler_runtime_multi_voice_index(const brick6_sampler_voice_t *voice)
{
    if ((voice >= &g_sampler_multi_voice[0])
        && (voice < &g_sampler_multi_voice[SAMPLER_MULTI_MAX_GLOBAL_VOICES]))
    {
        return (uint8_t)(voice - &g_sampler_multi_voice[0]);
    }

    return UINT8_MAX;
}

static brick6_sampler_multi_voice_handle_t
brick6_sampler_runtime_multi_voice_handle(const brick6_sampler_voice_t *voice)
{
    const uint8_t voice_index = brick6_sampler_runtime_multi_voice_index(voice);
    const brick6_sampler_multi_voice_handle_t handle = {
        .voice_index = voice_index,
        .reserved = { 0U, 0U, 0U },
        .generation = (voice != NULL) ? voice->trigger_order : 0U,
    };
    return handle;
}

static void brick6_sampler_runtime_multi_release_dsp_slot(brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return;
    }

    const uint8_t dsp_slot = voice->dsp_slot;
    if ((dsp_slot != MULTI_VOICE_DSP_SLOT_INDEX_INVALID)
        && (voice->trigger_order != 0U))
    {
        (void)multi_voice_dsp_release(
            dsp_slot,
            brick6_sampler_runtime_multi_voice_handle(voice));
    }
    voice->dsp_slot = MULTI_VOICE_DSP_SLOT_INDEX_INVALID;
}

static sample_audio_key_t brick6_sampler_runtime_multi_key(uint16_t multi_sample_id)
{
    return sample_audio_key_multi(multi_sample_id);
}

static void brick6_sampler_runtime_multi_release_voice_vca(
    brick6_sampler_voice_t *voice)
{
    if ((voice == NULL) || (voice->vca_note_released != 0U))
    {
        return;
    }

    mixer_multi_filter_note_off(multi_voice_dsp_get(voice->dsp_slot));
    voice->vca_note_released = 1U;
}

static void brick6_sampler_runtime_multi_mark_voice_physical_release(
    brick6_sampler_voice_t *voice)
{
    const uint8_t voice_index = brick6_sampler_runtime_multi_voice_index(voice);
    if ((voice == NULL) || (voice_index == UINT8_MAX) || (voice->trigger_order == 0U))
    {
        return;
    }

    brick6_sampler_runtime_multi_release_voice_vca(voice);
    voice->release_pending = 1U;
}

static void brick6_sampler_runtime_multi_diag_note_reject(uint8_t track_id,
                                                          uint8_t note,
                                                          uint8_t velocity,
                                                          uint16_t instrument_id,
                                                          uint16_t sample_id,
                                                          uint8_t reason)
{
    g_brick6_sampler_runtime_diag.multi_last_reject_reason = reason;
    g_brick6_sampler_runtime_diag.multi_last_active_global =
        (uint8_t)brick6_sampler_runtime_multi_active_count();
    g_brick6_sampler_runtime_diag.multi_last_active_track =
        (uint8_t)brick6_sampler_runtime_multi_active_count_for_track(track_id);
    (void)note;
    (void)velocity;
    (void)instrument_id;
    (void)sample_id;
}

static void brick6_sampler_runtime_multi_diag_note_page0_reject(
    uint8_t track_id,
    uint8_t note,
    uint8_t velocity,
    uint16_t instrument_id,
    const multi_sample_audio_source_t *resolved,
    sample_page_state_t state0)
{
    if (resolved == NULL)
    {
        brick6_sampler_runtime_multi_diag_note_reject(
            track_id,
            note,
            velocity,
            instrument_id,
            (resolved != NULL) ? resolved->multi_sample_id : MULTI_SAMPLE_POOL_INVALID_ID,
            (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_PAGE0_MISSING);
        return;
    }

    const uint16_t sample_id = resolved->multi_sample_id;
    g_brick6_sampler_runtime_diag.multi_last_reject_reason =
        (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_PAGE0_MISSING;
    g_brick6_sampler_runtime_diag.multi_last_active_global =
        (uint8_t)brick6_sampler_runtime_multi_active_count();
    g_brick6_sampler_runtime_diag.multi_last_active_track =
        (uint8_t)brick6_sampler_runtime_multi_active_count_for_track(track_id);

    if (sample_id >= MULTI_SAMPLE_POOL_MAX_SAMPLES)
    {
        return;
    }
    if (g_sampler_multi_page0_reject_logged[sample_id] != 0U)
    {
        return;
    }
    g_sampler_multi_page0_reject_logged[sample_id] = 1U;

    (void)note;
    (void)velocity;
    (void)instrument_id;
    (void)state0;
}

static void brick6_sampler_runtime_multi_diag_note_trigger(const brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return;
    }

    g_brick6_sampler_runtime_diag.multi_last_active_global =
        (uint8_t)brick6_sampler_runtime_multi_active_count();
    g_brick6_sampler_runtime_diag.multi_last_active_track =
        (uint8_t)brick6_sampler_runtime_multi_active_count_for_track(voice->owner_track_id);

}

static void brick6_sampler_runtime_multi_direct_stop(
    brick6_sampler_voice_t *voice,
    sample_audio_segment_status_t status)
{
    const uint8_t stop_reason =
        (status == SAMPLE_AUDIO_SEGMENT_DONE)
            ? (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_DONE
            : (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN;
    if (stop_reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN)
    {
        g_brick6_sampler_runtime_diag.multi_page_underrun++;
    }
    brick6_sampler_runtime_multi_diag_note_stop(voice, stop_reason);
    brick6_sampler_runtime_multi_mark_voice_physical_release(voice);
    voice->active = 0U;
    voice->position = 0.0f;
    sample_voice_reader_stop(&voice->reader);
}

static void brick6_sampler_render_multi_mono_1x_direct(
    brick6_sampler_voice_t *voice,
    float *out_mono,
    uint32_t frames)
{
    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    uint32_t produced = 0U;
    while (produced < frames)
    {
        uint32_t rendered = 0U;
        sample_audio_segment_status_t status = SAMPLE_AUDIO_SEGMENT_NOT_READY;
        float last = 0.0f;
        if ((sample_voice_reader_render_multi_fwd_1x_mono(
                 &voice->reader,
                 render_gain,
                 out_mono,
                 frames - produced,
                 produced,
                 &rendered,
                 &status,
                 &last) == 0U)
            || (status != SAMPLE_AUDIO_SEGMENT_OK) || (rendered == 0U))
        {
            brick6_sampler_runtime_multi_direct_stop(voice, status);
            break;
        }
        brick6_sampler_runtime_voice_note_output(voice, last, last);
        produced += rendered;
        voice->position = voice->reader.position;
        if (voice->reader.active == 0U)
        {
            brick6_sampler_runtime_multi_direct_stop(
                voice, SAMPLE_AUDIO_SEGMENT_DONE);
            break;
        }
    }
}

static void brick6_sampler_render_multi_stereo_1x_direct(
    brick6_sampler_voice_t *voice,
    float *out_l,
    float *out_r,
    uint32_t frames)
{
    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    uint32_t produced = 0U;
    while (produced < frames)
    {
        uint32_t rendered = 0U;
        sample_audio_segment_status_t status = SAMPLE_AUDIO_SEGMENT_NOT_READY;
        float last_l = 0.0f;
        float last_r = 0.0f;
        if ((sample_voice_reader_render_multi_fwd_1x_stereo(
                 &voice->reader,
                 render_gain,
                 out_l,
                 out_r,
                 frames - produced,
                 produced,
                 &rendered,
                 &status,
                 &last_l,
                 &last_r) == 0U)
            || (status != SAMPLE_AUDIO_SEGMENT_OK) || (rendered == 0U))
        {
            brick6_sampler_runtime_multi_direct_stop(voice, status);
            break;
        }
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
        produced += rendered;
        voice->position = voice->reader.position;
        if (voice->reader.active == 0U)
        {
            brick6_sampler_runtime_multi_direct_stop(
                voice, SAMPLE_AUDIO_SEGMENT_DONE);
            break;
        }
    }
}

static void brick6_sampler_render_multi_mono(brick6_sampler_voice_t *voice,
                                             float *out_mono,
                                             uint32_t frames)
{
    if ((voice == NULL) || (out_mono == NULL) || (frames == 0U)
        || (voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
        || (sample_audio_format_or_stereo(voice->play_plan.format)
            != SAMPLE_AUDIO_FORMAT_FLOAT32_MONO))
    {
        return;
    }

    if ((voice->multi_renderer == BRICK6_SAMPLER_MULTI_RENDER_MONO_1X)
        && (voice->start_fade_remaining == 0U))
    {
        brick6_sampler_render_multi_mono_1x_direct(voice, out_mono, frames);
        return;
    }

    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    uint32_t produced = 0U;
    while (produced < frames)
    {
        sample_audio_segment_t segment;
        if (sample_voice_reader_begin_segment(&voice->reader, frames - produced, &segment) == 0U)
        {
            g_brick6_sampler_runtime_diag.multi_page_underrun++;
            brick6_sampler_runtime_multi_diag_note_stop(
                voice,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN);
            brick6_sampler_runtime_multi_mark_voice_physical_release(voice);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        if (segment.status == SAMPLE_AUDIO_SEGMENT_DONE)
        {
            const uint8_t stop_reason =
                (voice->reader.position >= (float)voice->region_end)
                    ? (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_DONE
                    : (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN;
            if (stop_reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN)
            {
                g_brick6_sampler_runtime_diag.multi_page_underrun++;
            }
            brick6_sampler_runtime_multi_diag_note_stop(voice, stop_reason);
            brick6_sampler_runtime_multi_mark_voice_physical_release(voice);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }
        if ((segment.status != SAMPLE_AUDIO_SEGMENT_OK) || (segment.frames == 0U)
            || (segment.is_mono == 0U))
        {
            if (segment.status == SAMPLE_AUDIO_SEGMENT_UNDERRUN)
            {
                g_brick6_sampler_runtime_diag.multi_page_underrun++;
            }
            brick6_sampler_runtime_multi_diag_note_stop(
                voice,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN);
            brick6_sampler_runtime_multi_mark_voice_physical_release(voice);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        BRICK6_SAMPLER_RUNTIME_DIAG_INC(segment_cursor_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);
        float fade_buf[AUDIO_BLOCK_SIZE];
        const float *fade_ptr = NULL;
        uint32_t fade_count = 0U;
        if (voice->start_fade_remaining != 0U)
        {
            for (uint32_t i = 0U; i < segment.frames; ++i)
            {
                fade_buf[i] = 1.0f;
            }
            (void)brick6_sampler_runtime_apply_start_fade(voice, fade_buf, segment.frames);
            fade_ptr = fade_buf;
            fade_count = segment.frames;
        }

        float last = 0.0f;
        brick6_sampler_runtime_mix_reader_segment_mono(&segment,
                                                       render_gain,
                                                       fade_ptr,
                                                       fade_count,
                                                       out_mono,
                                                       produced,
                                                       &last);
        brick6_sampler_runtime_voice_note_output(voice, last, last);

        sample_voice_reader_commit_segment(&voice->reader, segment.frames);
        produced += segment.frames;
        voice->position = voice->reader.position;
        if (voice->reader.active == 0U)
        {
            const uint8_t stop_reason =
                (voice->reader.position >= (float)voice->region_end)
                    ? (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_DONE
                    : (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN;
            if (stop_reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN)
            {
                g_brick6_sampler_runtime_diag.multi_page_underrun++;
            }
            brick6_sampler_runtime_multi_diag_note_stop(voice, stop_reason);
            brick6_sampler_runtime_multi_mark_voice_physical_release(voice);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }
    }
}

static void brick6_sampler_runtime_multi_diag_note_stop(const brick6_sampler_voice_t *voice,
                                                        uint8_t reason)
{
    if (voice == NULL)
    {
        return;
    }

    const uint32_t current_frame = (uint32_t)voice->position;
    const uint32_t end_frame = voice->region_end;
    g_brick6_sampler_runtime_diag.multi_last_stop_reason = reason;
    g_brick6_sampler_runtime_diag.multi_last_current_frame = current_frame;
    g_brick6_sampler_runtime_diag.multi_last_end_frame = end_frame;
    if (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_DONE)
    {
        g_brick6_sampler_runtime_diag.multi_stop_done++;
    }
    else if (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN)
    {
        g_brick6_sampler_runtime_diag.multi_stop_underrun++;
    }
    else if (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_STEAL)
    {
        g_brick6_sampler_runtime_diag.multi_stop_steal++;
    }
    else if (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_REL_DONE)
    {
        g_brick6_sampler_runtime_diag.multi_stop_rel_done++;
    }
    g_brick6_sampler_runtime_diag.multi_last_active_global =
        (uint8_t)brick6_sampler_runtime_multi_active_count();
    g_brick6_sampler_runtime_diag.multi_last_active_track =
        (uint8_t)brick6_sampler_runtime_multi_active_count_for_track(voice->owner_track_id);

}

