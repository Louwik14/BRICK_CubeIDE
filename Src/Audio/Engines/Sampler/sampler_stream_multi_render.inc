static void brick6_sampler_runtime_mix_reader_segment(const sample_audio_segment_t *segment,
                                                      float gain,
                                                      const float *fade_gain,
                                                      uint32_t fade_count,
                                                      float *out_l,
                                                      float *out_r,
                                                      uint32_t out_offset,
                                                      float *out_last_l,
                                                      float *out_last_r)
{
    if ((segment == NULL) || (segment->status != SAMPLE_AUDIO_SEGMENT_OK))
    {
        return;
    }

    /* This dispatcher is the stereo insertion point, including the explicit
       mono-to-stereo promotion required by the clip shifter. */
    switch (segment->kernel_type)
    {
        case SAMPLE_KERNEL_REV_1X:
            sample_voice_reader_mix_rev_1x(segment,
                                           gain,
                                           fade_gain,
                                           fade_count,
                                           out_l,
                                           out_r,
                                           out_offset,
                                           out_last_l,
                                           out_last_r);
            break;
        case SAMPLE_KERNEL_PITCH_FWD_LINEAR:
            sample_voice_reader_mix_pitch_fwd_linear(segment,
                                                     gain,
                                                     fade_gain,
                                                     fade_count,
                                                     out_l,
                                                     out_r,
                                                     out_offset,
                                                     out_last_l,
                                                     out_last_r);
            break;
        case SAMPLE_KERNEL_PITCH_REV_LINEAR:
            sample_voice_reader_mix_pitch_rev_linear(segment,
                                                     gain,
                                                     fade_gain,
                                                     fade_count,
                                                     out_l,
                                                     out_r,
                                                     out_offset,
                                                     out_last_l,
                                                     out_last_r);
            break;
        case SAMPLE_KERNEL_FWD_1X:
        default:
            sample_voice_reader_mix_fwd_1x(segment,
                                           gain,
                                           fade_gain,
                                           fade_count,
                                           out_l,
                                           out_r,
                                           out_offset,
                                           out_last_l,
                                           out_last_r);
            break;
    }
}

