void brick6_sampler_runtime_init(void)
{
    sample_voice_reader_init();
    multi_voice_dsp_init();
    brick6_sampler_runtime_diag_reset();
    memset(g_sampler_voice, 0, sizeof(g_sampler_voice));
    memset(g_sampler_multi_voice, 0, sizeof(g_sampler_multi_voice));
    memset(g_sampler_multi_track_state, 0, sizeof(g_sampler_multi_track_state));
    memset(g_sampler_clip_runtime, 0, sizeof(g_sampler_clip_runtime));
    memset(g_sampler_stream_live, 0, sizeof(g_sampler_stream_live));
    brick6_sampler_runtime_stream_live_diag_reset();
    memset(g_sampler_clip_slots, 0, sizeof(g_sampler_clip_slots));
    memset(g_sampler_declick_tail, 0, sizeof(g_sampler_declick_tail));
    g_sampler_render_track_mask = 0U;
    for (uint8_t i = 0U; i < BRICK6_MAX_CLIP_TRACKS; ++i)
    {
        g_sampler_clip_slots[i].owner_track_id = BRICK6_SAMPLER_CLIP_SLOT_NONE;
    }
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        g_sampler_multi_voice[i].owner_track_id = UINT8_MAX;
        g_sampler_multi_voice[i].multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_sampler_multi_voice[i].multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_sampler_multi_voice[i].dsp_slot = MULTI_VOICE_DSP_SLOT_INDEX_INVALID;
        g_sampler_multi_voice[i].spread_index = BRICK6_SAMPLER_MULTI_VOICE_INDEX_INVALID;
    }
    for (uint8_t i = 0U; i < SEQ_TRACK_COUNT; ++i)
    {
        g_sampler_voice[i].note = 60U;
        g_sampler_voice[i].source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        g_sampler_voice[i].multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_sampler_voice[i].multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_sampler_voice[i].velocity = 127U;
        g_sampler_voice[i].gain = 1.0f;
        g_sampler_voice[i].trigger_velocity_gain = 1.0f;
        g_sampler_voice[i].step_signed = 1.0f;
        g_sampler_voice[i].start = 0.0f;
        g_sampler_voice[i].length = 1.0f;
        g_sampler_voice[i].loop_start = 0.0f;
        g_sampler_voice[i].slice_count = 0U;
        g_sampler_voice[i].configured_slice_count = 0U;
        g_sampler_voice[i].loop_begin = 0U;
        g_sampler_voice[i].loop_mode = 0U;
        brick6_sampler_runtime_multi_track_reset(i);
        sample_voice_reader_reset(&g_sampler_voice[i].reader);
        brick6_sampler_runtime_clip_reset(i);
    }
    brick6_sampler_preview_reset();
}

static void brick6_sampler_runtime_reset_track_internal(uint8_t track_id,
                                                        uint8_t renderer_replace)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (renderer_replace == 0U)
        brick6_sampler_runtime_begin_declick_tail(track_id,
                                                  &g_sampler_voice[track_id]);
    if (renderer_replace != 0U)
        brick6_sampler_runtime_multi_stop_track_renderer(track_id);
    else
        brick6_sampler_runtime_multi_stop_track(track_id);
    if (renderer_replace != 0U)
        for (uint8_t i = 0U; i < STEAL_DECLICK_TAIL_SLOTS; ++i)
            if ((g_sampler_declick_tail[i].active != 0U)
                    && (g_sampler_declick_tail[i].owner_track_id == track_id))
                memset(&g_sampler_declick_tail[i], 0,
                       sizeof(g_sampler_declick_tail[i]));
    memset(&g_sampler_voice[track_id], 0, sizeof(g_sampler_voice[track_id]));
    sample_voice_reader_reset(&g_sampler_voice[track_id].reader);
    g_sampler_voice[track_id].note = 60U;
    g_sampler_voice[track_id].source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    g_sampler_voice[track_id].multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_voice[track_id].multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_voice[track_id].velocity = 127U;
    g_sampler_voice[track_id].gain = 1.0f;
    g_sampler_voice[track_id].trigger_velocity_gain = 1.0f;
    g_sampler_voice[track_id].step_signed = 1.0f;
    g_sampler_voice[track_id].length = 1.0f;
    g_sampler_voice[track_id].loop_start = 0.0f;
    g_sampler_voice[track_id].loop_begin = 0U;
    g_sampler_voice[track_id].slice_count = 0U;
    g_sampler_voice[track_id].configured_slice_count = 0U;
    g_sampler_voice[track_id].release_pending = 0U;
    if (renderer_replace == 0U)
    {
        brick6_sampler_runtime_multi_track_reset(track_id);
        brick6_sampler_runtime_clip_reset(track_id);
    }
    brick6_sampler_runtime_refresh_track_activity(track_id);
}

