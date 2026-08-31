static uint8_t brick6_sampler_runtime_track_is_clip(uint8_t track_id)
{
    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track_id, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    if (ctx == NULL)
    {
        return 0U;
    }

    return (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_STREAM) ? 1U : 0U;
}

static uint8_t brick6_sampler_runtime_clip_mode_is_off(const brick6_sampler_clip_runtime_t *clip)
{
    return ((clip != NULL) && (clip->stretch_mode == 0U)) ? 1U : 0U;
}

static uint8_t brick6_sampler_runtime_clip_mode_is_shifter(const brick6_sampler_clip_runtime_t *clip)
{
    return ((clip != NULL) && (clip->stretch_mode == 2U)) ? 1U : 0U;
}

static uint8_t brick6_sampler_runtime_clip_uses_shifter(const brick6_sampler_clip_runtime_t *clip)
{
    return (brick6_sampler_runtime_clip_mode_is_shifter(clip) != 0U) ? 1U : 0U;
}

static uint16_t brick6_sampler_runtime_clip_sanitize_grain_size(uint16_t grain_size)
{
    switch (grain_size)
    {
        case 384U:
        case 512U:
        case 768U:
        case 1024U:
        case 1536U:
        case 2048U:
            return grain_size;
        default:
            return BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES;
    }
}

static uint16_t brick6_sampler_runtime_clip_shifter_window_frames(uint16_t grain_size)
{
    switch (brick6_sampler_runtime_clip_sanitize_grain_size(grain_size))
    {
        case 384U:
        case 512U:
        case 768U:
        case 1024U:
        case 1536U:
        case 2048U:
            return brick6_sampler_runtime_clip_sanitize_grain_size(grain_size);
        default:
            break;
    }

    return BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES;
}

static brick6_sampler_clip_slot_t *brick6_sampler_runtime_clip_get_slot(uint8_t track_id)
{
    brick6_sampler_clip_runtime_t *clip;
    uint8_t slot_index;

    if (track_id >= SEQ_TRACK_COUNT)
    {
        return NULL;
    }

    clip = &g_sampler_clip_runtime[track_id];
    slot_index = clip->clip_slot_index;
    if (slot_index >= BRICK6_MAX_CLIP_TRACKS)
    {
        return NULL;
    }

    if (g_sampler_clip_slots[slot_index].owner_track_id != track_id)
    {
        return NULL;
    }

    return &g_sampler_clip_slots[slot_index];
}

static brick6_sampler_clip_slot_t *brick6_sampler_runtime_clip_ensure_slot(uint8_t track_id)
{
    brick6_sampler_clip_slot_t *slot;

    slot = brick6_sampler_runtime_clip_get_slot(track_id);
    if (slot != NULL)
    {
        return slot;
    }

    for (uint8_t i = 0U; i < BRICK6_MAX_CLIP_TRACKS; ++i)
    {
        if (g_sampler_clip_slots[i].owner_track_id != BRICK6_SAMPLER_CLIP_SLOT_NONE)
        {
            continue;
        }

        memset(&g_sampler_clip_slots[i], 0, sizeof(g_sampler_clip_slots[i]));
        g_sampler_clip_slots[i].owner_track_id = track_id;
        g_sampler_clip_runtime[track_id].clip_slot_index = i;
        brick6_clip_shifter_init(&g_sampler_clip_slots[i].shifter);
        return &g_sampler_clip_slots[i];
    }

    return NULL;
}

static void brick6_sampler_runtime_clip_release_slot(uint8_t track_id)
{
    brick6_sampler_clip_slot_t *slot;

    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    slot = brick6_sampler_runtime_clip_get_slot(track_id);
    if (slot != NULL)
    {
        memset(slot, 0, sizeof(*slot));
        slot->owner_track_id = BRICK6_SAMPLER_CLIP_SLOT_NONE;
    }

    g_sampler_clip_runtime[track_id].clip_slot_index = BRICK6_SAMPLER_CLIP_SLOT_NONE;
}

