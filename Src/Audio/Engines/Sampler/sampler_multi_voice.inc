static void brick6_sampler_runtime_multi_stop_voice(brick6_sampler_voice_t *voice, uint8_t reason)
{
    if (voice == NULL)
    {
        return;
    }

    const uint8_t stopped_track_id = voice->owner_track_id;
    const uint8_t stopped_voice_index = brick6_sampler_runtime_multi_voice_index(voice);
    const uint16_t stopped_multi_sample_id = voice->multi_sample_id;
    /* Normal Note Off already made this a no-op; EOF, steal and forced stops
     * also close a still-held per-voice VCA gate and cannot leave it stale. */
    brick6_sampler_runtime_multi_release_voice_vca(voice);
    if ((reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NONE)
        || (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_STEAL)
        || (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_REL_DONE))
    {
        brick6_sampler_runtime_begin_declick_tail(stopped_track_id, voice);
    }

    if (__get_IPSR() != 0U)
    {
        brick6_sampler_runtime_multi_mark_voice_physical_release(voice);
    }

    if ((reason != (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NONE)
        && (voice->active != 0U)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_MULTI))
    {
        brick6_sampler_runtime_multi_diag_note_stop(voice, reason);
    }

    sample_voice_reader_stop(&voice->reader);
    voice->active = 0U;
    brick6_sampler_runtime_multi_release_dsp_slot(voice);
    voice->spread_index = BRICK6_SAMPLER_MULTI_VOICE_INDEX_INVALID;
    voice->position = 0.0f;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->owner_track_id = UINT8_MAX;
    voice->release_pending = 0U;
    voice->vca_note_released = 0U;
    voice->output_id = 0U;
    if (stopped_voice_index < SAMPLER_MULTI_MAX_GLOBAL_VOICES)
    {
        mod_lfo_v1_poly_voice_reset((uint8_t)(SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET
                                              + stopped_voice_index));
    }
    voice->trigger_order = 0U;

    brick6_sampler_runtime_multi_defer_stream_release(stopped_multi_sample_id);
}

static void brick6_sampler_runtime_multi_stop_voice_after_vca(brick6_sampler_voice_t *voice)
{
    if (voice != NULL)
    {
        voice->last_out_valid = 0U;
        voice->last_out_l = 0.0f;
        voice->last_out_r = 0.0f;
    }
    brick6_sampler_runtime_multi_stop_voice(
        voice,
        (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_REL_DONE);
}

static void brick6_sampler_runtime_multi_defer_stream_release(uint16_t multi_sample_id)
{
    if (multi_sample_id < MULTI_SAMPLE_POOL_MAX_SAMPLES)
    {
        g_sampler_multi_stream_release_pending[multi_sample_id] = 1U;
    }
}

static void brick6_sampler_runtime_multi_stop_track_internal(
    uint8_t track_id, uint8_t close_common_gate)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        if (g_sampler_multi_voice[i].owner_track_id == track_id)
        {
            brick6_sampler_runtime_multi_stop_voice(&g_sampler_multi_voice[i],
                                                    (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NONE);
        }
    }

    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track_id, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    if ((close_common_gate != 0U)
            && (ctx != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI)
            && (ctx->program_route.mix_track_id < MIXER_MAX_TRACKS))
    {
        mixer_track_vca_all_notes_off(ctx->program_route.mix_track_id);
    }
}

static void brick6_sampler_runtime_multi_stop_track(uint8_t track_id)
{
    brick6_sampler_runtime_multi_stop_track_internal(track_id, 1U);
}

static void brick6_sampler_runtime_multi_stop_track_renderer(uint8_t track_id)
{
    brick6_sampler_runtime_multi_stop_track_internal(track_id, 0U);
}

static void brick6_sampler_runtime_multi_service_physical_releases(void)
{
    if (__get_IPSR() != 0U)
    {
        return;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if (voice->release_pending == 0U)
        {
            continue;
        }

        if (voice->active == 0U)
        {
            const multi_voice_dsp_slot_t *const dsp_state =
                multi_voice_dsp_get_const(voice->dsp_slot);
            if (mixer_multi_voice_vca_requires_source(dsp_state) == 0U)
            {
                brick6_sampler_runtime_multi_release_dsp_slot(voice);
                voice->spread_index = BRICK6_SAMPLER_MULTI_VOICE_INDEX_INVALID;
                voice->trigger_order = 0U;
                voice->release_pending = 0U;
                voice->vca_note_released = 0U;
                voice->output_id = 0U;
                voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
                voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
                voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
                voice->owner_track_id = UINT8_MAX;
            }
        }
    }
}