static void brick6_sampler_render_sample_segment_cursor(brick6_sampler_voice_t *voice,
                                                        float *out_l,
                                                        float *out_r,
                                                        uint32_t frames)
{
    if ((voice == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
    {
        return;
    }

    const float render_gain = voice->gain * voice->trigger_velocity_gain;

    uint32_t produced = 0U;
    while (produced < frames)
    {
        sample_audio_segment_t segment;
        if ((sample_voice_reader_begin_segment(&voice->reader, frames - produced, &segment) == 0U)
            || (segment.status != SAMPLE_AUDIO_SEGMENT_OK) || (segment.frames == 0U))
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        BRICK6_SAMPLER_RUNTIME_DIAG_INC(segment_cursor_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);

        if ((voice->fade_in_frames == 0U) && (voice->fade_out_frames == 0U))
        {
            float last_l = 0.0f;
            float last_r = 0.0f;
            float fade_buf[AUDIO_BLOCK_SIZE];
            const float *fade_ptr = 0;
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
            brick6_sampler_runtime_mix_reader_segment(&segment,
                                                      render_gain,
                                                      fade_ptr,
                                                      fade_count,
                                                      out_l,
                                                      out_r,
                                                      produced,
                                                      &last_l,
                                                      &last_r);
            brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
        }
        else
        {
            float fade_buf[AUDIO_BLOCK_SIZE];
            float last_l = 0.0f;
            float last_r = 0.0f;
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
                    const float source_frame = segment.source_position + ((float)i * segment.source_step);
                    const uint32_t base_frame = (uint32_t)source_frame;
                    loop_pos = (base_frame > voice->region_begin) ? (base_frame - voice->region_begin) : 0U;
                }
                else if (segment.kernel_type == SAMPLE_KERNEL_PITCH_REV_LINEAR)
                {
                    const float source_frame = segment.source_position - ((float)i * segment.source_step);
                    const uint32_t base_frame = (uint32_t)source_frame;
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
            (void)brick6_sampler_runtime_apply_start_fade(voice, fade_buf, segment.frames);
            brick6_sampler_runtime_mix_reader_segment(&segment,
                                                      render_gain,
                                                      fade_buf,
                                                      segment.frames,
                                                      out_l,
                                                      out_r,
                                                      produced,
                                                      &last_l,
                                                      &last_r);
            brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
        }

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

static void brick6_sampler_render_multi(brick6_sampler_voice_t *voice,
                                        float *out_l,
                                        float *out_r,
                                        uint32_t frames)
{
    if ((voice == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U)
        || (voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_MULTI))
    {
        return;
    }


    if ((voice->multi_renderer == BRICK6_SAMPLER_MULTI_RENDER_STEREO_1X)
        && (voice->start_fade_remaining == 0U))
    {
        brick6_sampler_render_multi_stereo_1x_direct(voice, out_l, out_r, frames);
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
            brick6_sampler_runtime_multi_defer_stream_release(voice->multi_sample_id);
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
            brick6_sampler_runtime_multi_defer_stream_release(voice->multi_sample_id);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        if (segment.status != SAMPLE_AUDIO_SEGMENT_OK)
        {
            if (segment.status == SAMPLE_AUDIO_SEGMENT_UNDERRUN)
            {
                g_brick6_sampler_runtime_diag.multi_page_underrun++;
            }
            brick6_sampler_runtime_multi_diag_note_stop(
                voice,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN);
            brick6_sampler_runtime_multi_mark_voice_physical_release(voice);
            brick6_sampler_runtime_multi_defer_stream_release(voice->multi_sample_id);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        if (segment.frames == 0U)
        {
            g_brick6_sampler_runtime_diag.multi_page_underrun++;
            brick6_sampler_runtime_multi_diag_note_stop(
                voice,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN);
            brick6_sampler_runtime_multi_mark_voice_physical_release(voice);
            brick6_sampler_runtime_multi_defer_stream_release(voice->multi_sample_id);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        BRICK6_SAMPLER_RUNTIME_DIAG_INC(segment_cursor_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);

        float last_l = 0.0f;
        float last_r = 0.0f;
        float fade_buf[AUDIO_BLOCK_SIZE];
        const float *fade_ptr = 0;
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
        brick6_sampler_runtime_mix_reader_segment(&segment,
                                                  render_gain,
                                                  fade_ptr,
                                                  fade_count,
                                                  out_l,
                                                  out_r,
                                                  produced,
                                                  &last_l,
                                                  &last_r);
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);

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
            brick6_sampler_runtime_multi_defer_stream_release(voice->multi_sample_id);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }
    }

}

static void brick6_sampler_runtime_clip_render_shifter(brick6_sampler_voice_t *voice,
                                                       brick6_sampler_clip_runtime_t *clip,
                                                       brick6_sampler_clip_slot_t *slot,
                                                       float *out_l,
                                                       float *out_r,
                                                       uint32_t frames)
{
    if ((voice == NULL) || (clip == NULL) || (slot == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
    {
        return;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    memset(slot->stretch_render_l, 0, sizeof(slot->stretch_render_l));
    memset(slot->stretch_render_r, 0, sizeof(slot->stretch_render_r));

    brick6_sampler_runtime_clip_configure_shifter(clip, slot);
    brick6_sampler_render_sample_segment_cursor(voice,
                                                slot->stretch_render_l,
                                                slot->stretch_render_r,
                                                frames);
    brick6_clip_shifter_process_stereo(&slot->shifter,
                                       slot->stretch_render_l,
                                       slot->stretch_render_r,
                                       frames);

    for (uint32_t i = 0U; i < frames; ++i)
    {
        out_l[i] += slot->stretch_render_l[i];
        out_r[i] += slot->stretch_render_r[i];
    }
    if (frames != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice,
                                                 slot->stretch_render_l[frames - 1U],
                                                 slot->stretch_render_r[frames - 1U]);
    }
}

static uint8_t brick6_sampler_runtime_render_stream_fwd_1x_fast(
    brick6_sampler_voice_t *voice,
    float *out_l,
    float *out_r,
    uint32_t frames)
{
    if ((voice == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U)
        || (voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
        || (voice->use_segment_cursor == 0U)
        || (voice->reverse != 0U)
        || (voice->loop_mode != BRICK6_SAMPLER_LOOP_NONE)
        || (voice->fade_in_frames != 0U)
        || (voice->fade_out_frames != 0U)
        || (voice->start_fade_remaining != 0U)
        || (voice->play_plan.kernel_type != SAMPLE_KERNEL_FWD_1X)
        || (voice->play_plan.direction != 0U)
        || (voice->play_plan.loop_mode != (uint8_t)SAMPLE_PLAY_LOOP_NONE)
        || (voice->play_plan.step_q16 != BRICK6_SAMPLER_Q16_ONE)
        || (fabsf(voice->step_signed - 1.0f) > BRICK6_SAMPLER_STEP_EPSILON))
    {
        return 0U;
    }

    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    uint32_t produced = 0U;
    while (produced < frames)
    {
        uint32_t rendered = 0U;
        float last_l = 0.0f;
        float last_r = 0.0f;
        if (sample_voice_reader_render_fwd_1x_ready_simple(&voice->reader,
                                                           render_gain,
                                                           out_l,
                                                           out_r,
                                                           frames - produced,
                                                           produced,
                                                           &rendered,
                                                           &last_l,
                                                           &last_r) == 0U)
        {
            break;
        }
        if (rendered == 0U)
        {
            break;
        }

        BRICK6_SAMPLER_RUNTIME_DIAG_INC(fast_path_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
        produced += rendered;
        voice->position = voice->reader.position;
        if (voice->reader.active == 0U)
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }
    }

    if (produced == 0U)
    {
        return 0U;
    }
    if ((produced < frames) && (voice->active != 0U))
    {
        brick6_sampler_render_sample_segment_cursor(voice,
                                                    &out_l[produced],
                                                    &out_r[produced],
                                                    frames - produced);
    }
    return 1U;
}

static uint8_t brick6_sampler_runtime_render_stream_fwd_1x_fast_mono(
    brick6_sampler_voice_t *voice,
    float *out_mono,
    uint32_t frames)
{
    if ((voice == NULL) || (out_mono == NULL) || (frames == 0U)
        || (voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
        || (sample_audio_format_or_stereo(voice->format)
            != SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
        || (voice->use_segment_cursor == 0U)
        || (voice->reverse != 0U)
        || (voice->loop_mode != BRICK6_SAMPLER_LOOP_NONE)
        || (voice->fade_in_frames != 0U)
        || (voice->fade_out_frames != 0U)
        || (voice->start_fade_remaining != 0U)
        || (voice->play_plan.kernel_type != SAMPLE_KERNEL_FWD_1X)
        || (voice->play_plan.direction != 0U)
        || (voice->play_plan.loop_mode != (uint8_t)SAMPLE_PLAY_LOOP_NONE)
        || (voice->play_plan.step_q16 != BRICK6_SAMPLER_Q16_ONE)
        || (fabsf(voice->step_signed - 1.0f) > BRICK6_SAMPLER_STEP_EPSILON))
    {
        return 0U;
    }

    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    uint32_t produced = 0U;
    while (produced < frames)
    {
        uint32_t rendered = 0U;
        float last = 0.0f;
        if (sample_voice_reader_render_fwd_1x_ready_simple_mono(&voice->reader,
                                                                render_gain,
                                                                out_mono,
                                                                frames - produced,
                                                                produced,
                                                                &rendered,
                                                                &last) == 0U)
        {
            break;
        }
        if (rendered == 0U)
        {
            break;
        }

        BRICK6_SAMPLER_RUNTIME_DIAG_INC(fast_path_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);
        brick6_sampler_runtime_voice_note_output(voice, last, last);
        produced += rendered;
        voice->position = voice->reader.position;
        if (voice->reader.active == 0U)
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }
    }

    if (produced == 0U)
    {
        return 0U;
    }
    if ((produced < frames) && (voice->active != 0U))
    {
        brick6_sampler_render_sample_segment_cursor_mono(voice,
                                                         &out_mono[produced],
                                                         frames - produced);
    }
    return 1U;
}

static void brick6_sampler_render_sample_mono(brick6_sampler_voice_t *voice,
                                              float *out_mono,
                                              uint32_t frames)
{
    if ((voice == NULL) || (out_mono == NULL) || (frames == 0U)
        || (sample_audio_format_or_stereo(voice->format)
            != SAMPLE_AUDIO_FORMAT_FLOAT32_MONO))
    {
        return;
    }
    if ((voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
        || (voice->play_plan.region_end == 0U)
        || (voice->use_segment_cursor == 0U))
    {
        voice->active = 0U;
        voice->position = 0.0f;
        sample_voice_reader_stop(&voice->reader);
        return;
    }
    if (brick6_sampler_runtime_render_stream_fwd_1x_fast_mono(voice,
                                                               out_mono,
                                                               frames) == 0U)
    {
        brick6_sampler_render_sample_segment_cursor_mono(voice, out_mono, frames);
    }
    if (((voice->reverse == 0U) && (voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE)
         && (voice->reader.position >= (float)voice->region_end))
        || ((voice->reverse != 0U) && (voice->reader.active == 0U)))
    {
        voice->active = 0U;
        voice->position = 0.0f;
        sample_voice_reader_stop(&voice->reader);
    }
}

static ITCM_TEXT void brick6_sampler_render_sample(brick6_sampler_voice_t *voice,
                                         float *out_l,
                                         float *out_r,
                                         uint32_t frames)
{
    if ((voice == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
    {
        return;
    }

    const float render_gain = voice->gain * voice->trigger_velocity_gain;

    if ((voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
        || (voice->play_plan.region_end == 0U))
    {
        voice->active = 0U;
        voice->position = 0.0f;
        sample_voice_reader_stop(&voice->reader);
        return;
    }

    const uint32_t loop_begin = voice->region_begin;
    const uint32_t loop_end = voice->region_end;
    const uint32_t loop_frames = voice->loop_frames;
    const uint8_t has_fade = ((voice->fade_in_frames != 0U) || (voice->fade_out_frames != 0U)) ? 1U : 0U;
    uint32_t produced = 0U;

    if (voice->use_segment_cursor != 0U)
    {
        if (brick6_sampler_runtime_render_stream_fwd_1x_fast(voice, out_l, out_r, frames) != 0U)
        {
            return;
        }
        brick6_sampler_render_sample_segment_cursor(voice, out_l, out_r, frames);
        if (((voice->reverse == 0U) && (voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE)
             && (voice->reader.position >= (float)voice->region_end))
            || ((voice->reverse != 0U) && (voice->reader.active == 0U)))
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
        }
        return;
    }

    if ((voice->reverse != 0U) || (fabsf(voice->step_signed - 1.0f) > BRICK6_SAMPLER_STEP_EPSILON)
        || (voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG))
    {
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(slow_path_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);
        float fade_buf[AUDIO_BLOCK_SIZE];
        const float *fade_ptr = 0;
        uint32_t fade_count = 0U;
        uint8_t underrun = 0U;
        float last_l = 0.0f;
        float last_r = 0.0f;
        if (has_fade != 0U)
        {
            fade_ptr = fade_buf;
            fade_count = frames;
            for (uint32_t i = 0U; i < frames; ++i)
            {
                const uint32_t base_frame = (uint32_t)voice->reader.position;
                const uint32_t loop_pos = (voice->reverse != 0U)
                                              ? ((loop_end > 0U) && (base_frame < loop_end)
                                                     ? ((loop_end - 1U) - base_frame)
                                                     : 0U)
                                              : ((base_frame > loop_begin) ? (base_frame - loop_begin) : 0U);
                fade_buf[i] = brick6_sampler_runtime_fade_gain(loop_pos,
                                                               loop_frames,
                                                               voice->fade_in_frames,
                                                               voice->fade_out_frames);
            }
        }

        produced = sample_voice_reader_render_pitch_forward(&voice->reader,
                                                            loop_begin,
                                                            loop_end,
                                                            &voice->reverse,
                                                            voice->loop_mode,
                                                            render_gain,
                                                            fade_ptr,
                                                            fade_count,
                                                            out_l,
                                                            out_r,
                                                            frames,
                                                            &underrun,
                                                            &last_l,
                                                            &last_r);
        if (produced != 0U)
        {
            brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
        }
        voice->position = voice->reader.position;
        if ((underrun != 0U) || (produced < frames)
            || (brick6_sampler_runtime_is_terminal_position(voice, voice->reader.position) != 0U))
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
        }

        return;
    }

    uint32_t position = (uint32_t)voice->position;

    while (produced < frames)
    {
        if (position >= loop_end)
        {
            if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD) && (loop_end > loop_begin))
            {
                position = loop_begin;
                sample_voice_reader_seek(&voice->reader, loop_begin);
                continue;
            }

            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_commit_block(&voice->reader, 0U);
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        uint32_t request_frames = frames - produced;
        const uint32_t region_remaining = loop_end - position;
        if (request_frames > region_remaining)
        {
            request_frames = region_remaining;
        }

        sample_reader_block_t block;
        if ((sample_voice_reader_begin_block(&voice->reader, request_frames, &block) == 0U)
            || (block.status != SAMPLE_READER_BLOCK_OK)
            || (block.frames == 0U))
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_commit_block(&voice->reader, 0U);
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        BRICK6_SAMPLER_RUNTIME_DIAG_INC(fast_path_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);

        const float *src_l = block.l;
        const float *src_r = (block.is_mono != 0U) ? block.l : block.r;
        float last_l = 0.0f;
        float last_r = 0.0f;
        if (has_fade == 0U)
        {
            const float sample_gain = render_gain;
            if (block.is_mono != 0U)
            {
                for (uint32_t i = 0U; i < block.frames; ++i)
                {
                    const float sample_l = src_l[i * block.frame_step];
                    last_l = sample_l * sample_gain;
                    last_r = last_l;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < block.frames; ++i)
                {
                    last_l = src_l[i * block.frame_step] * sample_gain;
                    last_r = src_r[i * block.frame_step] * sample_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                }
            }
        }
        else
        {
            uint32_t loop_pos = position - loop_begin;
            for (uint32_t i = 0U; i < block.frames; ++i)
            {
                const float fade_gain = brick6_sampler_runtime_fade_gain(loop_pos + i,
                                                                         loop_frames,
                                                                         voice->fade_in_frames,
                                                                         voice->fade_out_frames);
                const float sample_gain = render_gain * fade_gain;
                const float sample_l = src_l[i * block.frame_step];
                const float sample_r = (block.is_mono != 0U)
                                           ? sample_l
                                           : src_r[i * block.frame_step];
                last_l = sample_l * sample_gain;
                last_r = sample_r * sample_gain;
                out_l[produced + i] += last_l;
                out_r[produced + i] += last_r;
            }
        }
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);

        sample_voice_reader_commit_block(&voice->reader, block.frames);
        produced += block.frames;
        position += block.frames;
        if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD) && (position >= loop_end)
            && (loop_end > loop_begin))
        {
            position = loop_begin;
            sample_voice_reader_seek(&voice->reader, loop_begin);
        }
    }

    voice->position = (float)position;
    if (brick6_sampler_runtime_is_terminal_position(voice, voice->position) != 0U)
    {
        voice->active = 0U;
        voice->position = 0.0f;
        sample_voice_reader_stop(&voice->reader);
    }
}