static void brick6_sampler_runtime_clip_configure_shifter(const brick6_sampler_clip_runtime_t *clip,
                                                          brick6_sampler_clip_slot_t *slot)
{
    if ((clip == NULL) || (slot == NULL))
    {
        return;
    }

    brick6_clip_shifter_set_window_frames(&slot->shifter,
                                          brick6_sampler_runtime_clip_shifter_window_frames(clip->grain_size));
    brick6_clip_shifter_set_pitch_correction(&slot->shifter, clip->shifter_correction);
}

static void brick6_sampler_runtime_clip_reset_shifter(const brick6_sampler_clip_runtime_t *clip,
                                                      brick6_sampler_clip_slot_t *slot)
{
    if ((clip == NULL) || (slot == NULL))
    {
        return;
    }

    brick6_clip_shifter_reset(&slot->shifter);
    brick6_sampler_runtime_clip_configure_shifter(clip, slot);
}

static void brick6_sampler_runtime_clip_reset(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    brick6_sampler_runtime_clip_release_slot(track_id);
    g_sampler_clip_runtime[track_id].sample_id = 0U;
    g_sampler_clip_runtime[track_id].gain = 1.0f;
    g_sampler_clip_runtime[track_id].source_bpm = 120.0f;
    g_sampler_clip_runtime[track_id].grain_size = BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES;
    g_sampler_clip_runtime[track_id].sync_length = 0U;
    g_sampler_clip_runtime[track_id].pitch_semitones = 0.0f;
    g_sampler_clip_runtime[track_id].play_mode = 0U;
    g_sampler_clip_runtime[track_id].loop_enabled = 1U;
    g_sampler_clip_runtime[track_id].stretch_mode = 0U;
    g_sampler_clip_runtime[track_id].state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_IDLE;
    g_sampler_clip_runtime[track_id].use_shifter_engine = 0U;
    g_sampler_clip_runtime[track_id].clip_slot_index = BRICK6_SAMPLER_CLIP_SLOT_NONE;
    g_sampler_clip_runtime[track_id].timing_ratio_q16 = BRICK6_SAMPLER_Q16_ONE;
    g_sampler_clip_runtime[track_id].pitch_ratio_q16 = BRICK6_SAMPLER_Q16_ONE;
    g_sampler_clip_runtime[track_id].shifter_correction = 1.0f;
    sample_voice_reader_reset(&g_sampler_voice[track_id].reader);
    g_sampler_voice[track_id].active = 0U;
    g_sampler_voice[track_id].note = STREAM_SAMPLER_ROOT_NOTE;
    g_sampler_voice[track_id].position = 0.0f;
    g_sampler_voice[track_id].source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    g_sampler_voice[track_id].source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    g_sampler_voice[track_id].multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_voice[track_id].multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_voice[track_id].release_pending = 0U;
}

static uint32_t brick6_sampler_runtime_clip_ratio_q16(float source_bpm)
{
    if (source_bpm <= 0.0f)
    {
        return BRICK6_SAMPLER_Q16_ONE;
    }

    const float project_bpm = (float)audio_transport_runtime_get()
        ->tempo_effective_bpm_milli * 0.001f;
    if (project_bpm <= 0.0f)
    {
        return BRICK6_SAMPLER_Q16_ONE;
    }

    float ratio = project_bpm / source_bpm;
    if (ratio < 0.5f)
    {
        ratio = 0.5f;
    }
    else if (ratio > 2.0f)
    {
        ratio = 2.0f;
    }

    return (uint32_t)(ratio * (float)BRICK6_SAMPLER_Q16_ONE + 0.5f);
}

