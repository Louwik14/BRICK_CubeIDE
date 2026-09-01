void mixer_set_track_filter_morph(uint32_t track_id, float morph)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    const float next = clampf_local(morph, 0.0f, 127.0f);
    if(filter->morph_target == next) return;
    filter->morph_target = next;
    filter->morph_step = (next - filter->morph) * (1.0f / 64.0f);
    filter->morph_ramp_remaining = 64U;
    filter->filter_env_prepared_consumed = 1U;
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_MORPH);
}

void mixer_set_track_filter_cutoff(uint32_t track_id, float cutoff_hz)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    const float target_hz = clampf_local(cutoff_hz,
                                         MIXER_FILTER_CUTOFF_MIN_HZ,
                                         MIXER_FILTER_CUTOFF_MAX_HZ);
    if (filter->cutoff_target_hz == target_hz) return;
    const float delta = target_hz - filter->cutoff_target_hz;
    filter->cutoff_target_hz = target_hz;
    filter->cutoff_mod_hz += delta;
    filter->cutoff_mod_target_hz += delta;
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_CUTOFF);
}

void mixer_set_track_filter_cutoff_modulated(uint32_t track_id, float cutoff_hz)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;
    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const float next = clampf_local(cutoff_hz,
                                    MIXER_FILTER_CUTOFF_MIN_HZ,
                                    MIXER_FILTER_CUTOFF_MAX_HZ);
    if (filter->cutoff_mod_target_hz == next) return;
    filter->cutoff_mod_target_hz = next;
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_CUTOFF);
}

void mixer_set_track_filter_resonance(uint32_t track_id, float resonance)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    const float next = clampf_local(resonance, 0.0f, 1.0f);
    if (filter->resonance_target == next) return;
    filter->resonance_target = next;
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_RESONANCE);
}

void mixer_set_track_filter_eg_amount(uint32_t track_id, float eg_amount)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const float next = clampf_local(eg_amount, -1.0f, 1.0f);
    if (filter->eg_amount == next) return;
    filter->eg_amount = next;
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_EG_AMOUNT);
}

void mixer_set_track_filter_attack(uint32_t track_id, float attack_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_env_units(attack_s, filter->sample_rate);
    if (filter->filter_env.attack == next) return;
    env_adsr_set_attack(&filter->filter_env, next);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_FILTER_ATTACK);
}

void mixer_set_track_filter_decay(uint32_t track_id, float decay_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_env_units(decay_s, filter->sample_rate);
    if (filter->filter_env.decay == next) return;
    env_adsr_set_decay(&filter->filter_env, next);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_FILTER_DECAY);
}

void mixer_set_track_filter_sustain(uint32_t track_id, float sustain)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_sustain_to_env_units(sustain);
    if (filter->filter_env.sustain == next) return;
    env_adsr_set_sustain(&filter->filter_env, next);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_FILTER_SUSTAIN);
}

void mixer_set_track_filter_release(uint32_t track_id, float release_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_env_units(release_s, filter->sample_rate);
    if (filter->filter_env.release == next) return;
    env_adsr_set_release(&filter->filter_env, next);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_FILTER_RELEASE);
}

void mixer_set_track_filter_keytrack(uint32_t track_id, float amount)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const float next = clampf_local(amount, 0.0f, 1.0f);
    if (filter->keytrack == next) return;
    filter->keytrack = next;
    filter->keytrack_ratio_target = mixer_track_filter_keytrack_ratio(filter);
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_filter_env_reset(uint32_t track_id, uint8_t enabled)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    (void)enabled;
}

void mixer_set_track_filter_env_delay(uint32_t track_id, float delay_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    (void)delay_s;
}

void mixer_set_track_filter_retrigger_hard(uint32_t track_id, uint8_t hard)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint8_t next = (hard != 0U) ? 1U : 0U;
    if (filter->filter_retrigger_hard == next) return;
    filter->filter_retrigger_hard = next;
    mixer_track_filter_touch_config(filter);
}

void mixer_track_filter_note_on(uint32_t track_id, uint8_t midi_note, uint8_t velocity)
{
    (void)velocity;

    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->current_note = midi_note;
    filter->keytrack_ratio_target = mixer_track_filter_keytrack_ratio(filter);
    filter->note_active = 1U;
    env_adsr_retrigger(&filter->filter_env, filter->filter_retrigger_hard != 0U);
}

void mixer_set_track_vca_attack(uint32_t track_id, float attack_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_env_units(attack_s, filter->sample_rate);
    const float synth_next = (attack_s < 0.0f) ? 0.0f : attack_s;
    if ((filter->vca_env.attack == next)
            && (filter->synth_vca_env.attack_time == synth_next))
        return;
    if (filter->vca_env.attack != next)
        env_adsr_set_attack(&filter->vca_env, next);
    vca_env_set_attack(&filter->synth_vca_env, attack_s);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_VCA_ATTACK);
}

