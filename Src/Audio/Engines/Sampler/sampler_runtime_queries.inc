uint32_t brick6_sampler_runtime_render_track_mask(void)
{
    return g_sampler_render_track_mask;
}

uint8_t brick6_sampler_runtime_track_is_mono_native_ctx(
    const track_audio_runtime_ctx_t *ctx)
{
    if ((ctx == NULL)
        || (ctx->program_route.entity_id >= SEQ_TRACK_COUNT)
        || (ctx->program_route.active == 0U)
        || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER))
        return 0U;

    const uint8_t track_id = ctx->program_route.entity_id;
    if ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_STREAM)
    {
        const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
        const brick6_sampler_clip_runtime_t *const clip =
            &g_sampler_clip_runtime[track_id];
        const uint8_t shifter_slot_ready =
            ((clip->use_shifter_engine == 0U)
             || (brick6_sampler_runtime_clip_get_slot(track_id) != NULL));
        return (uint8_t)((clip->state
                    == (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING)
                && (shifter_slot_ready != 0U)
                && (voice->active != 0U)
                && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
                && (sample_audio_format_or_stereo(voice->format)
                    == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO));
    }

    if ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_MULTI)
    {
        const uint8_t voice_limit =
            brick6_sampler_runtime_multi_spread_voice_count(track_id);
        if ((voice_limit > 1U)
            && (g_sampler_multi_track_state[track_id].spread > 0.0f))
            return 0U;
        uint8_t active_voice = 0U;
        for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
        {
            const brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
            if ((brick6_sampler_runtime_multi_voice_occupied(voice) == 0U)
                || (voice->owner_track_id != track_id)) continue;
            active_voice = 1U;
            if (sample_audio_format_or_stereo(voice->play_plan.format)
                != SAMPLE_AUDIO_FORMAT_FLOAT32_MONO) return 0U;
        }
        return active_voice;
    }
    return 0U;
}

uint8_t brick6_sampler_runtime_track_is_mono_native(uint8_t track_id)
{
    track_audio_runtime_ctx_t ctx;
    return (audio_note_engine_adapter_current_ctx(track_id, &ctx) != 0U)
        ? brick6_sampler_runtime_track_is_mono_native_ctx(&ctx) : 0U;
}

static uint8_t brick6_sampler_runtime_multi_voice_format_compatible(
    const brick6_sampler_voice_t *voice)
{
    return (uint8_t)((voice != NULL)
        && (sample_audio_format_is_valid(voice->play_plan.format) != 0U)
        && (voice->play_plan.stride_floats
            == sample_audio_format_stride_floats(voice->play_plan.format))
        && (voice->play_plan.frames_per_page
            == sample_audio_format_frames_per_page(voice->play_plan.format)));
}