static uint32_t brick6_sampler_runtime_clip_resolve_timing_ratio_q16(uint8_t track_id,
                                                                     const sample_resolved_source_t *desc,
                                                                     const brick6_sampler_clip_runtime_t *clip,
                                                                     uint32_t *out_region_begin,
                                                                     uint32_t *out_region_end)
{
    uint32_t region_begin = 0U;
    uint32_t region_end = 0U;
    uint32_t ratio_q16 = BRICK6_SAMPLER_Q16_ONE;

    if ((track_id >= SEQ_TRACK_COUNT) || (desc == NULL) || (clip == NULL) || (desc->total_frames == 0U))
    {
        return ratio_q16;
    }

    region_begin = brick6_sampler_runtime_clamp_region_begin(desc->total_frames, g_sampler_voice[track_id].start);
    region_end = region_begin
                 + brick6_sampler_runtime_clamp_region_length(desc->total_frames,
                                                              region_begin,
                                                              g_sampler_voice[track_id].length);
    if (region_begin >= region_end)
    {
        region_begin = 0U;
        region_end = desc->total_frames;
    }

    if (out_region_begin != NULL)
    {
        *out_region_begin = region_begin;
    }
    if (out_region_end != NULL)
    {
        *out_region_end = region_end;
    }

    if (brick6_sampler_runtime_clip_mode_is_off(clip) != 0U)
    {
        return BRICK6_SAMPLER_Q16_ONE;
    }

    if (clip->sync_length == 4U)
    {
        ratio_q16 = brick6_sampler_runtime_clip_ratio_q16(clip->source_bpm);
    }
    else if (clip->sync_length != 0U)
    {
        uint32_t target_bars = 0U;
        float target_seconds = 0.0f;
        const float project_bpm = (float)audio_transport_runtime_get()
            ->tempo_effective_bpm_milli * 0.001f;
        const uint32_t sample_rate = (desc->sample_rate != 0U) ? desc->sample_rate : 48000U;
        const float source_seconds = (float)(region_end - region_begin) / (float)sample_rate;

        if (clip->sync_length == 1U)
        {
            target_bars = 1U;
        }
        else if (clip->sync_length == 2U)
        {
            target_bars = 2U;
        }
        else if (clip->sync_length == 3U)
        {
            target_bars = 4U;
        }

        if ((target_bars != 0U) && (project_bpm > 0.0f) && (source_seconds > 0.0f))
        {
            target_seconds = ((float)target_bars * 4.0f * 60.0f) / project_bpm;
            if (target_seconds > 0.0f)
            {
                float ratio = source_seconds / target_seconds;
                if (ratio < 0.5f)
                {
                    ratio = 0.5f;
                }
                else if (ratio > 2.0f)
                {
                    ratio = 2.0f;
                }
                ratio_q16 = (uint32_t)(ratio * (float)BRICK6_SAMPLER_Q16_ONE + 0.5f);
            }
        }
    }

    return ratio_q16;
}

static brick6_sampler_stream_pitch_plan_t brick6_sampler_runtime_stream_build_pitch_plan(
    uint8_t track_id,
    uint8_t played_note,
    const sample_resolved_source_t *desc,
    const brick6_sampler_clip_runtime_t *clip,
    uint8_t use_shifter,
    uint32_t *out_region_begin,
    uint32_t *out_region_end)
{
    brick6_sampler_stream_pitch_plan_t plan = {
        .note_delta = (int16_t)played_note - (int16_t)STREAM_SAMPLER_ROOT_NOTE,
        .timing_ratio_q16 = BRICK6_SAMPLER_Q16_ONE,
        .pitch_ratio_q16 = BRICK6_SAMPLER_Q16_ONE,
        .step_q16 = BRICK6_SAMPLER_Q16_ONE,
        .shifter_correction = 1.0f,
    };
    const uint32_t source_sample_rate = ((desc != NULL) && (desc->sample_rate != 0U))
                                            ? desc->sample_rate
                                            : BOARD_AUDIO_SAMPLE_RATE_HZ;
    const float semitones = (float)plan.note_delta
                            + ((clip != NULL) ? clip->pitch_semitones : 0.0f);
    const float musical_ratio = brick6_sampler_runtime_pitch_ratio(semitones);
    const float sample_rate_ratio = (float)source_sample_rate
                                    / (float)BOARD_AUDIO_SAMPLE_RATE_HZ;
    const float desired_pitch_ratio = sample_rate_ratio * musical_ratio;

    plan.timing_ratio_q16 = brick6_sampler_runtime_clip_resolve_timing_ratio_q16(
        track_id, desc, clip, out_region_begin, out_region_end);
    plan.pitch_ratio_q16 = brick6_sampler_runtime_ratio_to_q16(desired_pitch_ratio);

    const float timing_ratio = (float)plan.timing_ratio_q16
                               / (float)BRICK6_SAMPLER_Q16_ONE;
    const float represented_pitch_ratio = (float)plan.pitch_ratio_q16
                                          / (float)BRICK6_SAMPLER_Q16_ONE;
    if (use_shifter != 0U)
    {
        plan.step_q16 = brick6_sampler_runtime_ratio_to_q16(
            sample_rate_ratio * timing_ratio);
        const float reader_timing_ratio = (float)plan.step_q16
                                          / (float)BRICK6_SAMPLER_Q16_ONE;
        if (reader_timing_ratio > 0.0f)
        {
            plan.shifter_correction = represented_pitch_ratio / reader_timing_ratio;
        }
    }
    else
    {
        plan.step_q16 = brick6_sampler_runtime_ratio_to_q16(
            timing_ratio * desired_pitch_ratio);
    }

    return plan;
}