void mixer_set_track_vca_decay(uint32_t track_id, float decay_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_env_units(decay_s, filter->sample_rate);
    const float synth_next = (decay_s < 0.0f) ? 0.0f : decay_s;
    if ((filter->vca_env.decay == next)
            && (filter->synth_vca_env.decay_time == synth_next))
        return;
    if (filter->vca_env.decay != next)
        env_adsr_set_decay(&filter->vca_env, next);
    vca_env_set_decay(&filter->synth_vca_env, decay_s);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_VCA_DECAY);
}

void mixer_set_track_vca_sustain(uint32_t track_id, float sustain)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_sustain_to_env_units(sustain);
    const float synth_next = (sustain <= 0.0f) ? 0.0f
                           : ((sustain >= 1.0f) ? 1.0f : sustain);
    if ((filter->vca_env.sustain == next)
            && (filter->synth_vca_env.sustain == synth_next))
        return;
    if (filter->vca_env.sustain != next)
        env_adsr_set_sustain(&filter->vca_env, next);
    vca_env_set_sustain(&filter->synth_vca_env, sustain);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_VCA_SUSTAIN);
}

void mixer_set_track_vca_release(uint32_t track_id, float release_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_env_units(release_s, filter->sample_rate);
    const float synth_next = (release_s < 0.0f) ? 0.0f : release_s;
    if ((filter->vca_env.release == next)
            && (filter->synth_vca_env.release_time == synth_next))
        return;
    if (filter->vca_env.release != next)
        env_adsr_set_release(&filter->vca_env, next);
    vca_env_set_release(&filter->synth_vca_env, release_s);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_VCA_RELEASE);
}

void mixer_set_track_filter_mode(uint32_t track_id, uint8_t mode)
{
    if (track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint8_t next = (mode > 2U) ? 2U : mode;
    if (filter->filter_mode == next)
        return;
    filter->filter_mode = next;
    if(next == 2U) fx_deluge_filter_init(&filter->deluge, filter->sample_rate);
    else mixer_track_filter_reset_dsp(filter);
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_vca_enabled(uint32_t track_id, uint8_t enabled)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint8_t next = (enabled != 0U) ? 1U : 0U;
    const uint8_t changed = (filter->vca_enabled != next) ? 1U : 0U;
    filter->vca_enabled = next;
    if (changed != 0U) mixer_track_filter_touch_config(filter);
    if (enabled == 0U)
    {
        filter->vca_note_active = 0U;
        filter->vca_note_count = 0U;
        filter->vca_current_note = MIXER_FILTER_NOTE_REF_MIDI;
        filter->vca_gate = 0U;
        filter->vca_env_value = 0.0f;
        env_adsr_reset(&filter->vca_env);
    }
}

void mixer_set_track_vca_retrigger_hard(uint32_t track_id, uint8_t hard)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint8_t next = (hard != 0U) ? 1U : 0U;
    if (filter->vca_retrigger_hard == next) return;
    filter->vca_retrigger_hard = next;
    mixer_track_filter_touch_config(filter);
}

void mixer_track_vca_note_on(uint32_t track_id, uint8_t midi_note, uint8_t velocity)
{
    (void)velocity;

    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->vca_enabled = 1U;
    filter->vca_current_note = midi_note;
    if (filter->vca_note_count < 0xFFU)
    {
        filter->vca_note_count++;
    }
    if (filter->vca_note_active == 0U)
    {
        filter->vca_note_active = 1U;
        filter->vca_gate = 1U;
        env_adsr_retrigger(&filter->vca_env, filter->vca_retrigger_hard != 0U);
    }
}

void mixer_track_vca_note_off(uint32_t track_id, uint8_t midi_note)
{
    (void)midi_note;

    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    if((filter->vca_enabled == 0U) || (filter->vca_note_active == 0U))
        return;

    if (filter->vca_note_count > 0U)
    {
        filter->vca_note_count--;
    }
    if (filter->vca_note_count == 0U)
    {
        filter->vca_note_active = 0U;
        filter->vca_gate = 0U;
        env_adsr_gate_off(&filter->vca_env);
    }
}

void mixer_track_vca_all_notes_off(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->vca_note_active = 0U;
    filter->vca_note_count = 0U;
    filter->vca_current_note = MIXER_FILTER_NOTE_REF_MIDI;
    filter->vca_gate = 0U;
    filter->vca_env_value = 0.0f;
    env_adsr_reset(&filter->vca_env);
}

uint8_t mixer_track_vca_is_running(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return 0U;

    const mixer_track_filter_t *const filter = &g_track_filters[track_id];
    if (filter->vca_enabled == 0U)
        return 0U;

    return (env_adsr_stage(&filter->vca_env) != ENV_ADSR_STAGE_IDLE) ? 1U : 0U;
}

uint8_t mixer_track_vca_requires_source(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return 1U;

    const mixer_track_filter_t *const filter = &g_track_filters[track_id];
    if (filter->vca_enabled == 0U)
        return 1U;

    return (env_adsr_stage(&filter->vca_env) != ENV_ADSR_STAGE_IDLE) ? 1U : 0U;
}