void brick6_sampler_runtime_service_physical_releases(void)
{
    brick6_sampler_runtime_multi_service_physical_releases();
}

static uint8_t brick6_sampler_runtime_multi_sample_has_active_voice(uint16_t multi_sample_id)
{
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        const brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if ((voice->active != 0U)
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
            && (voice->multi_sample_id == multi_sample_id))
        {
            return 1U;
        }
    }

    return 0U;
}

static void brick6_sampler_runtime_multi_service_stream_releases(void)
{
    if (__get_IPSR() != 0U)
    {
        return;
    }

    for (uint16_t multi_sample_id = 0U;
         multi_sample_id < MULTI_SAMPLE_POOL_MAX_SAMPLES;
         ++multi_sample_id)
    {
        if (g_sampler_multi_stream_release_pending[multi_sample_id] == 0U)
        {
            continue;
        }

        if (brick6_sampler_runtime_multi_sample_has_active_voice(multi_sample_id) != 0U)
        {
            continue;
        }

        g_sampler_multi_stream_release_pending[multi_sample_id] = 0U;
    }
}

void brick6_sampler_runtime_service(void)
{
    brick6_sampler_runtime_multi_service_physical_releases();
    brick6_sampler_runtime_multi_service_stream_releases();
}

static uint8_t brick6_sampler_runtime_multi_voice_occupied(
    const brick6_sampler_voice_t *voice)
{
    return ((voice != NULL)
            && ((voice->active != 0U)
                || (voice->dsp_slot != MULTI_VOICE_DSP_SLOT_INDEX_INVALID))
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)) ? 1U : 0U;
}

static void brick6_sampler_runtime_multi_assign_spread_index(
    brick6_sampler_voice_t *voice,
    uint8_t track_id)
{
    uint8_t used[SAMPLER_MULTI_MAX_VOICES_PER_TRACK] = { 0U };
    if ((voice == NULL) || (track_id >= SEQ_TRACK_COUNT))
    {
        return;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        const brick6_sampler_voice_t *const candidate = &g_sampler_multi_voice[i];
        if ((candidate == voice)
            || (brick6_sampler_runtime_multi_voice_occupied(candidate) == 0U)
            || (candidate->owner_track_id != track_id)
            || (candidate->spread_index >= SAMPLER_MULTI_MAX_VOICES_PER_TRACK))
        {
            continue;
        }
        used[candidate->spread_index] = 1U;
    }

    voice->spread_index = 0U;
    while ((voice->spread_index < SAMPLER_MULTI_MAX_VOICES_PER_TRACK)
           && (used[voice->spread_index] != 0U))
    {
        voice->spread_index++;
    }
    if (voice->spread_index >= SAMPLER_MULTI_MAX_VOICES_PER_TRACK)
    {
        voice->spread_index = 0U;
    }
}

static void brick6_sampler_runtime_multi_reindex_spread(uint8_t track_id)
{
    uint8_t assigned[SAMPLER_MULTI_MAX_GLOBAL_VOICES] = { 0U };
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    const uint8_t count = brick6_sampler_runtime_multi_spread_voice_count(track_id);
    for (uint8_t rank = 0U; rank < count; ++rank)
    {
        brick6_sampler_voice_t *oldest = NULL;
        uint8_t oldest_index = 0U;
        for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
        {
            brick6_sampler_voice_t *const candidate = &g_sampler_multi_voice[i];
            if ((assigned[i] != 0U)
                || (brick6_sampler_runtime_multi_voice_occupied(candidate) == 0U)
                || (candidate->owner_track_id != track_id))
            {
                continue;
            }
            if ((oldest == NULL) || (candidate->trigger_order < oldest->trigger_order))
            {
                oldest = candidate;
                oldest_index = i;
            }
        }
        if (oldest == NULL)
        {
            break;
        }
        oldest->spread_index = rank;
        assigned[oldest_index] = 1U;
    }
}