static uint8_t brick6_sampler_runtime_clip_start_playback(uint8_t track_id)
{
    brick6_sampler_clip_slot_t *slot = NULL;
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return 0U;
    }

    brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    sample_resolved_source_t source;
    sample_resolved_source_init(&source);
    const uint8_t cache_voice_id = brick6_sampler_runtime_cache_voice_id(track_id);
    sample_play_plan_t play_plan;
    brick6_sampler_stream_pitch_plan_t pitch_plan;
    uint8_t use_shifter = brick6_sampler_runtime_clip_uses_shifter(clip);
    uint32_t region_begin = 0U;
    uint32_t region_end = 0U;

    if ((clip->sample_id >= SAMPLE_CLASSIC_CAPACITY)
            || (sample_classic_audio_projection_resolve(clip->sample_id, &source) == 0U)
            || (source.total_frames == 0U)
            || (sample_classic_audio_projection_is_ready(clip->sample_id) == 0U))
    {
        if (clip->use_shifter_engine != 0U)
        {
            brick6_sampler_runtime_clip_release_slot(track_id);
        }
        return 0U;
    }

    if (use_shifter != 0U)
    {
        slot = brick6_sampler_runtime_clip_ensure_slot(track_id);
        if (slot == NULL)
        {
            use_shifter = 0U;
        }
    }
    else
    {
        brick6_sampler_runtime_clip_release_slot(track_id);
    }

    pitch_plan = brick6_sampler_runtime_stream_build_pitch_plan(track_id,
                                                                voice->note,
                                                                &source,
                                                                clip,
                                                                use_shifter,
                                                                &region_begin,
                                                                &region_end);
    clip->timing_ratio_q16 = pitch_plan.timing_ratio_q16;
    clip->pitch_ratio_q16 = pitch_plan.pitch_ratio_q16;
    clip->shifter_correction = pitch_plan.shifter_correction;

    memset(&play_plan, 0, sizeof(play_plan));
    sample_resolved_source_t stream_source;
    sample_resolved_source_init(&stream_source);
    if (sample_classic_audio_projection_resolve(clip->sample_id, &stream_source) == 0U)
    {
        if (use_shifter != 0U)
        {
            brick6_sampler_runtime_clip_release_slot(track_id);
        }
        return 0U;
    }
    play_plan.sample_id = clip->sample_id;
    play_plan.key = sample_audio_key_classic(clip->sample_id);
    play_plan.format = stream_source.format;
    play_plan.stride_floats = stream_source.stride_floats;
    play_plan.frames_per_page = stream_source.frames_per_page;
    play_plan.registration_epoch = stream_source.registration_epoch;
    play_plan.start_frame = region_begin;
    play_plan.region_begin = region_begin;
    play_plan.region_end = region_end;
    play_plan.loop_begin = region_begin;
    play_plan.loop_end = region_end;
    play_plan.fade_in_frames = 0U;
    play_plan.fade_out_frames = 0U;
    play_plan.step_q16 = pitch_plan.step_q16;
    play_plan.direction = 0U;
    play_plan.loop_mode = (clip->loop_enabled != 0U) ? BRICK6_SAMPLER_LOOP_FORWARD
                                                     : BRICK6_SAMPLER_LOOP_NONE;
    play_plan.stop_on_underrun = 1U;
    play_plan.kernel_type = (pitch_plan.step_q16 == BRICK6_SAMPLER_Q16_ONE)
                                ? SAMPLE_KERNEL_FWD_1X
                                : SAMPLE_KERNEL_PITCH_FWD_LINEAR;

    const uint8_t start_fade = brick6_sampler_runtime_begin_declick_tail(track_id, voice);
    sample_voice_reader_reset(&voice->reader);

    if (sample_voice_reader_bind_play_plan(&voice->reader, &play_plan, cache_voice_id) == 0U)
    {
        sample_voice_reader_reset(&voice->reader);
        if (use_shifter != 0U)
        {
            brick6_sampler_runtime_clip_release_slot(track_id);
        }
        return 0U;
    }

    voice->sample_id = clip->sample_id;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_CLIP;
    voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_CLIP;
    voice->position = (float)region_begin;
    voice->active = 1U;
    brick6_sampler_runtime_mark_track_activity(track_id);
    voice->mode = 2U;
    voice->slice_count = 0U;
    voice->slice_index = 0U;
    voice->gain = clip->gain;
    voice->trigger_velocity_gain = 1.0f;
    voice->start = g_sampler_voice[track_id].start;
    voice->length = g_sampler_voice[track_id].length;
    voice->tune = 0.0f;
    voice->region_begin = region_begin;
    voice->region_end = region_end;
    voice->loop_frames = region_end - region_begin;
    voice->fade_in_frames = 0U;
    voice->fade_out_frames = 0U;
    voice->step_signed = (float)pitch_plan.step_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
    voice->reverse = 0U;
    voice->loop_mode = (clip->loop_enabled != 0U) ? BRICK6_SAMPLER_LOOP_FORWARD
                                                  : BRICK6_SAMPLER_LOOP_NONE;
    voice->use_slice = 0U;
    voice->use_segment_cursor = 1U;
    voice->play_plan = play_plan;
    voice->format = play_plan.format;
    voice->stride_floats = play_plan.stride_floats;
    voice->frames_per_page = play_plan.frames_per_page;
    voice->registration_epoch = play_plan.registration_epoch;
    voice->trigger_order = brick6_sampler_runtime_next_trigger_order();
    voice->last_out_l = 0.0f;
    voice->last_out_r = 0.0f;
    voice->last_out_valid = 0U;
    voice->start_fade_remaining = 0U;
    voice->release_pending = 0U;
    if (start_fade != 0U)
    {
        brick6_sampler_runtime_start_declick_fade_in(voice);
    }
    clip->use_shifter_engine = use_shifter;
    if ((clip->use_shifter_engine != 0U) && (slot != NULL))
    {
        brick6_sampler_runtime_clip_reset_shifter(clip, slot);
    }
    return 1U;
}

static void brick6_sampler_runtime_clip_stop_playback_internal(uint8_t track_id,
                                                               uint8_t with_declick)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    if (with_declick != 0U)
    {
        brick6_sampler_runtime_begin_declick_tail(track_id, voice);
    }
    sample_voice_reader_stop(&voice->reader);
    voice->active = 0U;
    voice->position = 0.0f;
    voice->ram_position_q16 = 0ULL;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    voice->release_pending = 0U;
    g_sampler_clip_runtime[track_id].state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_IDLE;
    g_sampler_clip_runtime[track_id].use_shifter_engine = 0U;
    brick6_sampler_runtime_clip_release_slot(track_id);
}

static void brick6_sampler_runtime_clip_stop_playback(uint8_t track_id)
{
    brick6_sampler_runtime_clip_stop_playback_internal(track_id, 1U);
}

static void brick6_sampler_runtime_clip_stop_playback_silent(uint8_t track_id)
{
    brick6_sampler_runtime_clip_stop_playback_internal(track_id, 0U);
}