float mixer_get_track_vca_env_value(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return 0.0f;

    return clampf_local(g_track_filters[track_id].vca_env_value, 0.0f, 1.0f);
}

float mixer_get_track_filter_env_value(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return 0.0f;

    return clampf_local(g_track_filters[track_id].filter_env_value, 0.0f, 1.0f);
}

float mixer_prepare_track_filter_env_source(uint32_t track_id, uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS) || (frames == 0U))
    {
        return 0.0f;
    }

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    const int16_t segment_first = env_adsr_value(&filter->filter_env);
    uint32_t offset = 0U;
    uint8_t count = 0U;
    int16_t terminal = segment_first;
    while ((offset < frames) && (count < (AUDIO_BLOCK_SIZE / MIXER_FILTER_UPDATE_PERIOD)))
    {
        uint32_t chunk = frames - offset;
        if (chunk > MIXER_FILTER_UPDATE_PERIOD)
        {
            chunk = MIXER_FILTER_UPDATE_PERIOD;
        }
        int16_t first = 0;
        terminal = env_adsr_process_advance(&filter->filter_env, chunk, &first);
        filter->filter_env_prepared_first[count] = first;
        filter->filter_env_prepared_terminal[count] = terminal;
        count++;
        offset += chunk;
    }

    filter->filter_env_value = (float)terminal * (1.0f / 32767.0f);
    filter->filter_env_prepared_frames = (uint16_t)frames;
    filter->filter_env_prepared_count = count;
    filter->filter_env_prepared_consumed = 0U;
    return clampf_local((float)segment_first * (1.0f / 32767.0f), 0.0f, 1.0f);
}

void mixer_track_filter_note_off(uint32_t track_id, uint8_t midi_note)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    if(filter->note_active == 0U || filter->current_note != midi_note)
        return;

    filter->note_active = 0U;
    env_adsr_gate_off(&filter->filter_env);
}

void mixer_track_filter_all_notes_off(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->note_active = 0U;
    filter->current_note = MIXER_FILTER_NOTE_REF_MIDI;
    filter->keytrack_ratio_target = 1.0f;
    filter->filter_env_value = 0.0f;
    env_adsr_reset(&filter->filter_env);
}

void __attribute__((used)) mixer_external_inputs_clear(void)
{
    g_external_lane_mask = 0U;
    memset(g_external_track_enabled, 0, sizeof(g_external_track_enabled));
    memset(g_external_poly_initialized, 0, sizeof(g_external_poly_initialized));
    memset(g_external_track_format, 0, sizeof(g_external_track_format));
    memset(g_external_track_frames_valid, 0, sizeof(g_external_track_frames_valid));
}

void __attribute__((used)) mixer_submit_external_mono_native(uint32_t track_id, const float *mono, uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS) || (mono == NULL))
    {
        return;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    if (g_external_track_enabled[track_id] != 0U)
    {
        return;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        g_external_track_mono[track_id][i] = mono[i];
    }

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_MONO_NATIVE;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

void __attribute__((used)) mixer_submit_external_stereo(uint32_t track_id,
                                                        const float *left,
                                                        const float *right,
                                                        uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS) || (left == NULL) || (right == NULL))
    {
        return;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    if (g_external_track_enabled[track_id] != 0U)
    {
        return;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        g_external_track_l[track_id][i] = left[i];
        g_external_track_r[track_id][i] = right[i];
    }

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_STEREO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

uint8_t __attribute__((used)) mixer_begin_external_mono_native(uint32_t track_id,
                                                               uint32_t frames,
                                                               float **out_mono)
{
    if (out_mono != NULL)
    {
        *out_mono = NULL;
    }

    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (out_mono == NULL)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return 0U;
    }

    *out_mono = g_external_track_mono[track_id];
    return 1U;
}

void __attribute__((used)) mixer_commit_external_mono_native(uint32_t track_id, uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return;
    }

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_MONO_NATIVE;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

uint8_t __attribute__((used)) mixer_begin_external_multi_mono(uint32_t track_id,
                                                              uint32_t frames,
                                                              float **out_mono)
{
    if (out_mono != NULL)
    {
        *out_mono = NULL;
    }

    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (out_mono == NULL)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return 0U;
    }

    *out_mono = g_external_track_mono[track_id];
    return 1U;
}

void __attribute__((used)) mixer_commit_external_multi_mono(uint32_t track_id,
                                                            uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return;
    }

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_MULTI_MONO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

uint8_t __attribute__((used)) mixer_begin_external_stereo(uint32_t track_id,
                                                          uint32_t frames,
                                                          float **out_left,
                                                          float **out_right)
{
    if (out_left != NULL)
    {
        *out_left = NULL;
    }
    if (out_right != NULL)
    {
        *out_right = NULL;
    }

    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (out_left == NULL)
            || (out_right == NULL)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return 0U;
    }

    *out_left = g_external_track_l[track_id];
    *out_right = g_external_track_r[track_id];
    return 1U;
}