static void brick6_sampler_runtime_multi_get_spread_pan(
    const brick6_sampler_voice_t *voice,
    float *out_pan_l,
    float *out_pan_r)
{
    if ((out_pan_l == NULL) || (out_pan_r == NULL))
    {
        return;
    }

    *out_pan_l = 1.0f;
    *out_pan_r = 1.0f;
    if ((voice == NULL) || (voice->owner_track_id >= SEQ_TRACK_COUNT))
    {
        return;
    }

    const uint8_t count = brick6_sampler_runtime_multi_spread_voice_count(voice->owner_track_id);
    const float spread = g_sampler_multi_track_state[voice->owner_track_id].spread;
    if ((count > 1U) && (spread > 0.0f) && (voice->spread_index < count))
    {
        const float position = ((((float)voice->spread_index / (float)(count - 1U))
                                 * 2.0f) - 1.0f) * spread;
        const float pan_for_mix = -position;
        *out_pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
        *out_pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
    }
}

static uint32_t brick6_sampler_runtime_multi_active_count(void)
{
    uint32_t active = 0U;
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        const brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if (((voice->active != 0U)
             || (voice->dsp_slot != MULTI_VOICE_DSP_SLOT_INDEX_INVALID))
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_MULTI))
        {
            active++;
        }
    }
    return active;
}

static uint32_t brick6_sampler_runtime_multi_active_count_for_track(uint8_t track_id)
{
    uint32_t active = 0U;
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        const brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if (((voice->active != 0U)
             || (voice->dsp_slot != MULTI_VOICE_DSP_SLOT_INDEX_INVALID))
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
            && (voice->owner_track_id == track_id))
        {
            active++;
        }
    }
    return active;
}

static uint8_t brick6_sampler_runtime_multi_spread_voice_count(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 1U;
    }

    uint8_t count = g_sampler_multi_track_state[track_id].voice_count;
    if (count < 1U)
    {
        count = 1U;
    }
    if (count > SAMPLER_MULTI_MAX_VOICES_PER_TRACK)
    {
        count = SAMPLER_MULTI_MAX_VOICES_PER_TRACK;
    }
    return count;
}

void brick6_sampler_runtime_set_multi_voice_count(uint8_t track_id, uint8_t count)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (count < 1U)
    {
        count = 1U;
    }
    if (count > SAMPLER_MULTI_MAX_VOICES_PER_TRACK)
    {
        count = SAMPLER_MULTI_MAX_VOICES_PER_TRACK;
    }
    g_sampler_multi_track_state[track_id].voice_count = count;

    brick6_sampler_runtime_multi_reindex_spread(track_id);
}

uint8_t brick6_sampler_runtime_get_multi_voice_count(uint8_t track_id)
{
    return brick6_sampler_runtime_multi_spread_voice_count(track_id);
}

void brick6_sampler_runtime_set_multi_spread(uint8_t track_id, float spread)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    if (spread < 0.0f)
    {
        spread = 0.0f;
    }
    else if (spread > 1.0f)
    {
        spread = 1.0f;
    }
    g_sampler_multi_track_state[track_id].spread = spread;
}

float brick6_sampler_runtime_get_multi_spread(uint8_t track_id)
{
    return (track_id < SEQ_TRACK_COUNT)
        ? g_sampler_multi_track_state[track_id].spread : 0.0f;
}

static brick6_sampler_voice_t *brick6_sampler_runtime_multi_alloc_voice(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return NULL;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if ((voice->active == 0U)
            && (voice->dsp_slot == MULTI_VOICE_DSP_SLOT_INDEX_INVALID))
        {
            return voice;
        }
    }

    /* A STOP already accepted by CONTROL may still have a physical release
     * tail. Reclaim only that stopped resource; this is slot maintenance,
     * never musical victim selection. */
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if ((voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
                && (voice->release_pending != 0U))
        {
            brick6_sampler_runtime_multi_stop_voice(
                voice, (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NONE);
            return voice;
        }
    }

    /* CONTROL's published START contract makes this unreachable in a legal
     * workload. A non-free physical slot is an invariant violation. */
    return NULL;
}

