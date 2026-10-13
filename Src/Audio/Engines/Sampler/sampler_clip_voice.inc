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

static uint8_t brick6_sampler_runtime_clip_resolve_source(
    const brick6_sampler_clip_runtime_t *clip,
    sample_resolved_source_t *source)
{
    if ((clip == NULL) || (source == NULL)) return 0U;
    return (clip->source != 0U)
        ? rec_source_audio_resolve(source)
        : sample_classic_audio_projection_resolve(clip->sample_id, source);
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
        case BRICK6_SAMPLER_CLIP_GRAIN_LAW:
        case 192U:
        case 256U:
        case 320U:
        case 384U:
        case 512U:
        case 576U:
        case 640U:
        case 704U:
        case 768U:
        case 1024U:
        case 1536U:
        case 2048U:
        case 2560U:
        case 3072U:
        case 4096U:
        case 6144U:
        case 8192U:
            return grain_size;
        default:
            return BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES;
    }
}

static uint16_t brick6_sampler_runtime_clip_shifter_window_frames(uint16_t grain_size)
{
    switch (brick6_sampler_runtime_clip_sanitize_grain_size(grain_size))
    {
        case 192U:
        case 256U:
        case 320U:
        case 384U:
        case 512U:
        case 576U:
        case 640U:
        case 704U:
        case 768U:
        case 1024U:
        case 1536U:
        case 2048U:
        case 2560U:
        case 3072U:
        case 4096U:
        case 6144U:
        case 8192U:
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
        float *const delay_l = (i == 0U) ? g_sampler_clip_shifter_delay_d1[0]
                               : (i == 1U) ? g_sampler_clip_shifter_delay_general[0]
                               : (i == 2U) ? g_sampler_clip_shifter_delay_recorder[0]
                                           : g_sampler_clip_shifter_delay_recorder[2];
        float *const delay_r = (i == 0U) ? g_sampler_clip_shifter_delay_d1[1]
                               : (i == 1U) ? g_sampler_clip_shifter_delay_general[1]
                               : (i == 2U) ? g_sampler_clip_shifter_delay_recorder[1]
                                           : g_sampler_clip_shifter_delay_recorder_ring;
        brick6_clip_shifter_init(&g_sampler_clip_slots[i].shifter, delay_l, delay_r);
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

    const uint16_t grain_size = (clip->grain_size == BRICK6_SAMPLER_CLIP_GRAIN_LAW)
                                    ? clip->law_grain_size
                                    : brick6_sampler_runtime_clip_shifter_window_frames(clip->grain_size);
    brick6_clip_shifter_set_window_frames(&slot->shifter,
                                          grain_size);
    brick6_clip_shifter_set_pitch_correction(&slot->shifter, clip->shifter_correction);
    brick6_clip_shifter_set_experiment(&slot->shifter,
                                       clip->shifter_heads,
                                       clip->shifter_window,
                                       clip->shifter_dispersion);
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
    /* No source is not sample 0.  Keep the AUDIO selector aligned with the
     * CONTROL/PERSIST asset sentinel so a reset cannot masquerade as a
     * successfully installed first sample. */
    g_sampler_clip_runtime[track_id].sample_id = UINT16_MAX;
    g_sampler_clip_runtime[track_id].source = 0U;
    g_sampler_clip_runtime[track_id].gain = 1.0f;
    g_sampler_clip_runtime[track_id].source_bpm = 120.0f;
    g_sampler_clip_runtime[track_id].grain_size = BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES;
    g_sampler_clip_runtime[track_id].law_grain_size = BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES;
    g_sampler_clip_runtime[track_id].shifter_heads = 2U;
    g_sampler_clip_runtime[track_id].shifter_window = (uint8_t)BRICK6_CLIP_SHIFTER_WINDOW_TRI;
    g_sampler_clip_runtime[track_id].shifter_dispersion = 0U;
    g_sampler_clip_runtime[track_id].sync_length = 0U;
    g_sampler_clip_runtime[track_id].pitch_semitones = 0.0f;
    g_sampler_clip_runtime[track_id].mode = BRICK6_SAMPLER_MODE_ONE;
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
    brick6_sampler_runtime_stream_live_reset(track_id);
}

static void brick6_sampler_runtime_publish_playhead(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT) return;
    sample_playhead_slot_t *const slot = &g_sample_playhead[track_id];
    const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    uint32_t sequence = slot->sequence;
    if ((sequence & 1U) != 0U) ++sequence;
    slot->sequence = sequence + 1U;
    __DMB();
    sample_playhead_snapshot_t snapshot = {0};
    if ((voice->active != 0U)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_CLIP))
    {
        uint32_t base_begin = brick6_sampler_runtime_clamp_region_begin(
            voice->source_frames, voice->start);
        uint32_t base_end = base_begin
            + brick6_sampler_runtime_clamp_region_length(
                voice->source_frames, base_begin, voice->length);
        if (base_end <= base_begin) base_end = base_begin + 1U;
        snapshot.generation = voice->registration_epoch;
        snapshot.frame = voice->reader.frame_pos;
        snapshot.frame_count = voice->source_frames;
        snapshot.region_begin = base_begin;
        snapshot.region_end = base_end;
        snapshot.loop_begin = voice->loop_begin;
        snapshot.slice_count = voice->slice_count;
        snapshot.slice_index = voice->slice_index;
        snapshot.global_slot = voice->sample_id;
        snapshot.active = 1U;
    }
    slot->snapshot = snapshot;
    __DMB();
    slot->sequence = sequence + 2U;
    __DMB();
}