void __attribute__((used)) mixer_commit_external_stereo(uint32_t track_id, uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return;
    }

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_STEREO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

uint8_t __attribute__((used)) mixer_begin_external_multi_stereo(uint32_t track_id,
                                                                uint32_t frames,
                                                                float **out_left,
                                                                float **out_right)
{
    if (out_left != NULL)
    {
        *out_left = NULL;
    }
    if (out_right != NULL)
    {
        *out_right = NULL;
    }

    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (out_left == NULL)
            || (out_right == NULL)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return 0U;
    }

    *out_left = g_external_track_l[track_id];
    *out_right = g_external_track_r[track_id];
    return 1U;
}

void __attribute__((used)) mixer_commit_external_multi_stereo(uint32_t track_id,
                                                              uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return;
    }

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_MULTI_STEREO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

uint8_t mixer_begin_external_poly(uint32_t track_id, uint32_t frames)
{
    if ((track_id >= 8U) || (frames == 0U) || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U))
        return 0U;
    g_external_poly_initialized[track_id] = 0U;
    return 1U;
}

ITCM_TEXT uint8_t mixer_process_external_poly_voice_prepared(uint32_t mix_track_id,
                                                   uint32_t poly_track_id,
                                                   uint8_t voice,
                                                   float *mono,
                                                   uint32_t frames,
                                                   float voice_pan)
{
    mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
    if ((filter == NULL) || (mix_track_id >= MIXER_MAX_TRACKS) || (mono == NULL)
            || (frames == 0U) || (frames > AUDIO_BLOCK_SIZE))
        return 0U;

    const uint8_t voice_slot = synth_polyphony_get_slot((uint8_t)poly_track_id, voice);
    const mixer_poly_cutoff_override_t *const poly_cutoff =
        (voice_slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
            ? &g_poly_cutoff_override[voice_slot] : NULL;
    (void)mixer_track_filter_process_block_mono(filter, mono, frames, poly_cutoff);
    const float clamped_pan = clamp_pan(voice_pan);
    const float pan_for_mix = -clamped_pan;
    const float pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
    const float pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
    float *unit_output = g_external_track_l[mix_track_id];
    float *attenuated_output = g_external_track_r[mix_track_id];
    float attenuated_pan = pan_r;
    if (clamped_pan < 0.0f)
    {
        unit_output = g_external_track_r[mix_track_id];
        attenuated_output = g_external_track_l[mix_track_id];
        attenuated_pan = pan_l;
    }
    uint32_t i = 0U;
    const uint8_t poly_initialized =
        g_external_poly_initialized[mix_track_id];
    float vca_gain[AUDIO_BLOCK_SIZE];
    const uint32_t vca_frames = vca_env_process_block(&filter->synth_vca_env,
                                                       vca_gain,
                                                       frames);
    for (; i < vca_frames; ++i)
    {
        const float vca = vca_gain[i];
        filter->vca_env_value = vca;
        const float scaled = mono[i] * vca;
        const float attenuated = scaled * attenuated_pan;
        if (poly_initialized == 0U)
        {
            attenuated_output[i] = attenuated;
            unit_output[i] = scaled;
        }
        else
        {
            attenuated_output[i] += attenuated;
            unit_output[i] += scaled;
        }
    }
    if (poly_initialized == 0U)
    {
        if (i < frames)
        {
            memset(&g_external_track_l[mix_track_id][i],
                   0,
                   (frames - i) * sizeof(float));
            memset(&g_external_track_r[mix_track_id][i],
                   0,
                   (frames - i) * sizeof(float));
        }
        g_external_poly_initialized[mix_track_id] = 1U;
    }
    if (i < frames)
    {
        memset(&mono[i], 0, (frames - i) * sizeof(float));
        filter->vca_env_value = 0.0f;
    }
    return (vca_env_stage(&filter->synth_vca_env) != VCA_ENV_IDLE);
}

uint8_t mixer_process_external_poly_voice(uint32_t mix_track_id,
                                          uint32_t poly_track_id,
                                          uint8_t voice,
                                          float *mono,
                                          uint32_t frames,
                                          float voice_pan)
{
    mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
    if ((filter == NULL) || (mix_track_id >= MIXER_MAX_TRACKS)) return 0U;
    mixer_poly_filter_sync_config(filter, &g_track_filters[mix_track_id]);
    return mixer_process_external_poly_voice_prepared(mix_track_id, poly_track_id,
                                                      voice, mono, frames, voice_pan);
}

void mixer_prepare_external_poly_voice(uint32_t mix_track_id,
                                       uint32_t poly_track_id,
                                       uint8_t voice)
{
    mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
    if ((filter != NULL) && (mix_track_id < MIXER_MAX_TRACKS))
    {
        mixer_poly_filter_sync_config(filter, &g_track_filters[mix_track_id]);
    }
}

void mixer_invalidate_external_poly_track(uint32_t poly_track_id)
{
    for (uint8_t voice = 0U; voice < SYNTH_POLYPHONY_MAX_VOICES; ++voice)
    {
        mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
        if (filter != NULL)
        {
            filter->config_version = UINT32_MAX;
            filter->continuous_epoch = UINT32_MAX;
            const uint8_t slot = synth_polyphony_get_slot((uint8_t)poly_track_id, voice);
            if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                g_poly_cutoff_override[slot].valid = 0U;
        }
    }
}

void mixer_poly_voice_set_cutoff(uint8_t slot, float value)
{
    if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
    {
        g_poly_cutoff_override[slot].effective_hz = value;
        g_poly_cutoff_override[slot].valid = 1U;
    }
}
void mixer_poly_voice_set_resonance(uint8_t slot, float value)
{
    if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
    {
        g_poly_filters_hot[slot].resonance = value;
        g_poly_filters_hot[slot].resonance_target = value;
    }
}
void mixer_poly_voice_set_eg_amount(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) g_poly_filters_hot[slot].eg_amount = value; }
void mixer_poly_voice_set_filter_attack(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) env_adsr_set_attack(&g_poly_filters_hot[slot].filter_env, mixer_track_filter_time_s_to_env_units(value, g_poly_filters_hot[slot].sample_rate)); }
void mixer_poly_voice_set_filter_decay(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) env_adsr_set_decay(&g_poly_filters_hot[slot].filter_env, mixer_track_filter_time_s_to_env_units(value, g_poly_filters_hot[slot].sample_rate)); }
void mixer_poly_voice_set_filter_sustain(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) env_adsr_set_sustain(&g_poly_filters_hot[slot].filter_env, mixer_track_filter_sustain_to_env_units(value)); }
void mixer_poly_voice_set_filter_release(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) env_adsr_set_release(&g_poly_filters_hot[slot].filter_env, mixer_track_filter_time_s_to_env_units(value, g_poly_filters_hot[slot].sample_rate)); }
void mixer_poly_voice_set_vca_attack(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) vca_env_set_attack(&g_poly_filters_hot[slot].synth_vca_env, value); }
void mixer_poly_voice_set_vca_decay(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) vca_env_set_decay(&g_poly_filters_hot[slot].synth_vca_env, value); }
void mixer_poly_voice_set_vca_sustain(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) vca_env_set_sustain(&g_poly_filters_hot[slot].synth_vca_env, value); }
void mixer_poly_voice_set_vca_release(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) vca_env_set_release(&g_poly_filters_hot[slot].synth_vca_env, value); }