static uint8_t brick6_sampler_runtime_start_multi_renderer(uint8_t track_id,
                                                           uint16_t instrument_id,
                                                           uint8_t note,
                                                           uint8_t velocity,
                                                           float gain,
                                                           uint32_t output_id,
                                                           uint8_t held)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (note > 127U) || (velocity > 127U)
        || (brick6_sampler_runtime_track_is_clip(track_id) != 0U))
    {
        return 0U;
    }

    if (velocity == 0U)
    {
        if (output_id == 0U)
        {
            brick6_sampler_runtime_note_off_multi_track_note_all(track_id, note);
        }
        else
        {
            brick6_sampler_runtime_note_off_multi_track_note_output(track_id,
                                                                    note,
                                                                    output_id);
        }
        return 1U;
    }

    if (instrument_id == MULTI_SAMPLE_POOL_INVALID_ID)
    {
        instrument_id = g_sampler_multi_track_state[track_id].instrument_id;
        if (instrument_id == MULTI_SAMPLE_POOL_INVALID_ID)
        {
            g_brick6_sampler_runtime_diag.multi_no_instrument_assigned++;
            g_brick6_sampler_runtime_diag.multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
            brick6_sampler_runtime_multi_diag_note_reject(
                track_id,
                note,
                velocity,
                MULTI_SAMPLE_POOL_INVALID_ID,
                MULTI_SAMPLE_POOL_INVALID_ID,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NO_INSTRUMENT);
            return 0U;
        }
    }
    if (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
    {
        g_brick6_sampler_runtime_diag.multi_invalid_instrument_id++;
        g_brick6_sampler_runtime_diag.multi_instrument_id = instrument_id;
        brick6_sampler_runtime_multi_diag_note_reject(
            track_id,
            note,
            velocity,
            instrument_id,
            MULTI_SAMPLE_POOL_INVALID_ID,
            (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_INVALID_SAMPLE);
        return 0U;
    }

    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track_id, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    if ((ctx == NULL) || (ctx->program_route.active == 0U)
        || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER))
    {
        return 0U;
    }

    multi_sample_audio_source_t resolved;
    if (multi_sample_audio_projection_resolve(instrument_id, note, velocity, &resolved) == 0U)
    {
        g_brick6_sampler_runtime_diag.multi_resolve_fail++;
        g_brick6_sampler_runtime_diag.multi_instrument_id = instrument_id;
        g_brick6_sampler_runtime_diag.note = note;
        g_brick6_sampler_runtime_diag.velocity = velocity;
        brick6_sampler_runtime_multi_diag_note_reject(
            track_id,
            note,
            velocity,
            instrument_id,
            MULTI_SAMPLE_POOL_INVALID_ID,
            (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NO_ZONE);
        return 0U;
    }

    if (resolved.total_frames == 0U)
    {
        g_brick6_sampler_runtime_diag.multi_resolve_fail++;
        brick6_sampler_runtime_multi_diag_note_reject(
            track_id,
            note,
            velocity,
            instrument_id,
            resolved.multi_sample_id,
            (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_INVALID_SAMPLE);
        return 0U;
    }

    const sample_audio_key_t key = brick6_sampler_runtime_multi_key(resolved.multi_sample_id);
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    if ((voice->active != 0U)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_CLIP))
    {
        return 0U;
    }

    uint8_t local_start_fade = 0U;
    if (voice->active != 0U)
    {
        local_start_fade = brick6_sampler_runtime_begin_declick_tail(track_id, voice);
        sample_voice_reader_stop(&voice->reader);
        voice->active = 0U;
        voice->position = 0.0f;
        voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        voice->ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
        voice->ram_generation = 0U;
        voice->ram_data = NULL;
        voice->ram_channels = 0U;
        voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
    }

    brick6_sampler_voice_t *const multi_voice =
        brick6_sampler_runtime_multi_alloc_voice(track_id);
    if (multi_voice == NULL)
        return 0U;
    sample_play_plan_t play_plan;
    memset(&play_plan, 0, sizeof(play_plan));
    play_plan.key = key;
    play_plan.sample_id = resolved.multi_sample_id;
    play_plan.start_frame = 0U;
    play_plan.region_begin = 0U;
    play_plan.region_end = resolved.total_frames;
    play_plan.loop_begin = 0U;
    play_plan.loop_end = resolved.total_frames;
    const float ratio = brick6_sampler_runtime_pitch_ratio(
        (float)resolved.pitch_semitones);
    play_plan.step_q16 = brick6_sampler_runtime_ratio_to_q16(ratio);
    play_plan.direction = 0U;
    play_plan.loop_mode = BRICK6_SAMPLER_LOOP_NONE;
    play_plan.stop_on_underrun = 1U;
    play_plan.kernel_type = (play_plan.step_q16 == BRICK6_SAMPLER_Q16_ONE)
                                ? SAMPLE_KERNEL_FWD_1X
                                : SAMPLE_KERNEL_PITCH_FWD_LINEAR;
    const float multi_trigger_velocity_gain =
        (resolved.zone_is_single_velocity_layer != 0U)
            ? brick6_sampler_runtime_velocity_gain(velocity)
            : 1.0f;
    const float expected_render_gain =
        g_sampler_multi_track_state[track_id].gain * gain * multi_trigger_velocity_gain;
    sample_resolved_source_t common_source;
    sample_play_plan_t common_plan;
    const brick6_sample_common_trigger_t common_trigger = {
        .kind = BRICK6_SAMPLE_COMMON_TRIGGER_MULTI,
        .runtime_plan = &play_plan,
        .multi_source = &resolved,
        .total_frames = resolved.total_frames,
        .step_q16 = play_plan.step_q16,
        .render_gain = expected_render_gain,
        .instrument_id = instrument_id,
        .track_id = track_id,
        .note = note,
        .velocity = velocity,
    };
    const brick6_sample_common_plan_result_t common_result =
        brick6_sampler_runtime_build_common_play_plan(&common_trigger,
                                                      &common_source,
                                                      &common_plan);
    brick6_sampler_runtime_note_common_play_plan_result(common_result, 0U);
    if (common_result != BRICK6_SAMPLE_COMMON_PLAN_OK)
    {
        return 0U;
    }
    sample_voice_reader_reset(&multi_voice->reader);
    if (sample_voice_reader_bind_play_plan(&multi_voice->reader,
                                           &common_plan,
                                           BRICK6_SAMPLER_CACHE_VOICE_NONE) == 0U)
    {
        sample_voice_reader_reset(&multi_voice->reader);
        g_brick6_sampler_runtime_diag.multi_page0_missing++;
        brick6_sampler_runtime_multi_diag_note_page0_reject(
            track_id,
            note,
            velocity,
            instrument_id,
            &resolved,
            sample_page_cache_audio_get_page_state_key(key, 0U));
        return 0U;
    }
    multi_voice->trigger_order = brick6_sampler_runtime_next_trigger_order();
    sample_voice_reader_bind_loop_cache_incarnation(
        &multi_voice->reader,
        brick6_sampler_runtime_multi_voice_index(multi_voice),
        multi_voice->trigger_order);
    const brick6_sampler_multi_voice_handle_t multi_handle =
        brick6_sampler_runtime_multi_voice_handle(multi_voice);
    uint8_t dsp_slot = MULTI_VOICE_DSP_SLOT_INDEX_INVALID;
    const multi_voice_dsp_format_t dsp_format =
        (common_plan.format == SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED)
            ? MULTI_VOICE_DSP_FORMAT_STEREO
            : MULTI_VOICE_DSP_FORMAT_MONO;
    const uint8_t mix_track = ctx->program_route.mix_track_id;
    if (mix_track >= MIXER_MAX_TRACKS)
    {
        sample_voice_reader_reset(&multi_voice->reader);
        multi_voice->trigger_order = 0U;
        return 0U;
    }
    if (multi_voice_dsp_acquire(multi_handle,
                                dsp_format,
                                (float)BRICK6_SAMPLER_RAM_OUTPUT_SAMPLE_RATE_HZ,
                                &dsp_slot) == 0U)
    {
        sample_voice_reader_reset(&multi_voice->reader);
        multi_voice->trigger_order = 0U;
        multi_voice->dsp_slot = MULTI_VOICE_DSP_SLOT_INDEX_INVALID;
        return 0U;
    }
    multi_voice->dsp_slot = dsp_slot;
    multi_voice_dsp_slot_t *const dsp_state = multi_voice_dsp_get(dsp_slot);
    if (dsp_state == NULL)
    {
        sample_voice_reader_reset(&multi_voice->reader);
        (void)multi_voice_dsp_release(dsp_slot, multi_handle);
        multi_voice->dsp_slot = MULTI_VOICE_DSP_SLOT_INDEX_INVALID;
        multi_voice->trigger_order = 0U;
        return 0U;
    }
    multi_voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_MULTI;
    multi_voice->sample_id = resolved.multi_sample_id;
    multi_voice->multi_instrument_id = instrument_id;
    multi_voice->multi_sample_id = resolved.multi_sample_id;
    multi_voice->owner_track_id = track_id;
    brick6_sampler_runtime_multi_assign_spread_index(multi_voice, track_id);
    multi_voice->position = (float)common_plan.start_frame;
    multi_voice->active = 1U;
    brick6_sampler_runtime_mark_track_activity(track_id);
    multi_voice->note = note;
    multi_voice->velocity = velocity;
    multi_voice->mode = 0U;
    multi_voice->gain = g_sampler_multi_track_state[track_id].gain * gain;
    multi_voice->trigger_velocity_gain =
        multi_trigger_velocity_gain;
    multi_voice->region_begin = common_plan.region_begin;
    multi_voice->region_end = common_plan.region_end;
    multi_voice->loop_frames = common_plan.region_end - common_plan.region_begin;
    multi_voice->fade_in_frames = common_plan.fade_in_frames;
    multi_voice->fade_out_frames = common_plan.fade_out_frames;
    multi_voice->step_signed = (float)common_plan.step_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
    multi_voice->reverse = common_plan.direction;
    multi_voice->loop_mode = common_plan.loop_mode;
    multi_voice->use_slice = 0U;
    multi_voice->use_segment_cursor = 1U;
    multi_voice->multi_renderer =
        (common_plan.kernel_type != SAMPLE_KERNEL_FWD_1X)
            ? BRICK6_SAMPLER_MULTI_RENDER_PITCHED
            : ((common_plan.format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
                   ? BRICK6_SAMPLER_MULTI_RENDER_MONO_1X
                   : BRICK6_SAMPLER_MULTI_RENDER_STEREO_1X);
    multi_voice->release_pending = 0U;
    multi_voice->vca_note_released = 0U;
    multi_voice->output_id = output_id;
    multi_voice->play_plan = common_plan;
    multi_voice->format = common_plan.format;
    multi_voice->stride_floats = common_plan.stride_floats;
    multi_voice->frames_per_page = common_plan.frames_per_page;
    multi_voice->registration_epoch = common_plan.registration_epoch;
    multi_voice->last_out_l = 0.0f;
    multi_voice->last_out_r = 0.0f;
    multi_voice->last_out_valid = 0U;
    multi_voice->start_fade_remaining = 0U;
    /* Arm the voice-local envelopes only after every fallible preparation step
     * has succeeded and all ownership/state fields describe this generation. */
    mixer_multi_filter_note_on(mix_track, dsp_state, note);
    if (local_start_fade != 0U)
    {
        brick6_sampler_runtime_start_declick_fade_in(multi_voice);
    }
    g_brick6_sampler_runtime_diag.multi_instrument_id = instrument_id;
    g_brick6_sampler_runtime_diag.multi_sample_id = resolved.multi_sample_id;
    g_brick6_sampler_runtime_diag.multi_gain_applied =
        (g_sampler_multi_track_state[track_id].gain != 1.0f) ? 1U : 0U;
    g_brick6_sampler_runtime_diag.multi_voice_started++;
    brick6_sampler_runtime_diag_note_trigger(track_id, multi_voice, 0U, resolved.total_frames);
    brick6_sampler_runtime_multi_diag_note_trigger(multi_voice);
    if (held == 0U)
        mod_lfo_v1_poly_note_trigger(track_id,
            (uint8_t)(SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET
                      + brick6_sampler_runtime_multi_voice_index(multi_voice)));
    return 1U;
}

uint8_t brick6_sampler_runtime_trigger_multi_note_velocity_output(uint8_t track_id,
                                                                  uint16_t instrument_id,
                                                                  uint8_t note,
                                                                  uint8_t velocity,
                                                                  float gain,
                                                                  uint32_t output_id)
{
    return brick6_sampler_runtime_start_multi_renderer(track_id, instrument_id,
        note, velocity, gain, output_id, 0U);
}

uint8_t brick6_sampler_runtime_trigger_multi_note_velocity(uint8_t track_id,
                                                           uint16_t instrument_id,
                                                           uint8_t note,
                                                           uint8_t velocity,
                                                           float gain)
{
    return brick6_sampler_runtime_trigger_multi_note_velocity_output(track_id,
                                                                    instrument_id,
                                                                    note,
                                                                    velocity,
                                                                    gain,
                                                                    0U);
}

uint8_t brick6_sampler_runtime_trigger_multi_track_note_velocity(uint8_t track_id,
                                                                 uint8_t note,
                                                                 uint8_t velocity)
{
    return brick6_sampler_runtime_trigger_multi_note_velocity_output(track_id,
                                                                    MULTI_SAMPLE_POOL_INVALID_ID,
                                                                    note,
                                                                    velocity,
                                                                    1.0f,
                                                                    0U);
}

uint8_t brick6_sampler_runtime_trigger_multi_track_note_velocity_output(uint8_t track_id,
                                                                        uint8_t note,
                                                                        uint8_t velocity,
                                                                        uint32_t output_id)
{
    return brick6_sampler_runtime_trigger_multi_note_velocity_output(track_id,
                                                                    MULTI_SAMPLE_POOL_INVALID_ID,
                                                                    note,
                                                                    velocity,
                                                                    1.0f,
                                                                    output_id);
}

uint8_t brick6_sampler_runtime_initialize_held_note(uint8_t track_id,
                                                    uint8_t note,
                                                    uint8_t velocity,
                                                    uint32_t output_id,
                                                    uint8_t multi)
{
    if (multi != 0U)
        return brick6_sampler_runtime_start_multi_renderer(track_id,
            MULTI_SAMPLE_POOL_INVALID_ID, note, velocity, 1.0f, output_id, 1U);
    brick6_sampler_runtime_trigger_note_velocity(track_id, note, velocity);
    return 1U;
}

void brick6_sampler_runtime_note_off_multi_track_note_output(uint8_t track_id,
                                                             uint8_t note,
                                                             uint32_t output_id)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (note > 127U) || (output_id == 0U))
    {
        return;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if ((voice->active == 0U)
            || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
            || (voice->owner_track_id != track_id)
            || (voice->note != note)
            || (voice->output_id != output_id))
        {
            continue;
        }

        mixer_multi_filter_note_off(multi_voice_dsp_get(voice->dsp_slot));
        brick6_sampler_runtime_multi_release_voice_vca(voice);
        voice->release_pending = 1U;
        return;
    }
}

