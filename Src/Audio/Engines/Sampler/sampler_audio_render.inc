void brick6_sampler_runtime_render_stream_track(const track_audio_runtime_ctx_t *ctx,
                                                float *out_l,
                                                float *out_r,
                                                uint32_t frames)
{
    if ((ctx == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
    {
        return;
    }

    if ((ctx->program_route.entity_id >= SEQ_TRACK_COUNT)
            || (ctx->program_route.active == 0U)
            || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
            || ((track_runtime_type_t)ctx->type != TRACK_RUNTIME_TYPE_STREAM))
    {
        return;
    }

    brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[ctx->program_route.entity_id];
    brick6_sampler_clip_slot_t *const slot = brick6_sampler_runtime_clip_get_slot(ctx->program_route.entity_id);
    brick6_sampler_voice_t *const voice = &g_sampler_voice[ctx->program_route.entity_id];

    if (clip->state != (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING)
    {
        if ((voice->active != 0U) || (voice->reader.active != 0U) || (voice->release_pending != 0U))
        {
            brick6_sampler_runtime_clip_stop_playback(ctx->program_route.entity_id);
        }
        brick6_sampler_runtime_mix_declick_tails(ctx->program_route.entity_id, out_l, out_r, frames);
        return;
    }

    if ((voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
            || (voice->active == 0U))
    {
        brick6_sampler_runtime_clip_stop_playback(ctx->program_route.entity_id);
        clip->state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
        brick6_sampler_runtime_mix_declick_tails(ctx->program_route.entity_id, out_l, out_r, frames);
        return;
    }

    if ((voice->release_pending != 0U)
            && (mixer_track_vca_requires_source(ctx->program_route.mix_track_id) == 0U))
    {
        brick6_sampler_runtime_clip_stop_playback_silent(ctx->program_route.entity_id);
        brick6_sampler_runtime_mix_declick_tails(ctx->program_route.entity_id, out_l, out_r, frames);
        return;
    }

    if (clip->use_shifter_engine != 0U)
    {
        if (slot == NULL)
        {
            clip->use_shifter_engine = 0U;
            brick6_sampler_render_sample(voice, out_l, out_r, frames);
        }
        else
        {
            brick6_sampler_runtime_clip_render_shifter(voice, clip, slot, out_l, out_r, frames);
        }
    }
    else
    {
        brick6_sampler_render_sample(voice, out_l, out_r, frames);
    }
    if (voice->active == 0U)
    {
        clip->state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
        voice->release_pending = 0U;
        voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        brick6_sampler_runtime_clip_release_slot(ctx->program_route.entity_id);
    }
    brick6_sampler_runtime_mix_declick_tails(ctx->program_route.entity_id, out_l, out_r, frames);
}

void brick6_sampler_runtime_render_stream_track_mono(const track_audio_runtime_ctx_t *ctx,
                                                     float *out_mono,
                                                     uint32_t frames)
{
    if ((ctx == NULL) || (out_mono == NULL) || (frames == 0U)
        || (frames > AUDIO_BLOCK_SIZE))
    {
        return;
    }
    const uint8_t track_id = ctx->program_route.entity_id;
    if ((track_id >= (uint8_t)(sizeof(g_sampler_clip_runtime)
                               / sizeof(g_sampler_clip_runtime[0])))
        || (ctx->program_route.active == 0U)
        || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
        || ((track_runtime_type_t)ctx->type != TRACK_RUNTIME_TYPE_STREAM)
        || (brick6_sampler_runtime_track_is_mono_native_ctx(ctx) == 0U))
    {
        return;
    }

    brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    if (clip->state != (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING)
    {
        if ((voice->active != 0U) || (voice->reader.active != 0U)
            || (voice->release_pending != 0U))
        {
            brick6_sampler_runtime_clip_stop_playback(track_id);
        }
        brick6_sampler_runtime_mix_declick_tails_mono(track_id, out_mono, frames);
        return;
    }
    if ((voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
            || (voice->active == 0U))
    {
        brick6_sampler_runtime_clip_stop_playback(track_id);
        clip->state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
        brick6_sampler_runtime_mix_declick_tails_mono(track_id, out_mono, frames);
        return;
    }
    if ((voice->release_pending != 0U)
        && (mixer_track_vca_requires_source(ctx->program_route.mix_track_id) == 0U))
    {
        brick6_sampler_runtime_clip_stop_playback_silent(track_id);
        brick6_sampler_runtime_mix_declick_tails_mono(track_id, out_mono, frames);
        return;
    }

    if (clip->use_shifter_engine != 0U)
    {
        brick6_sampler_clip_slot_t *const slot = brick6_sampler_runtime_clip_get_slot(track_id);
        if (slot == NULL)
        {
            brick6_sampler_runtime_clip_stop_playback(track_id);
            clip->state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
            brick6_sampler_runtime_mix_declick_tails_mono(track_id, out_mono, frames);
            return;
        }
        brick6_sampler_runtime_clip_render_shifter_mono(voice,
                                                        clip,
                                                        slot,
                                                        out_mono,
                                                        frames);
    }
    else
    {
        brick6_sampler_render_sample_mono(voice, out_mono, frames);
    }
    if (voice->active == 0U)
    {
        clip->state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
        voice->release_pending = 0U;
        voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        brick6_sampler_runtime_clip_release_slot(track_id);
    }
    brick6_sampler_runtime_mix_declick_tails_mono(track_id, out_mono, frames);
}

void brick6_sampler_runtime_render_multi_track(const track_audio_runtime_ctx_t *ctx,
                                               float *out_l,
                                               float *out_r,
                                               uint32_t frames)
{
    if ((ctx == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
    {
        return;
    }

    if ((ctx->program_route.entity_id >= SEQ_TRACK_COUNT)
            || (ctx->program_route.active == 0U)
            || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
            || ((track_runtime_type_t)ctx->type != TRACK_RUNTIME_TYPE_MULTI))
    {
        return;
    }

    float voice_l[AUDIO_BLOCK_SIZE];
    float voice_r[AUDIO_BLOCK_SIZE];
    float vca_gain[AUDIO_BLOCK_SIZE];
    const uint8_t poly_lfo_active = (mod_matrix_poly_route_mask(ctx->program_route.entity_id) != 0U);
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const multi_voice = &g_sampler_multi_voice[i];
        if ((multi_voice->owner_track_id == ctx->program_route.entity_id)
            && ((multi_voice->active != 0U)
                || (multi_voice->dsp_slot != MULTI_VOICE_DSP_SLOT_INDEX_INVALID)))
        {
            multi_voice_dsp_slot_t *const dsp_state =
                multi_voice_dsp_get(multi_voice->dsp_slot);
            if (dsp_state == NULL)
            {
                brick6_sampler_runtime_multi_stop_voice(
                    multi_voice,
                    (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_REL_DONE);
                continue;
            }
            if (brick6_sampler_runtime_multi_voice_format_compatible(multi_voice) == 0U)
            {
                brick6_sampler_runtime_multi_stop_voice(
                    multi_voice,
                    (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_REL_DONE);
                continue;
            }
            if (poly_lfo_active != 0U)
            {
                mixer_multi_filter_prepare_voice_block(ctx->program_route.mix_track_id, dsp_state);
                mod_lfo_v1_process_poly_voice(ctx->program_route.entity_id,
                    (uint8_t)(SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET + i), ctx, frames);
            }
            if (dsp_state->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_MONO)
            {
                memset(voice_l, 0, frames * sizeof(float));
                brick6_sampler_render_multi_mono(multi_voice, voice_l, frames);
                if (poly_lfo_active != 0U)
                    mixer_multi_filter_process_mono_prepared(ctx->program_route.mix_track_id, dsp_state, voice_l, frames);
                else
                    mixer_multi_filter_process_mono(ctx->program_route.mix_track_id, dsp_state, voice_l, frames);
                float pan_l = 1.0f;
                float pan_r = 1.0f;
                brick6_sampler_runtime_multi_get_spread_pan(multi_voice,
                                                            &pan_l,
                                                            &pan_r);
                uint32_t vca_frames = frames;
                if (dsp_state->vca_enabled != 0U)
                {
                    vca_frames = env_adsr_process_vca_block(&dsp_state->vca_env,
                                                            vca_gain,
                                                            frames);
                }
                for (uint32_t frame = 0U; frame < vca_frames; ++frame)
                {
                    const float vca = (dsp_state->vca_enabled != 0U)
                        ? vca_gain[frame] : 1.0f;
                    out_l[frame] += voice_l[frame] * vca * pan_l;
                    out_r[frame] += voice_l[frame] * vca * pan_r;
                }
            }
            else
            {
                memset(voice_l, 0, frames * sizeof(float));
                memset(voice_r, 0, frames * sizeof(float));
                brick6_sampler_render_multi(multi_voice, voice_l, voice_r, frames);
                if (poly_lfo_active != 0U)
                    mixer_multi_filter_process_prepared(ctx->program_route.mix_track_id, dsp_state, voice_l, voice_r, frames);
                else
                    mixer_multi_filter_process(ctx->program_route.mix_track_id, dsp_state, voice_l, voice_r, frames);
                float pan_l = 1.0f;
                float pan_r = 1.0f;
                brick6_sampler_runtime_multi_get_spread_pan(multi_voice,
                                                            &pan_l,
                                                            &pan_r);
                uint32_t vca_frames = frames;
                if (dsp_state->vca_enabled != 0U)
                {
                    vca_frames = env_adsr_process_vca_block(&dsp_state->vca_env,
                                                            vca_gain,
                                                            frames);
                }
                for (uint32_t frame = 0U; frame < vca_frames; ++frame)
                {
                    const float vca = (dsp_state->vca_enabled != 0U)
                        ? vca_gain[frame] : 1.0f;
                    out_l[frame] += voice_l[frame] * vca * pan_l;
                    out_r[frame] += voice_r[frame] * vca * pan_r;
                }
            }
            if (mixer_multi_voice_vca_requires_source(dsp_state) == 0U)
            {
                brick6_sampler_runtime_multi_stop_voice_after_vca(multi_voice);
            }
        }
    }

    brick6_sampler_runtime_mix_declick_tails(ctx->program_route.entity_id, out_l, out_r, frames);
}

multi_voice_dsp_slot_t *brick6_sampler_runtime_get_multi_voice_dsp(uint8_t voice_index)
{
    if (voice_index >= SAMPLER_MULTI_MAX_GLOBAL_VOICES)
    {
        return NULL;
    }
    const brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[voice_index];
    if ((voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
            || (voice->dsp_slot == MULTI_VOICE_DSP_SLOT_INDEX_INVALID))
    {
        return NULL;
    }
    return multi_voice_dsp_get(voice->dsp_slot);
}

void brick6_sampler_runtime_render_multi_track_mono(const track_audio_runtime_ctx_t *ctx,
                                                    float *out_mono,
                                                    uint32_t frames)
{
    if ((ctx == NULL) || (out_mono == NULL) || (frames == 0U)
        || (frames > AUDIO_BLOCK_SIZE)
        || (ctx->program_route.entity_id >= SEQ_TRACK_COUNT)
        || (ctx->program_route.active == 0U)
        || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
        || ((track_runtime_type_t)ctx->type != TRACK_RUNTIME_TYPE_MULTI)
        || (brick6_sampler_runtime_track_is_mono_native_ctx(ctx) == 0U))
    {
        return;
    }

    float voice_mono[AUDIO_BLOCK_SIZE];
    float vca_gain[AUDIO_BLOCK_SIZE];
    const uint8_t poly_lfo_active = (mod_matrix_poly_route_mask(ctx->program_route.entity_id) != 0U);
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const multi_voice = &g_sampler_multi_voice[i];
        if ((multi_voice->owner_track_id != ctx->program_route.entity_id)
            || ((multi_voice->active == 0U)
                && (multi_voice->dsp_slot == MULTI_VOICE_DSP_SLOT_INDEX_INVALID)))
        {
            continue;
        }

        multi_voice_dsp_slot_t *const dsp_state =
            multi_voice_dsp_get(multi_voice->dsp_slot);
        if ((dsp_state == NULL)
            || (dsp_state->format != (uint8_t)MULTI_VOICE_DSP_FORMAT_MONO)
            || (brick6_sampler_runtime_multi_voice_format_compatible(multi_voice) == 0U))
        {
            brick6_sampler_runtime_multi_stop_voice(
                multi_voice,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_REL_DONE);
            continue;
        }

        if (poly_lfo_active != 0U)
        {
            mixer_multi_filter_prepare_voice_block(ctx->program_route.mix_track_id, dsp_state);
            mod_lfo_v1_process_poly_voice(ctx->program_route.entity_id,
                (uint8_t)(SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET + i), ctx, frames);
        }

        memset(voice_mono, 0, frames * sizeof(float));
        brick6_sampler_render_multi_mono(multi_voice, voice_mono, frames);
        if (poly_lfo_active != 0U)
            mixer_multi_filter_process_mono_prepared(ctx->program_route.mix_track_id, dsp_state, voice_mono, frames);
        else
            mixer_multi_filter_process_mono(ctx->program_route.mix_track_id, dsp_state, voice_mono, frames);
        uint32_t vca_frames = frames;
        if (dsp_state->vca_enabled != 0U)
        {
            vca_frames = env_adsr_process_vca_block(&dsp_state->vca_env,
                                                    vca_gain,
                                                    frames);
        }
        for (uint32_t frame = 0U; frame < vca_frames; ++frame)
        {
            const float vca = (dsp_state->vca_enabled != 0U)
                ? vca_gain[frame] : 1.0f;
            out_mono[frame] += voice_mono[frame] * vca;
        }

        if (mixer_multi_voice_vca_requires_source(dsp_state) == 0U)
        {
            brick6_sampler_runtime_multi_stop_voice_after_vca(multi_voice);
        }
    }

    brick6_sampler_runtime_mix_declick_tails_mono(ctx->program_route.entity_id, out_mono, frames);
}

void brick6_sampler_runtime_render_track(const track_audio_runtime_ctx_t *ctx,
                                         float *out_l,
                                         float *out_r,
                                         uint32_t frames)
{
    if ((ctx == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
    {
        return;
    }

    BRICK6_SAMPLER_RUNTIME_DIAG_INC(render_track_calls);

    if ((ctx->program_route.entity_id >= SEQ_TRACK_COUNT)
            || (ctx->program_route.active == 0U)
            || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER))
    {
        return;
    }

    switch ((track_runtime_type_t)ctx->type)
    {
        case TRACK_RUNTIME_TYPE_STREAM:
            brick6_sampler_runtime_render_stream_track(ctx, out_l, out_r, frames);
            return;

        case TRACK_RUNTIME_TYPE_MULTI:
            brick6_sampler_runtime_render_multi_track(ctx, out_l, out_r, frames);
            return;

        default:
            break;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[ctx->program_route.entity_id];
    if ((voice->active != 0U)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM))
    {
        brick6_sampler_runtime_render_ram_track(ctx, out_l, out_r, frames);
        return;
    }

    if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_CLASSIC_CAPACITY))
    {
        brick6_sampler_runtime_mix_declick_tails(ctx->program_route.entity_id, out_l, out_r, frames);
        return;
    }

    if ((voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
            || (voice->play_plan.region_end == 0U))
    {
        voice->active = 0U;
        voice->position = 0U;
        sample_voice_reader_stop(&voice->reader);
        brick6_sampler_runtime_mix_declick_tails(ctx->program_route.entity_id, out_l, out_r, frames);
        return;
    }

    brick6_sampler_render_sample(voice, out_l, out_r, frames);
    brick6_sampler_runtime_mix_declick_tails(ctx->program_route.entity_id, out_l, out_r, frames);
}