void mixer_commit_external_poly(uint32_t track_id, uint32_t frames)
{
    if ((track_id >= 8U) || (frames == 0U) || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U)
            || (g_external_poly_initialized[track_id] == 0U))
        return;
    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_POLY_STEREO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

void mixer_track_poly_note_on(uint32_t poly_track_id,
                              uint32_t mix_track_id,
                              uint8_t voice,
                              uint8_t note,
                              uint8_t velocity)
{
    (void)velocity;
    if (mix_track_id >= MIXER_MAX_TRACKS)
        return;
    mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
    if (filter == NULL)
        return;
    mixer_poly_filter_sync_config(filter, &g_track_filters[mix_track_id]);
    filter->current_note = note;
    filter->keytrack_ratio_target = mixer_track_filter_keytrack_ratio(filter);
    filter->note_active = 1U;
    env_adsr_retrigger(&filter->filter_env, filter->filter_retrigger_hard != 0U);
    filter->vca_current_note = note;
    filter->vca_note_count = 1U;
    filter->vca_note_active = 1U;
    filter->vca_gate = 1U;
    vca_env_retrigger(&filter->synth_vca_env,
                      filter->vca_retrigger_hard != 0U);
}

void mixer_track_poly_note_off(uint32_t poly_track_id, uint8_t voice, uint8_t note)
{
    mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
    if ((filter == NULL) || (filter->current_note != note))
        return;
    filter->note_active = 0U;
    env_adsr_gate_off(&filter->filter_env);
    filter->vca_note_active = 0U;
    filter->vca_note_count = 0U;
    filter->vca_gate = 0U;
    vca_env_gate_off(&filter->synth_vca_env);
}

void mixer_track_poly_all_notes_off(uint32_t poly_track_id)
{
    for (uint8_t voice = 0U; voice < SYNTH_POLYPHONY_MAX_VOICES; ++voice)
    {
        mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
        if (filter != NULL)
        {
            filter->note_active = 0U;
            filter->vca_note_active = 0U;
            filter->vca_note_count = 0U;
            filter->vca_gate = 0U;
            env_adsr_gate_off(&filter->filter_env);
            vca_env_gate_off(&filter->synth_vca_env);
        }
    }
}

void mixer_synth_voice_slot_reset(uint8_t slot)
{
    if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
    {
        mixer_track_filter_init(&g_poly_filters_hot[slot], MIXER_FILTER_SAMPLE_RATE_DEFAULT);
        g_poly_cutoff_override[slot].effective_hz = MIXER_FILTER_CUTOFF_MAX_HZ;
        g_poly_cutoff_override[slot].valid = 0U;
    }
}