void __attribute__((used, externally_visible, noinline))
brick6_sampler_runtime_stream_live_diag_reset(void)
{
    memset(&g_sampler_stream_live_diag, 0,
           sizeof(g_sampler_stream_live_diag));
}

static void brick6_sampler_runtime_stream_live_reset(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT) return;
    brick6_sampler_stream_live_t *const live = &g_sampler_stream_live[track_id];
    if ((live->pending != 0U) && (live->pending_start != 0U))
        ++g_sampler_stream_live_diag.start_obsolete;
    memset(live, 0, sizeof(*live));
}

static void brick6_sampler_runtime_resolve_live_bounds(
    uint32_t total_frames, const brick6_sampler_voice_t *voice,
    uint32_t *out_begin, uint32_t *out_end, uint32_t *out_loop_begin)
{
    uint32_t begin = 0U;
    uint32_t end = total_frames;
    uint32_t loop_begin = 0U;
    if (voice->slice_count != 0U)
    {
        brick6_sampler_runtime_resolve_slice_bounds(
            total_frames, voice->slice_count, voice->slice_index,
            &begin, &end);
        loop_begin = begin;
    }
    else
    {
        begin = brick6_sampler_runtime_clamp_region_begin(
            total_frames, voice->start);
        end = begin + brick6_sampler_runtime_clamp_region_length(
            total_frames, begin, voice->length);
        if (end <= begin) end = begin + 1U;
        loop_begin = begin;
        if (brick6_sampler_mode_loop(voice->mode)
            == (uint8_t)SAMPLE_PLAY_LOOP_FORWARD)
        {
            const uint32_t marker = brick6_sampler_runtime_clamp_region_begin(
                total_frames, voice->loop_start);
            if ((marker >= begin) && (marker < end)) loop_begin = marker;
        }
    }
    *out_begin = begin;
    *out_end = end;
    *out_loop_begin = loop_begin;
}