void brick6_sampler_runtime_note_off_multi_track_note_all(uint8_t track_id, uint8_t note)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (note > 127U))
    {
        return;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if ((voice->active == 0U)
            || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
            || (voice->owner_track_id != track_id)
            || (voice->note != note))
        {
            continue;
        }

        mixer_multi_filter_note_off(multi_voice_dsp_get(voice->dsp_slot));
        brick6_sampler_runtime_multi_release_voice_vca(voice);
        voice->release_pending = 1U;
    }
}

void brick6_sampler_runtime_note_off(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        /* Source launch mode does not own note amplitude. Every Note-Off lets
         * the mixer VCA finish its release before this source is reclaimed. */
        if ((g_sampler_voice[track_id].active != 0U)
                && (g_sampler_voice[track_id].source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_CLIP))
        {
            g_sampler_voice[track_id].release_pending = 1U;
        }
        return;
    }

    return;
}

void brick6_sampler_runtime_note_off_note(uint8_t track_id, uint8_t note)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
        if ((voice->active != 0U) && (voice->note == note))
        {
            brick6_sampler_runtime_note_off(track_id);
        }
        return;
    }

    const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    if ((voice->active == 0U) || (voice->note != note))
    {
        return;
    }

    if (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
    {
        return;
    }

    brick6_sampler_runtime_stop(track_id);
}

void brick6_sampler_runtime_stop(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        brick6_sampler_runtime_clip_stop_playback(track_id);
        g_sampler_clip_runtime[track_id].state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_IDLE;
        brick6_sampler_runtime_refresh_track_activity(track_id);
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    brick6_sampler_runtime_begin_declick_tail(track_id, voice);
    voice->active = 0U;
    voice->position = 0.0f;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
    voice->ram_generation = 0U;
    voice->ram_data = NULL;
    voice->ram_channels = 0U;
    voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
    sample_voice_reader_stop(&voice->reader);
    brick6_sampler_runtime_multi_stop_track(track_id);
    brick6_sampler_runtime_refresh_track_activity(track_id);
}

void brick6_sampler_runtime_stop_transport_clips(void)
{
    for (uint8_t track_id = 0U; track_id < SEQ_TRACK_COUNT; ++track_id)
    {
        brick6_sampler_runtime_stop(track_id);
    }
}