void brick6_sampler_runtime_reset_track(uint8_t track_id)
{
    brick6_sampler_runtime_reset_track_internal(track_id, 0U);
}

void brick6_sampler_runtime_replace_track_renderer(uint8_t track_id)
{
    brick6_sampler_runtime_reset_track_internal(track_id, 1U);
}

void brick6_sampler_runtime_set_sample(uint8_t track_id, uint16_t sample_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    /* This selector controls the next trigger. Existing readers retain their
     * key and lease until their normal NOTE OFF/STOP. */
    g_sampler_clip_runtime[track_id].sample_id = sample_id;
}

uint8_t brick6_sampler_runtime_get_sample(uint8_t track_id,
                                          uint16_t *out_sample_id)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (out_sample_id == NULL))
        return 0U;
    *out_sample_id = g_sampler_clip_runtime[track_id].sample_id;
    return 1U;
}

void brick6_sampler_runtime_set_gain(uint8_t track_id, float gain)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_clip_runtime[track_id].gain = gain;
    g_sampler_voice[track_id].gain = gain;
}

void brick6_sampler_runtime_set_multi_instrument(uint8_t track_id, uint16_t instrument_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if ((instrument_id != MULTI_SAMPLE_POOL_INVALID_ID)
        && (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
    {
        return;
    }

    /* Existing voices retain their instrument/sample identities and leases;
     * only subsequent NOTE ON admission observes this selector. */
    g_sampler_multi_track_state[track_id].instrument_id = instrument_id;
}

void brick6_sampler_runtime_stop_multi_instrument(uint16_t instrument_id)
{
    if ((instrument_id == MULTI_SAMPLE_POOL_INVALID_ID)
        || (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
    {
        return;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if ((voice->active != 0U)
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
            && (voice->multi_instrument_id == instrument_id))
        {
            brick6_sampler_runtime_multi_stop_voice(
                voice,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_STEAL);
        }
    }
    for (uint8_t track_id = 0U; track_id < SEQ_TRACK_COUNT; ++track_id)
    {
        if (g_sampler_multi_track_state[track_id].instrument_id
                == instrument_id)
            g_sampler_multi_track_state[track_id].instrument_id =
                MULTI_SAMPLE_POOL_INVALID_ID;
    }
    /* All reader leases and DSP slots are released synchronously
     * before the command consumer advances its physical tail. */
}

uint8_t brick6_sampler_runtime_get_multi_instrument(uint8_t track_id, uint16_t *out_instrument_id)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (out_instrument_id == NULL))
    {
        return 0U;
    }

    *out_instrument_id = g_sampler_multi_track_state[track_id].instrument_id;
    return 1U;
}

void brick6_sampler_runtime_set_multi_gain(uint8_t track_id, float gain)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (gain < 0.0f)
    {
        gain = 0.0f;
    }
    else if (gain > 4.0f)
    {
        gain = 4.0f;
    }

    g_sampler_multi_track_state[track_id].gain = gain;
}

float brick6_sampler_runtime_get_multi_gain(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0.0f;
    }

    return g_sampler_multi_track_state[track_id].gain;
}