static void brick6_sampler_runtime_stream_live_apply(
    uint8_t track_id,
    brick6_sampler_stream_live_t *live)
{
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
    const uint8_t loop_mode = brick6_sampler_mode_loop(voice->mode);
    const uint32_t loop_begin = live->pending_loop_begin;
    const uint8_t did_seek = live->pending_seek;

    voice->region_begin = live->pending_region_begin;
    voice->region_end = live->pending_region_end;
    voice->loop_begin = loop_begin;
    voice->loop_frames = live->pending_region_end - loop_begin;
    voice->loop_mode = loop_mode;
    voice->play_plan.region_begin = voice->region_begin;
    voice->play_plan.region_end = voice->region_end;
    voice->play_plan.loop_begin = loop_begin;
    voice->play_plan.loop_end = voice->region_end;
    voice->play_plan.loop_mode = loop_mode;
    sample_voice_reader_update_bounds(&voice->reader,
                                      voice->region_begin,
                                      voice->region_end,
                                      loop_begin,
                                      voice->region_end,
                                      loop_mode);
    if (did_seek != 0U)
    {
        brick6_sampler_runtime_begin_declick_tail(track_id, voice);
        sample_voice_reader_seek(&voice->reader,
                                 live->pending_target_frame);
        sample_voice_reader_set_direction(&voice->reader,
            voice->mode == BRICK6_SAMPLER_MODE_REVERSE_ONE);
        voice->position = (float)live->pending_target_frame;
        brick6_sampler_runtime_start_declick_fade_in(voice);
        if (clip->use_shifter_engine != 0U)
        {
            brick6_sampler_clip_slot_t *const slot =
                brick6_sampler_runtime_clip_get_slot(track_id);
            if (slot != NULL)
                brick6_sampler_runtime_clip_reset_shifter(clip, slot);
        }
    }
    sample_voice_reader_cancel_requested_page(&voice->reader);
    if ((live->apply_mask & BRICK6_SAMPLER_STREAM_LIVE_DIRTY_START) != 0U)
        ++g_sampler_stream_live_diag.start_applied;
    if ((live->apply_mask & BRICK6_SAMPLER_STREAM_LIVE_DIRTY_LENGTH) != 0U)
        ++g_sampler_stream_live_diag.length_applied;
    live->apply_mask = 0U;
    live->pending = 0U;
    live->pending_seek = 0U;
    live->pending_start = 0U;
    live->cooldown_frames = BRICK6_SAMPLER_STREAM_LIVE_INTERVAL_FRAMES;
}