static void mixer_copy_voice_envelope_state(mixer_track_filter_t *destination,
                                            const mixer_track_filter_t *source)
{
    destination->filter_env = source->filter_env;
    destination->vca_env = source->vca_env;
    destination->synth_vca_env = source->synth_vca_env;
    destination->vca_env_value = source->vca_env_value;
    destination->filter_env_value = source->filter_env_value;
    destination->note_active = source->note_active;
    destination->current_note = source->current_note;
    destination->vca_note_active = source->vca_note_active;
    destination->vca_note_count = source->vca_note_count;
    destination->vca_current_note = source->vca_current_note;
    destination->vca_gate = source->vca_gate;
}

#define MIXER_SEND_MASK_BIT(send_index) ((uint8_t)(1U << (send_index)))

typedef struct
{
    uint8_t count;
    uint8_t assign_mask;
    uint8_t index[MIXER_NUM_SENDS];
    float current[MIXER_NUM_SENDS];
    float step[MIXER_NUM_SENDS];
} mixer_send_plan_t;

static inline uint8_t mixer_send_destination_process_mask(uint8_t reverb_active,
                                                          uint8_t delay_active,
                                                          uint8_t modfx_active)
{
    uint8_t mask = 0U;
    if ((reverb_active != 0U) || (g_send_fx_slot[MIXER_REVERB_SEND_INDEX] >= 0))
        mask |= MIXER_SEND_MASK_BIT(MIXER_REVERB_SEND_INDEX);
    if (delay_active != 0U)
        mask |= MIXER_SEND_MASK_BIT(MIXER_DELAY_SEND_INDEX);
    if (modfx_active != 0U)
        mask |= MIXER_SEND_MASK_BIT(MIXER_MODFX_SEND_INDEX);
    return mask;
}

static inline uint8_t mixer_track_input_send_mask(
    const mixer_audio_track_runtime_t *track,
    uint8_t destination_mask)
{
    uint8_t mask = 0U;
    for (uint32_t send = 0U; send < MIXER_NUM_SENDS; ++send)
    {
        const uint8_t bit = MIXER_SEND_MASK_BIT(send);
        if (((destination_mask & bit) != 0U)
                && ((track->send_level_current[send] != 0.0f)
                    || (track->send_level[send] != 0.0f)))
        {
            mask |= bit;
        }
    }
    return mask;
}

static uint8_t mixer_collect_input_send_masks(
    uint32_t lane_mask,
    uint8_t group_active,
    uint8_t destination_mask,
    uint8_t lane_send_masks[MIXER_MAX_TRACKS],
    uint8_t *group_send_mask)
{
    uint8_t mask = 0U;
    while (lane_mask != 0U)
    {
        const uint32_t lane = (uint32_t)__builtin_ctz(lane_mask);
        lane_mask &= lane_mask - 1U;
        const uint8_t lane_mask_value =
            ((group_active != 0U)
                && ((g_mixer_static_lane_flags[lane]
                    & MIXER_STATIC_GROUP_CHILD) != 0U))
            ? 0U
            : mixer_track_input_send_mask(&g_tracks[lane], destination_mask);
        lane_send_masks[lane] = lane_mask_value;
        mask |= lane_mask_value;
    }
    if (group_active != 0U)
    {
        *group_send_mask = mixer_track_input_send_mask(
            &g_tracks[MIXER_GROUP_BUS_TRACK], destination_mask);
        mask |= *group_send_mask;
    }
    return mask;
}

static inline mixer_send_plan_t mixer_prepare_send_plan(
    const mixer_audio_track_runtime_t *track,
    uint8_t lane_send_mask,
    uint8_t first_writer_mask,
    float inv_frames)
{
    mixer_send_plan_t plan = {0};
    for (uint32_t send = 0U; send < MIXER_NUM_SENDS; ++send)
    {
        if ((lane_send_mask & MIXER_SEND_MASK_BIT(send)) == 0U)
            continue;

        const uint8_t plan_index = plan.count++;
        const float current = track->send_level_current[send];
        const float target = track->send_level[send];
        plan.index[plan_index] = (uint8_t)send;
        if ((first_writer_mask & MIXER_SEND_MASK_BIT(send)) != 0U)
            plan.assign_mask |= MIXER_SEND_MASK_BIT(plan_index);
        plan.current[plan_index] = current;
        if (target != current)
            plan.step[plan_index] = (target - current) * inv_frames;
    }
    return plan;
}

static inline void mixer_finish_send_levels(mixer_audio_track_runtime_t *track)
{
    for (uint32_t send = 0U; send < MIXER_NUM_SENDS; ++send)
        track->send_level_current[send] = track->send_level[send];
}

static inline uint8_t mixer_group_fx_input_mask(
    const mixer_audio_track_runtime_t *track)
{
    uint8_t mask = 0U;
    for (uint8_t slot = 0U; slot < 2U; ++slot)
    {
        if ((track->group_fx_level_current[slot] != 0.0f)
                || (track->group_fx_level[slot] != 0.0f))
            mask |= (uint8_t)(1U << slot);
    }
    return mask;
}

