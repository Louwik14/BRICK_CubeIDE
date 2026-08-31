void brick6_sampler_runtime_init(void)
{
    multi_voice_dsp_init();
    brick6_sampler_runtime_diag_reset();
    memset(g_sampler_voice, 0, sizeof(g_sampler_voice));
    memset(g_sampler_multi_voice, 0, sizeof(g_sampler_multi_voice));
    memset(g_sampler_multi_track_state, 0, sizeof(g_sampler_multi_track_state));
    memset(g_sampler_clip_runtime, 0, sizeof(g_sampler_clip_runtime));
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
        g_sampler_multi_voice[i].ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
        g_sampler_multi_voice[i].ram_step_q16 = BRICK6_SAMPLER_Q16_ONE;
        g_sampler_multi_voice[i].dsp_slot = MULTI_VOICE_DSP_SLOT_INDEX_INVALID;
        g_sampler_multi_voice[i].spread_index = BRICK6_SAMPLER_MULTI_VOICE_INDEX_INVALID;
    }
    for (uint8_t i = 0U; i < SEQ_TRACK_COUNT; ++i)
    {
        g_sampler_voice[i].note = 60U;
        g_sampler_voice[i].source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        g_sampler_voice[i].multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_sampler_voice[i].multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_sampler_voice[i].ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
        g_sampler_voice[i].velocity = 127U;
        g_sampler_voice[i].gain = 1.0f;
        g_sampler_voice[i].trigger_velocity_gain = 1.0f;
        g_sampler_voice[i].step_signed = 1.0f;
        g_sampler_voice[i].ram_step_q16 = BRICK6_SAMPLER_Q16_ONE;
        g_sampler_voice[i].start = 0.0f;
        g_sampler_voice[i].length = 1.0f;
        g_sampler_voice[i].loop_start = 0.0f;
        g_sampler_voice[i].slice_count = 0U;
        g_sampler_voice[i].loop_begin = 0U;
        g_sampler_voice[i].loop_mode = 0U;
        g_sampler_voice[i].reverse = 0U;
        brick6_sampler_runtime_multi_track_reset(i);
        sample_voice_reader_reset(&g_sampler_voice[i].reader);
        brick6_sampler_runtime_clip_reset(i);
    }
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
    g_sampler_voice[track_id].ram_slot = SAMPLER_RAM_AUDIO_INVALID_SLOT;
    g_sampler_voice[track_id].ram_generation = 0U;
    g_sampler_voice[track_id].ram_data = NULL;
    g_sampler_voice[track_id].ram_channels = 0U;
    g_sampler_voice[track_id].ram_format = SAMPLER_RAM_FORMAT_NONE;
    g_sampler_voice[track_id].velocity = 127U;
    g_sampler_voice[track_id].gain = 1.0f;
    g_sampler_voice[track_id].trigger_velocity_gain = 1.0f;
    g_sampler_voice[track_id].step_signed = 1.0f;
    g_sampler_voice[track_id].ram_step_q16 = BRICK6_SAMPLER_Q16_ONE;
    g_sampler_voice[track_id].length = 1.0f;
    g_sampler_voice[track_id].loop_start = 0.0f;
    g_sampler_voice[track_id].loop_begin = 0U;
    g_sampler_voice[track_id].slice_count = 0U;
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

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
        if ((clip->sample_id != sample_id)
                && ((g_sampler_voice[track_id].active != 0U)
                    || (g_sampler_voice[track_id].reader.active != 0U)))
        {
            brick6_sampler_runtime_clip_stop_playback(track_id);
        }
        else if (clip->sample_id != sample_id)
        {
        }
        clip->sample_id = sample_id;
        return;
    }

    g_sampler_voice[track_id].sample_id = sample_id;
    g_sampler_voice[track_id].note = 60U;
    g_sampler_voice[track_id].position = 0.0f;
}

void brick6_sampler_runtime_set_gain(uint8_t track_id, float gain)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        g_sampler_clip_runtime[track_id].gain = gain;
        g_sampler_voice[track_id].gain = gain;
        return;
    }
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
        g_brick6_sampler_runtime_diag.multi_invalid_instrument_id++;
        return;
    }

    if (g_sampler_multi_track_state[track_id].instrument_id != instrument_id)
    {
        brick6_sampler_runtime_multi_stop_track(track_id);
        g_sampler_multi_track_state[track_id].instrument_id = instrument_id;
    }
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
    /* All reader leases and DSP slots are released synchronously
     * before the command consumer advances its physical tail. */
}