static void brick6_sampler_runtime_stream_live_schedule(uint8_t track_id)
{
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    brick6_sampler_stream_live_t *const live = &g_sampler_stream_live[track_id];
    brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
    sample_resolved_source_t source;
    sample_resolved_source_init(&source);
    if ((live->dirty_mask == 0U) || (live->pending != 0U)
        || (voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
        || (brick6_sampler_runtime_clip_resolve_source(clip, &source) == 0U)
        || (source.total_frames == 0U)) return;

    uint32_t region_begin = 0U;
    uint32_t region_end = 0U;
    uint32_t loop_begin = 0U;
    brick6_sampler_runtime_resolve_live_bounds(source.total_frames, voice,
        &region_begin, &region_end, &loop_begin);

    uint8_t seek = 0U;
    uint32_t target = voice->reader.frame_pos;
    if (voice->reader.frame_pos < region_begin)
    {
        seek = 1U;
        target = region_begin;
    }
    else if (voice->reader.frame_pos >= region_end)
    {
        seek = 1U;
        target = (brick6_sampler_mode_loop(voice->mode)
                  == SAMPLE_PLAY_LOOP_FORWARD)
            ? loop_begin : (region_end - 1U);
    }

    if (live->cooldown_frames != 0U) return;

    live->pending_region_begin = region_begin;
    live->pending_region_end = region_end;
    live->pending_loop_begin = loop_begin;
    live->pending_target_frame = target;
    live->pending_seek = seek;
    live->apply_mask = live->dirty_mask;
    live->dirty_mask = 0U;
    live->pending_start = ((live->apply_mask
        & BRICK6_SAMPLER_STREAM_LIVE_DIRTY_START) != 0U) ? 1U : 0U;

    if ((seek == 0U)
        || (sample_voice_reader_frame_ready(&voice->reader, target) != 0U))
    {
        if ((seek != 0U) && (live->pending_start != 0U))
            ++g_sampler_stream_live_diag.start_cache_hits;
        brick6_sampler_runtime_stream_live_apply(track_id, live);
        return;
    }
    if (sample_voice_reader_request_page(&voice->reader, target) == 0U)
    {
        live->dirty_mask |= live->apply_mask;
        live->apply_mask = 0U;
        live->pending_seek = 0U;
        live->pending_start = 0U;
        return;
    }
    live->pending = 1U;
    if (live->pending_start != 0U)
    {
        ++g_sampler_stream_live_diag.start_cold_loads;
        if (g_sampler_stream_live_diag.start_pending_max < 1U)
            g_sampler_stream_live_diag.start_pending_max = 1U;
    }
}

static void brick6_sampler_runtime_stream_live_queue(uint8_t track_id,
                                                     uint8_t dirty_mask)
{
    if (track_id >= SEQ_TRACK_COUNT) return;
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    brick6_sampler_stream_live_t *const live = &g_sampler_stream_live[track_id];
    if ((voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_CLIP))
        return;
    if ((dirty_mask & BRICK6_SAMPLER_STREAM_LIVE_DIRTY_START) != 0U)
        ++g_sampler_stream_live_diag.start_requested;
    if ((live->dirty_mask != 0U) || (live->pending != 0U)
        || (live->cooldown_frames != 0U))
        if ((dirty_mask & BRICK6_SAMPLER_STREAM_LIVE_DIRTY_START) != 0U)
            ++g_sampler_stream_live_diag.start_coalesced;
    live->dirty_mask |= dirty_mask;
    if (live->pending == 0U)
        brick6_sampler_runtime_stream_live_schedule(track_id);
}

static void brick6_sampler_runtime_stream_live_service(uint8_t track_id,
                                                       uint32_t frames)
{
    brick6_sampler_stream_live_t *const live = &g_sampler_stream_live[track_id];
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    if (live->pending != 0U)
    {
        if (sample_voice_reader_requested_page_ready(&voice->reader) != 0U)
        {
            if (live->pending_start != 0U)
                ++g_sampler_stream_live_diag.start_target_ready;
            brick6_sampler_runtime_stream_live_apply(track_id, live);
        }
    }
    if (live->cooldown_frames > frames)
        live->cooldown_frames -= frames;
    else
        live->cooldown_frames = 0U;
    if ((live->pending == 0U) && (live->dirty_mask != 0U))
        brick6_sampler_runtime_stream_live_schedule(track_id);
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

    const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    if (voice->slice_count != 0U)
    {
        uint32_t index = (voice->note > STREAM_SAMPLER_ROOT_NOTE)
            ? (uint32_t)(voice->note - STREAM_SAMPLER_ROOT_NOTE) : 0U;
        if (index >= voice->slice_count) index = voice->slice_count - 1U;
        brick6_sampler_runtime_resolve_slice_bounds(
            desc->total_frames, voice->slice_count, index,
            &region_begin, &region_end);
    }
    else
    {
        region_begin = brick6_sampler_runtime_clamp_region_begin(
            desc->total_frames, voice->start);
        region_end = region_begin
            + brick6_sampler_runtime_clamp_region_length(
                desc->total_frames, region_begin, voice->length);
    }
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
        const float source_seconds = (float)(region_end - region_begin)
            / (float)BOARD_AUDIO_SAMPLE_RATE_HZ;

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

static uint16_t brick6_sampler_runtime_clip_law_grain_size(uint32_t timing_ratio_q16)
{
    if (timing_ratio_q16 == 0U)
    {
        return BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES;
    }

    const float s = (float)timing_ratio_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
    float raw_grain = BRICK6_SAMPLER_CLIP_GRAIN_LAW_K_FRAMES / s;
    if (raw_grain < (float)BRICK6_CLIP_SHIFTER_MIN_WINDOW_FRAMES)
    {
        raw_grain = (float)BRICK6_CLIP_SHIFTER_MIN_WINDOW_FRAMES;
    }
    else if (raw_grain > (float)BRICK6_CLIP_SHIFTER_MAX_WINDOW_FRAMES)
    {
        raw_grain = (float)BRICK6_CLIP_SHIFTER_MAX_WINDOW_FRAMES;
    }

    return (uint16_t)(raw_grain + 0.5f);
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
    const float semitones = (float)plan.note_delta
                            + ((clip != NULL) ? clip->pitch_semitones : 0.0f);
    const float musical_ratio = brick6_sampler_runtime_pitch_ratio(semitones);
    const float sample_rate_ratio = 1.0f;
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

static void brick6_sampler_runtime_reproject_stream_voice_pitch_live(
    uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT) return;
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
    if ((voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)) return;
    sample_resolved_source_t source;
    sample_resolved_source_init(&source);
    if (brick6_sampler_runtime_clip_resolve_source(clip, &source) == 0U)
        return;
    const uint8_t played_note = (voice->slice_count != 0U)
        ? STREAM_SAMPLER_ROOT_NOTE : voice->note;
    uint32_t ignored_begin = 0U;
    uint32_t ignored_end = 0U;
    const brick6_sampler_stream_pitch_plan_t plan =
        brick6_sampler_runtime_stream_build_pitch_plan(
            track_id, played_note, &source, clip,
            brick6_sampler_runtime_clip_uses_shifter(clip),
            &ignored_begin, &ignored_end);
    clip->timing_ratio_q16 = plan.timing_ratio_q16;
    clip->pitch_ratio_q16 = plan.pitch_ratio_q16;
    clip->shifter_correction = plan.shifter_correction;
    voice->step_signed = (float)plan.step_q16
        / (float)BRICK6_SAMPLER_Q16_ONE;
    voice->play_plan.step_q16 = plan.step_q16;
    if (voice->play_plan.kernel_type != SAMPLE_KERNEL_DIRECTIONAL_LINEAR)
        voice->play_plan.kernel_type = (plan.step_q16 == BRICK6_SAMPLER_Q16_ONE)
            ? SAMPLE_KERNEL_FWD_1X : SAMPLE_KERNEL_PITCH_FWD_LINEAR;
    sample_voice_reader_set_step(&voice->reader, voice->step_signed);
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

    voice->slice_count = voice->configured_slice_count;

    if (((clip->source == 0U) && (clip->sample_id >= SAMPLE_CLASSIC_CAPACITY))
            || (brick6_sampler_runtime_clip_resolve_source(clip, &source) == 0U)
            || (source.total_frames == 0U))
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

    const uint8_t played_note = (voice->slice_count != 0U)
        ? STREAM_SAMPLER_ROOT_NOTE : voice->note;
    pitch_plan = brick6_sampler_runtime_stream_build_pitch_plan(track_id,
                                                                played_note,
                                                                &source,
                                                                clip,
                                                                use_shifter,
                                                                &region_begin,
                                                                &region_end);
    clip->timing_ratio_q16 = pitch_plan.timing_ratio_q16;
    clip->law_grain_size = brick6_sampler_runtime_clip_law_grain_size(pitch_plan.timing_ratio_q16);
    clip->pitch_ratio_q16 = pitch_plan.pitch_ratio_q16;
    clip->shifter_correction = pitch_plan.shifter_correction;

    if (voice->slice_count != 0U)
    {
        const uint32_t count = voice->slice_count;
        uint32_t index = (voice->note > STREAM_SAMPLER_ROOT_NOTE)
            ? (uint32_t)(voice->note - STREAM_SAMPLER_ROOT_NOTE) : 0U;
        if (index >= count) index = count - 1U;
        brick6_sampler_runtime_resolve_slice_bounds(
            source.total_frames, count, index, &region_begin, &region_end);
        voice->slice_index = (uint8_t)index;
    }

    memset(&play_plan, 0, sizeof(play_plan));
    play_plan.sample_id = clip->sample_id;
    play_plan.key = source.key;
    play_plan.format = source.format;
    play_plan.stride_floats = source.stride_floats;
    play_plan.frames_per_page = source.frames_per_page;
    play_plan.registration_epoch = source.registration_epoch;
    const uint8_t mode = (clip->mode < BRICK6_SAMPLER_MODE_COUNT)
        ? clip->mode : BRICK6_SAMPLER_MODE_ONE;
    const uint8_t loop_mode = brick6_sampler_mode_loop(mode);
    play_plan.start_frame = (mode == BRICK6_SAMPLER_MODE_REVERSE_ONE)
        ? (region_end - 1U) : region_begin;
    play_plan.region_begin = region_begin;
    play_plan.region_end = region_end;
    uint32_t loop_begin = region_begin;
    if ((loop_mode == SAMPLE_PLAY_LOOP_FORWARD) && (voice->slice_count == 0U))
    {
        const uint32_t marker = brick6_sampler_runtime_clamp_region_begin(
            source.total_frames, voice->loop_start);
        if ((marker >= region_begin) && (marker < region_end))
            loop_begin = marker;
    }
    play_plan.region_begin = region_begin;
    play_plan.region_end = region_end;
    play_plan.loop_begin = loop_begin;
    play_plan.loop_end = region_end;
    play_plan.fade_in_frames = 0U;
    play_plan.fade_out_frames = 0U;
    play_plan.step_q16 = pitch_plan.step_q16;
    play_plan.loop_mode = loop_mode;
    play_plan.initial_reverse = (mode == BRICK6_SAMPLER_MODE_REVERSE_ONE) ? 1U : 0U;
    /* Cold slice pages are scheduled by the lease and start silently once
     * READY; established playback still treats later misses as terminal. */
    play_plan.stop_on_underrun = 0U;
    play_plan.kernel_type = ((mode == BRICK6_SAMPLER_MODE_REVERSE_ONE)
        || (loop_mode == SAMPLE_PLAY_LOOP_PINGPONG))
            ? SAMPLE_KERNEL_DIRECTIONAL_LINEAR
            : ((pitch_plan.step_q16 == BRICK6_SAMPLER_Q16_ONE)
                ? SAMPLE_KERNEL_FWD_1X : SAMPLE_KERNEL_PITCH_FWD_LINEAR);

    const uint8_t start_fade = brick6_sampler_runtime_begin_declick_tail(track_id, voice);
    sample_voice_reader_reset(&voice->reader);

    if (sample_voice_reader_bind_musical_play_plan(
            &voice->reader, &play_plan, cache_voice_id) == 0U)
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
    voice->position = (float)play_plan.start_frame;
    voice->active = 1U;
    brick6_sampler_runtime_mark_track_activity(track_id);
    voice->gain = clip->gain;
    voice->trigger_velocity_gain = 1.0f;
    voice->start = g_sampler_voice[track_id].start;
    voice->length = g_sampler_voice[track_id].length;
    voice->region_begin = region_begin;
    voice->region_end = region_end;
    voice->loop_begin = loop_begin;
    voice->loop_frames = region_end - region_begin;
    voice->fade_in_frames = 0U;
    voice->fade_out_frames = 0U;
    voice->step_signed = (float)pitch_plan.step_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
    voice->mode = mode;
    voice->loop_mode = loop_mode;
    voice->use_slice = 0U;
    voice->use_segment_cursor = 1U;
    voice->play_plan = play_plan;
    voice->format = play_plan.format;
    voice->stride_floats = play_plan.stride_floats;
    voice->frames_per_page = play_plan.frames_per_page;
    voice->registration_epoch = play_plan.registration_epoch;
    voice->source_frames = source.total_frames;
    voice->trigger_order = brick6_sampler_runtime_next_trigger_order();
    voice->last_out_l = 0.0f;
    voice->last_out_r = 0.0f;
    voice->last_out_valid = 0U;
    voice->start_fade_remaining = 0U;
    voice->waiting_for_start_page = (uint8_t)(
        sample_voice_reader_frame_ready(&voice->reader,
                                        play_plan.start_frame) == 0U);
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
    brick6_sampler_runtime_stream_live_reset(track_id);
    return 1U;
}

static void brick6_sampler_runtime_clip_stop_playback(uint8_t track_id);

static void brick6_sampler_runtime_clip_rebind_rec(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT) return;
    brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    if ((clip->source == 0U)
            || (clip->state != (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING)
            || (voice->active == 0U)) return;
    sample_resolved_source_t current;
    if (rec_source_audio_resolve(&current) == 0U)
    {
        brick6_sampler_runtime_clip_stop_playback(track_id);
        return;
    }
    if (sample_audio_key_equal(
            &current.key, &voice->play_plan.key) != 0U) return;
    const uint32_t old_begin = voice->region_begin;
    const uint32_t old_frames = voice->loop_frames;
    const float old_position = voice->position;
    if (brick6_sampler_runtime_clip_start_playback(track_id) == 0U)
        clip->state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
    else if ((old_frames != 0U) && (voice->loop_frames != 0U)
            && (old_position >= (float)old_begin))
    {
        const float phase = (old_position - (float)old_begin)
            / (float)old_frames;
        if ((phase >= 0.0f) && (phase < 1.0f))
        {
            uint32_t offset = (uint32_t)(phase * (float)voice->loop_frames);
            if (offset >= voice->loop_frames) offset = voice->loop_frames - 1U;
            const uint32_t frame = voice->region_begin + offset;
            sample_voice_reader_seek(&voice->reader, frame);
            voice->position = (float)frame;
        }
    }
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
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    voice->release_pending = 0U;
    brick6_sampler_runtime_stream_live_reset(track_id);
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