typedef struct
{
    float current[2];
    float step[2];
    uint8_t mask;
    uint8_t assign_mask;
} mixer_group_fx_plan_t;

static inline mixer_group_fx_plan_t mixer_prepare_group_fx_plan(
    const mixer_audio_track_runtime_t *track,
    uint8_t group_child,
    uint8_t *written_mask,
    float inv_frames)
{
    mixer_group_fx_plan_t plan = {0};
    if ((group_child == 0U) || (written_mask == NULL))
        return plan;
    plan.mask = mixer_group_fx_input_mask(track);
    plan.assign_mask = (uint8_t)(plan.mask & (uint8_t)~*written_mask);
    *written_mask |= plan.mask;
    for (uint8_t slot = 0U; slot < 2U; ++slot)
    {
        plan.current[slot] = track->group_fx_level_current[slot];
        plan.step[slot] = (track->group_fx_level[slot] - plan.current[slot])
            * inv_frames;
    }
    return plan;
}

static inline void mixer_accumulate_group_fx_sample(
    mixer_group_fx_plan_t *plan,
    float bus_l[2][AUDIO_BLOCK_SIZE],
    float bus_r[2][AUDIO_BLOCK_SIZE],
    uint32_t frame,
    float left_trimmed,
    float right_trimmed)
{
    for (uint8_t slot = 0U; slot < 2U; ++slot)
    {
        const uint8_t bit = (uint8_t)(1U << slot);
        if ((plan->mask & bit) == 0U)
            continue;
        const float left = left_trimmed * plan->current[slot];
        const float right = right_trimmed * plan->current[slot];
        if ((plan->assign_mask & bit) != 0U)
        {
            bus_l[slot][frame] = left;
            bus_r[slot][frame] = right;
        }
        else
        {
            bus_l[slot][frame] += left;
            bus_r[slot][frame] += right;
        }
        plan->current[slot] += plan->step[slot];
    }
}

static inline void mixer_finish_group_fx_levels(
    mixer_audio_track_runtime_t *track,
    const mixer_group_fx_plan_t *plan)
{
    for (uint8_t slot = 0U; slot < 2U; ++slot)
    {
        if ((plan->mask & (uint8_t)(1U << slot)) != 0U)
            track->group_fx_level_current[slot] = track->group_fx_level[slot];
    }
}

static inline void mixer_accumulate_group_fx(
    mixer_audio_track_runtime_t *track,
    const float *left,
    const float *right,
    float bus_l[2][AUDIO_BLOCK_SIZE],
    float bus_r[2][AUDIO_BLOCK_SIZE],
    uint32_t frames,
    uint8_t *written_mask)
{
    const uint8_t mask = mixer_group_fx_input_mask(track);
    if ((mask == 0U) || (written_mask == NULL))
        return;
    const float inv_frames = (frames > 0U) ? (1.0f / (float)frames) : 0.0f;
    for (uint8_t slot = 0U; slot < 2U; ++slot)
    {
        const uint8_t bit = (uint8_t)(1U << slot);
        if ((mask & bit) == 0U)
            continue;
        float level = track->group_fx_level_current[slot];
        const float target = track->group_fx_level[slot];
        const float step = (target - level) * inv_frames;
        if ((*written_mask & bit) == 0U)
        {
            for (uint32_t i = 0U; i < frames; ++i)
            {
                const float gain = level * MIXER_TRACK_NOMINAL_TRIM;
                bus_l[slot][i] = left[i] * gain;
                bus_r[slot][i] = right[i] * gain;
                level += step;
            }
            *written_mask |= bit;
        }
        else
        {
            for (uint32_t i = 0U; i < frames; ++i)
            {
                const float gain = level * MIXER_TRACK_NOMINAL_TRIM;
                bus_l[slot][i] += left[i] * gain;
                bus_r[slot][i] += right[i] * gain;
                level += step;
            }
        }
        track->group_fx_level_current[slot] = target;
    }
}

static void mixer_clear_send_buffers(uint8_t clear_mask,
                                     float send_l[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE],
                                     float send_r[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE],
                                     uint32_t frames)
{
    for (uint32_t send = 0U; send < MIXER_NUM_SENDS; ++send)
    {
        if ((clear_mask & MIXER_SEND_MASK_BIT(send)) == 0U)
            continue;
        memset(send_l[send], 0, sizeof(float) * frames);
        memset(send_r[send], 0, sizeof(float) * frames);
    }
}