void brick6_sampler_runtime_set_multi_loop(uint8_t track_id, uint8_t enabled)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_multi_track_state[track_id].loop_enabled = (enabled != 0U) ? 1U : 0U;
}

uint8_t brick6_sampler_runtime_multi_instrument_is_ready(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    const uint16_t instrument_id = g_sampler_multi_track_state[track_id].instrument_id;
    if (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
    {
        return 0U;
    }

    return multi_sample_audio_projection_is_ready(instrument_id);
}

void brick6_sampler_runtime_set_start(uint8_t track_id, float start)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const float previous = voice->start;
    voice->start = start;
    if ((previous != start)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
        && (voice->slice_count == 0U))
        brick6_sampler_runtime_stream_live_queue(
            track_id, BRICK6_SAMPLER_STREAM_LIVE_DIRTY_START);
}

void brick6_sampler_runtime_set_length(uint8_t track_id, float length)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const float previous = voice->length;
    voice->length = length;
    if ((previous != length)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
        && (voice->slice_count == 0U))
        brick6_sampler_runtime_stream_live_queue(
            track_id, BRICK6_SAMPLER_STREAM_LIVE_DIRTY_LENGTH);
}

void brick6_sampler_runtime_set_mode(uint8_t track_id, uint8_t mode)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_clip_runtime[track_id].mode = (mode < BRICK6_SAMPLER_MODE_COUNT)
        ? mode : BRICK6_SAMPLER_MODE_ONE;
}

uint8_t brick6_sampler_runtime_mode_is_hold(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
        return 0U;
    const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const uint8_t mode = (voice->active != 0U)
        ? voice->mode : g_sampler_clip_runtime[track_id].mode;
    return brick6_sampler_mode_is_hold_value(mode);
}

void brick6_sampler_runtime_set_tune(uint8_t track_id, float tune)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].tune = tune;
    brick6_sampler_runtime_set_clip_pitch(track_id, tune);
    brick6_sampler_runtime_reproject_stream_voice_pitch_live(track_id);
}

void brick6_sampler_runtime_set_loop_start(uint8_t track_id, float loop_start)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].loop_start = loop_start;
    if (g_sampler_voice[track_id].slice_count == 0U)
        brick6_sampler_runtime_stream_live_queue(
            track_id, BRICK6_SAMPLER_STREAM_LIVE_DIRTY_LOOP);
}

void brick6_sampler_runtime_set_slice_count(uint8_t track_id, uint8_t slice_count)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    switch (slice_count)
    {
        case 0U: case 2U: case 4U: case 8U: case 16U: case 32U: case 64U:
            g_sampler_voice[track_id].configured_slice_count = slice_count;
            break;
        default:
            g_sampler_voice[track_id].configured_slice_count = 0U;
            break;
    }
    if (g_sampler_voice[track_id].active == 0U)
        g_sampler_voice[track_id].slice_count =
            g_sampler_voice[track_id].configured_slice_count;
}

void brick6_sampler_runtime_set_clip_source_bpm(uint8_t track_id, float source_bpm)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    g_sampler_clip_runtime[track_id].source_bpm = source_bpm;
}

void brick6_sampler_runtime_set_clip_source(uint8_t track_id, uint8_t source)
{
    if ((track_id >= SEQ_TRACK_COUNT)
            || (brick6_sampler_runtime_track_is_clip(track_id) == 0U)) return;
    brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
    const uint8_t next = (source != 0U) ? 1U : 0U;
    if (clip->source == next) return;
    brick6_sampler_runtime_clip_stop_playback(track_id);
    clip->source = next;
}