void brick6_sampler_runtime_stop_ram_slot(uint16_t ram_slot, uint32_t generation)
{
    if (ram_slot >= SAMPLER_RAM_AUDIO_MAX_SLOTS) return;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        brick6_sampler_voice_t *const voice = &g_sampler_voice[track];
        if ((voice->active != 0U)
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
            && (voice->ram_slot == ram_slot)
            && (voice->ram_generation == generation))
        {
            brick6_sampler_runtime_begin_declick_tail(track, voice);
            brick6_sampler_runtime_clear_ram_voice(voice);
        }
    }
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
    g_sampler_voice[track_id].start = start;
}

void brick6_sampler_runtime_set_length(uint8_t track_id, float length)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].length = length;
}

void brick6_sampler_runtime_set_mode(uint8_t track_id, uint8_t mode)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].mode = (mode > 5U) ? 0U : mode;
    brick6_sampler_runtime_reconcile_ram_voice_bounds_live(track_id);
}

void brick6_sampler_runtime_set_tune(uint8_t track_id, float tune)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].tune = tune;
    brick6_sampler_runtime_reproject_ram_voice_tune_live(track_id);
}

void brick6_sampler_runtime_set_loop_start(uint8_t track_id, float loop_start)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].loop_start = loop_start;
    if (brick6_sampler_runtime_ram_slice_mode_active(track_id) == 0U)
    {
        brick6_sampler_runtime_reconcile_ram_voice_bounds_live(track_id);
    }
}

void brick6_sampler_runtime_set_slice_count(uint8_t track_id, uint8_t slice_count)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_voice[track_id].slice_count = brick6_sampler_runtime_resolve_grid_count(slice_count);
}

void brick6_sampler_runtime_set_clip_source_bpm(uint8_t track_id, float source_bpm)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    g_sampler_clip_runtime[track_id].source_bpm = source_bpm;
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

    if (semitones < -12.0f)
    {
        semitones = -12.0f;
    }
    else if (semitones > 12.0f)
    {
        semitones = 12.0f;
    }

    g_sampler_clip_runtime[track_id].pitch_semitones = semitones;
}

void brick6_sampler_runtime_set_clip_play_mode(uint8_t track_id, uint8_t play_mode)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    g_sampler_clip_runtime[track_id].play_mode = (play_mode != 0U) ? 1U : 0U;
}

void brick6_sampler_runtime_set_clip_loop(uint8_t track_id, uint8_t loop_enabled)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    const uint8_t enabled = (loop_enabled != 0U) ? 1U : 0U;
    g_sampler_clip_runtime[track_id].loop_enabled = enabled;
    g_sampler_voice[track_id].loop_mode = (enabled != 0U) ? BRICK6_SAMPLER_LOOP_FORWARD
                                                          : BRICK6_SAMPLER_LOOP_NONE;
    g_sampler_voice[track_id].play_plan.loop_mode = g_sampler_voice[track_id].loop_mode;
    g_sampler_voice[track_id].reader.plan.loop_mode = g_sampler_voice[track_id].loop_mode;
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

void brick6_sampler_runtime_trigger(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
        const uint8_t launch_mode = (clip->play_mode != 0U) ? 1U : 0U;

        switch ((brick6_sampler_clip_state_t)clip->state)
        {
            case BRICK6_SAMPLER_CLIP_STATE_IDLE:
            case BRICK6_SAMPLER_CLIP_STATE_STOPPED:
                clip->state = (brick6_sampler_runtime_clip_start_playback(track_id) != 0U)
                                  ? (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING
                                  : (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
                break;

            case BRICK6_SAMPLER_CLIP_STATE_PLAYING:
                if (launch_mode == 0U)
                {
                    clip->state = (brick6_sampler_runtime_clip_start_playback(track_id) != 0U)
                                      ? (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING
                                      : (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
                }
                break;

            default:
                clip->state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_IDLE;
                break;
        }

        return;
    }

    if (brick6_sampler_runtime_ram_slice_mode_active(track_id) != 0U)
    {
        brick6_sampler_runtime_trigger_ram_slice(track_id);
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    if (brick6_sampler_runtime_trigger_ram(track_id) != 0U)
    {
        return;
    }

    brick6_sampler_runtime_begin_declick_tail(track_id, voice);
    sample_voice_reader_reset(&voice->reader);
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
    voice->use_segment_cursor = 0U;
    voice->release_pending = 0U;
    memset(&voice->play_plan, 0, sizeof(voice->play_plan));
}

void brick6_sampler_runtime_trigger_note(uint8_t track_id, uint8_t note)
{
    brick6_sampler_runtime_trigger_note_velocity(track_id, note, 127U);
}

void brick6_sampler_runtime_trigger_note_velocity(uint8_t track_id, uint8_t note, uint8_t velocity)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_voice[track_id].note = note;
    g_sampler_voice[track_id].velocity = velocity;

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
        clip->state = (brick6_sampler_runtime_clip_start_playback(track_id) != 0U)
                          ? (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING
                          : (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
        return;
    }

    brick6_sampler_runtime_trigger(track_id);
}