static void mixer_accumulate_send_plan_stereo(
    mixer_send_plan_t *plan,
    const float *left,
    const float *right,
    float send_l[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE],
    float send_r[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE],
    uint32_t frames,
    float trim)
{
    if (plan->assign_mask == 0U)
    {
        switch (plan->count)
        {
        case 3U:
        {
            const uint8_t send0 = plan->index[0];
            const uint8_t send1 = plan->index[1];
            const uint8_t send2 = plan->index[2];
            float gain0 = plan->current[0];
            float gain1 = plan->current[1];
            float gain2 = plan->current[2];
            const float step0 = plan->step[0];
            const float step1 = plan->step[1];
            const float step2 = plan->step[2];
            for (uint32_t frame = 0U; frame < frames; ++frame)
            {
                const float l = left[frame] * trim;
                const float r = right[frame] * trim;
                send_l[send0][frame] += l * gain0;
                send_r[send0][frame] += r * gain0;
                send_l[send1][frame] += l * gain1;
                send_r[send1][frame] += r * gain1;
                send_l[send2][frame] += l * gain2;
                send_r[send2][frame] += r * gain2;
                gain0 += step0;
                gain1 += step1;
                gain2 += step2;
            }
            plan->current[0] = gain0;
            plan->current[1] = gain1;
            plan->current[2] = gain2;
            return;
        }
        case 2U:
        {
            const uint8_t send0 = plan->index[0];
            const uint8_t send1 = plan->index[1];
            float gain0 = plan->current[0];
            float gain1 = plan->current[1];
            const float step0 = plan->step[0];
            const float step1 = plan->step[1];
            for (uint32_t frame = 0U; frame < frames; ++frame)
            {
                const float l = left[frame] * trim;
                const float r = right[frame] * trim;
                send_l[send0][frame] += l * gain0;
                send_r[send0][frame] += r * gain0;
                send_l[send1][frame] += l * gain1;
                send_r[send1][frame] += r * gain1;
                gain0 += step0;
                gain1 += step1;
            }
            plan->current[0] = gain0;
            plan->current[1] = gain1;
            return;
        }
        case 1U:
        {
            const uint8_t send = plan->index[0];
            float gain = plan->current[0];
            const float step = plan->step[0];
            for (uint32_t frame = 0U; frame < frames; ++frame)
            {
                const float l = left[frame] * trim;
                const float r = right[frame] * trim;
                send_l[send][frame] += l * gain;
                send_r[send][frame] += r * gain;
                gain += step;
            }
            plan->current[0] = gain;
            return;
        }
        default:
            return;
        }
    }

    /* First contributors initialize their bus; mixed first/later-writer plans
     * stay outside the common all-accumulate hot kernels above. */
    for (uint32_t active = 0U; active < plan->count; ++active)
    {
        const uint8_t send = plan->index[active];
        float current = plan->current[active];
        const float step = plan->step[active];
        if ((plan->assign_mask & MIXER_SEND_MASK_BIT(active)) != 0U)
        {
            for (uint32_t frame = 0U; frame < frames; ++frame)
            {
                send_l[send][frame] = left[frame] * trim * current;
                send_r[send][frame] = right[frame] * trim * current;
                current += step;
            }
        }
        else
        {
            for (uint32_t frame = 0U; frame < frames; ++frame)
            {
                send_l[send][frame] += left[frame] * trim * current;
                send_r[send][frame] += right[frame] * trim * current;
                current += step;
            }
        }
        plan->current[active] = current;
    }
}

void mixer_synth_voice_slot_copy(uint8_t source_slot, uint8_t destination_slot)
{
    if ((source_slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
            || (destination_slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
            || (source_slot == destination_slot))
        return;
    mixer_copy_voice_envelope_state(&g_poly_filters_hot[destination_slot],
                                    &g_poly_filters_hot[source_slot]);
    g_poly_cutoff_override[destination_slot] = g_poly_cutoff_override[source_slot];
}

void mixer_track_voice_state_to_poly(uint32_t mix_track_id,
                                     uint32_t poly_track_id,
                                     uint8_t voice)
{
    mixer_track_filter_t *const destination = mixer_poly_filter(poly_track_id, voice);
    if ((destination == NULL) || (mix_track_id >= MIXER_MAX_TRACKS))
        return;
    mixer_poly_filter_sync_config(destination, &g_track_filters[mix_track_id]);
    mixer_copy_voice_envelope_state(destination, &g_track_filters[mix_track_id]);
    destination->vca_enabled = 1U;
}

void mixer_track_voice_state_from_poly(uint32_t mix_track_id,
                                       uint32_t poly_track_id,
                                       uint8_t voice)
{
    mixer_track_filter_t *const source = mixer_poly_filter(poly_track_id, voice);
    if ((source == NULL) || (mix_track_id >= MIXER_MAX_TRACKS))
        return;
    mixer_copy_voice_envelope_state(&g_track_filters[mix_track_id], source);
}

/**
 * @brief Traite un bloc de mixage final MAIN.
 *
 * @param tracks Tableau de tracks stéréo.
 * @param track_count Nombre de tracks valides.
 * @param frames Taille bloc en frames.
 *
 * Contexte d'appel:
 * - IRQ audio (hard realtime).
 */
/**
 * @brief Point d'entrée mixer_process.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_process.
 *
 * @param tracks Paramètre d'entrée de l'API.
 * @param track_count Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