uint8_t brick6_sampler_runtime_clip_source_is_rec(uint8_t track_id)
{
    return ((track_id < SEQ_TRACK_COUNT)
            && (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
            && (g_sampler_clip_runtime[track_id].source != 0U)) ? 1U : 0U;
}

void brick6_sampler_runtime_set_clip_sync_length(uint8_t track_id, uint8_t sync_length)
{
    brick6_sampler_clip_runtime_t *clip;
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    clip = &g_sampler_clip_runtime[track_id];
    clip->sync_length = sync_length;
}

void brick6_sampler_runtime_set_clip_pitch(uint8_t track_id, float semitones)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    if (semitones < -24.0f)
    {
        semitones = -24.0f;
    }
    else if (semitones > 24.0f)
    {
        semitones = 24.0f;
    }

    g_sampler_clip_runtime[track_id].pitch_semitones = semitones;
}

void brick6_sampler_runtime_set_clip_stretch_mode(uint8_t track_id, uint8_t stretch_mode)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    g_sampler_clip_runtime[track_id].stretch_mode = (stretch_mode <= 2U) ? stretch_mode : 0U;
}

void brick6_sampler_runtime_set_clip_grain_size(uint8_t track_id, uint16_t grain_size)
{
    uint16_t sanitized_grain_size;

    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    sanitized_grain_size = brick6_sampler_runtime_clip_sanitize_grain_size(grain_size);
    g_sampler_clip_runtime[track_id].grain_size = sanitized_grain_size;
}

void brick6_sampler_runtime_set_clip_shifter_heads(uint8_t track_id, uint8_t heads)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U)) return;
    if ((heads != 2U) && (heads != 3U) && (heads != 4U) && (heads != 6U) && (heads != 8U)) heads = 2U;
    g_sampler_clip_runtime[track_id].shifter_heads = heads;
}

void brick6_sampler_runtime_set_clip_shifter_window(uint8_t track_id, uint8_t window)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U)) return;
    g_sampler_clip_runtime[track_id].shifter_window =
        (window <= (uint8_t)BRICK6_CLIP_SHIFTER_WINDOW_SINE)
            ? window : (uint8_t)BRICK6_CLIP_SHIFTER_WINDOW_TRI;
}

void brick6_sampler_runtime_set_clip_shifter_dispersion(uint8_t track_id, uint8_t percent)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U)) return;
    g_sampler_clip_runtime[track_id].shifter_dispersion = (percent <= 100U) ? percent : 100U;
}

void brick6_sampler_runtime_trigger(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];

        switch ((brick6_sampler_clip_state_t)clip->state)
        {
            case BRICK6_SAMPLER_CLIP_STATE_IDLE:
            case BRICK6_SAMPLER_CLIP_STATE_STOPPED:
                clip->state = (brick6_sampler_runtime_clip_start_playback(track_id) != 0U)
                                  ? (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING
                                  : (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
                break;

            case BRICK6_SAMPLER_CLIP_STATE_PLAYING:
                clip->state = (brick6_sampler_runtime_clip_start_playback(track_id) != 0U)
                                  ? (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING
                                  : (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
                break;

            default:
                clip->state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_IDLE;
                break;
        }

        return;
    }

    brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
    clip->state = (brick6_sampler_runtime_clip_start_playback(track_id) != 0U)
        ? (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING
        : (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
}

void brick6_sampler_runtime_trigger_note(uint8_t track_id, uint8_t note)
{
    (void)brick6_sampler_runtime_trigger_note_velocity(track_id, note, 127U);
}

uint8_t brick6_sampler_runtime_trigger_note_velocity(uint8_t track_id,
                                                     uint8_t note,
                                                     uint8_t velocity)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    g_sampler_voice[track_id].note = note;
    g_sampler_voice[track_id].velocity = velocity;

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
        clip->state = (brick6_sampler_runtime_clip_start_playback(track_id) != 0U)
                          ? (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING
                          : (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
        return (uint8_t)(clip->state
            == (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING);
    }

    brick6_sampler_runtime_trigger(track_id);
    return (uint8_t)(g_sampler_voice[track_id].active != 0U);
}
