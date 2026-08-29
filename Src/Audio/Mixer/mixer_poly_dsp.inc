    }
    track->group_fx_level_current[0] = track->group_fx_level[0];
    track->group_fx_level_current[1] = track->group_fx_level[1];

    filter->cutoff_hz = filter->cutoff_target_hz;
    filter->cutoff_mod_hz = filter->cutoff_mod_target_hz;
    filter->resonance = filter->resonance_target;
    filter->keytrack_ratio = filter->keytrack_ratio_target;
    mixer_track_filter_apply_core_params(filter);
}

static ITCM_TEXT void mixer_track_filter_process_block(mixer_track_filter_t *filter,
                                             float *left,
                                             float *right,
                                             uint32_t frames)
{
    if((filter == NULL) || (left == NULL) || (right == NULL))
        return;

    if(filter->filter_mode != 2U)
        mixer_track_filter_set_dsp_format(filter, MIXER_FILTER_DSP_STEREO);

    if(filter->filter_mode == 0U)
    {
        filter->filter_env_prepared_consumed = 1U;
        return;
    }

    const float cutoff_start_hz = filter->cutoff_hz;
    const float cutoff_mod_start_hz = filter->cutoff_mod_hz;
    const float resonance_start = filter->resonance;
    const float keytrack_ratio_start = filter->keytrack_ratio;
    filter->cutoff_hz = mixer_smooth_block(cutoff_start_hz,
                                           filter->cutoff_target_hz,
                                           MIXER_FILTER_BLOCK_SMOOTH);
    filter->cutoff_mod_hz = filter->cutoff_mod_target_hz;
    filter->resonance = mixer_smooth_block(resonance_start, filter->resonance_target, MIXER_FILTER_BLOCK_SMOOTH);
    filter->keytrack_ratio = filter->keytrack_ratio_target;
    mixer_track_filter_process_biquad_stereo_block(filter, left, right, frames,
                                                   cutoff_start_hz, cutoff_mod_start_hz,
                                                   resonance_start, keytrack_ratio_start);
}

static void mixer_track_filter_process_biquad_stereo_block(mixer_track_filter_t *filter,
                                                           float *left,
                                                           float *right,
                                                           uint32_t frames,
                                                           float cutoff_start_hz,
                                                           float cutoff_mod_start_hz,
                                                           float resonance_start,
                                                           float keytrack_ratio_start)
{
    uint32_t i = 0U;
    const uint8_t filter_env_modulated = (filter->eg_amount != 0.0f) ? 1U : 0U;
    if ((filter->morph_ramp_remaining == 0U)
            && (mixer_track_filter_env_control_is_static(filter, frames) != 0U)
            && ((filter->filter_mode == 2U) || (filter->biquad.bypass_xfade_remaining == 0U)))
    {
        const float env = mixer_track_filter_static_env_value(filter, frames);
        const float modulation_hz = filter->cutoff_mod_hz
            - filter->cutoff_target_hz;
        const float cutoff = mixer_track_filter_compute_modulated_cutoff(filter,
            filter->cutoff_hz, modulation_hz, filter->keytrack_ratio, env);
        if(filter->filter_mode == 2U)
        {
            fx_deluge_filter_configure(&filter->deluge,
                filter->morph,
                cutoff, filter->resonance);
            fx_deluge_filter_process(&filter->deluge, left, right, frames);
        }
        else
        {
            fx_biquad_filter_set_params(&filter->biquad, cutoff,
                mixer_track_filter_resonance_to_biquad_q(filter->resonance));
            fx_biquad_filter_process_block(&filter->biquad, left, right, frames);
        }
        return;
    }
    while(i < frames)
    {
        uint32_t chunk = frames - i;
        if(chunk > MIXER_FILTER_UPDATE_PERIOD)
        {
            chunk = MIXER_FILTER_UPDATE_PERIOD;
        }

        int16_t first_value = 0;
        int16_t terminal_value = env_adsr_value(&filter->filter_env);
        const uint8_t prepared =
            (uint8_t)((filter->filter_env_prepared_consumed == 0U)
                && (filter->filter_env_prepared_frames == frames)
                && ((i / MIXER_FILTER_UPDATE_PERIOD) < filter->filter_env_prepared_count));
        if (prepared != 0U)
        {
            first_value =
                filter->filter_env_prepared_first[i / MIXER_FILTER_UPDATE_PERIOD];
            terminal_value =
                filter->filter_env_prepared_terminal[i / MIXER_FILTER_UPDATE_PERIOD];
        }
        else
        {
            terminal_value =
                env_adsr_process_advance(&filter->filter_env, chunk, &first_value);
        }
        float env = 0.0f;
        if (filter_env_modulated != 0U)
        {
            const float env_first = (float)first_value * (1.0f / 32767.0f);
            const float env_terminal = (float)terminal_value * (1.0f / 32767.0f);
            env = (prepared != 0U) ? env_first : (0.5f * (env_first + env_terminal));
        }
        const float progress = (float)(i + chunk) / (float)frames;
        const float base_hz = cutoff_start_hz
            + ((filter->cutoff_hz - cutoff_start_hz) * progress);
        const float mod_absolute_hz = cutoff_mod_start_hz
            + ((filter->cutoff_mod_hz - cutoff_mod_start_hz) * progress);
        const float modulation_hz = mod_absolute_hz - filter->cutoff_target_hz;
        const float keytrack_ratio = keytrack_ratio_start
            + ((filter->keytrack_ratio - keytrack_ratio_start) * progress);
        const float resonance =
                resonance_start + ((filter->resonance - resonance_start) * progress);
        if (prepared == 0U)
        {
            filter->filter_env_value = (float)terminal_value * (1.0f / 32767.0f);
        }
        const float cutoff = mixer_track_filter_compute_modulated_cutoff(filter, base_hz,
                                                                          modulation_hz,
                                                                          keytrack_ratio, env);
        mixer_track_filter_advance_morph(filter, chunk);
        if(filter->filter_mode == 2U)
        {
            fx_deluge_filter_configure(&filter->deluge,
                filter->morph,
                cutoff, resonance);
            fx_deluge_filter_process(&filter->deluge, &left[i], &right[i], chunk);
        }
        else
        {
            fx_biquad_filter_set_params(&filter->biquad, cutoff,
                mixer_track_filter_resonance_to_biquad_q(resonance));
            fx_biquad_filter_process_block(&filter->biquad, &left[i], &right[i], chunk);
        }
        i += chunk;
    }
    filter->filter_env_prepared_consumed = 1U;
}

static void mixer_track_filter_process_biquad_mono_block(mixer_track_filter_t *filter,
                                                         float *mono,
                                                         uint32_t frames,
                                                         float cutoff_start_hz,
                                                         float cutoff_mod_start_hz,
                                                         float resonance_start,
                                                         float keytrack_ratio_start,
                                                         const mixer_poly_cutoff_override_t *poly_cutoff)
{
    uint32_t i = 0U;
    const uint8_t filter_env_modulated = (filter->eg_amount != 0.0f) ? 1U : 0U;
    const uint8_t poly_cutoff_valid = (poly_cutoff != NULL) ? poly_cutoff->valid : 0U;
    const float poly_cutoff_hz = (poly_cutoff_valid != 0U)
        ? poly_cutoff->effective_hz : 0.0f;
    const uint8_t control_static = mixer_track_filter_env_control_is_static(filter, frames);
    if ((filter->morph_ramp_remaining == 0U) && (control_static != 0U)
            && ((filter->filter_mode == 2U) || (filter->biquad_mono.bypass_xfade_remaining == 0U)))
    {
        const float env = mixer_track_filter_static_env_value(filter, frames);
        const float base_hz = (poly_cutoff_valid != 0U)
            ? poly_cutoff_hz : filter->cutoff_hz;
        const float modulation_hz = (poly_cutoff_valid != 0U)
            ? 0.0f : (filter->cutoff_mod_hz - filter->cutoff_target_hz);
        const float cutoff_arg = mixer_track_filter_compute_modulated_cutoff(filter,
            base_hz, modulation_hz, filter->keytrack_ratio, env);
        if(filter->filter_mode == 2U)
        {
            fx_deluge_filter_configure(&filter->deluge,
                filter->morph,
                cutoff_arg, filter->resonance);
            fx_deluge_filter_process_mono(&filter->deluge, mono, frames);
        }
        else
        {
            const float q_arg = mixer_track_filter_resonance_to_biquad_q(filter->resonance);
            fx_biquad_filter_mono_set_params(&filter->biquad_mono, cutoff_arg, q_arg);
            fx_biquad_filter_mono_process_block(&filter->biquad_mono, mono, frames);
        }
        return;
    }
    while(i < frames)
    {
        uint32_t chunk = frames - i;
        if(chunk > MIXER_FILTER_UPDATE_PERIOD)
        {
            chunk = MIXER_FILTER_UPDATE_PERIOD;
        }

        int16_t first_value = 0;
        int16_t terminal_value = env_adsr_value(&filter->filter_env);
        const uint8_t prepared =
            (uint8_t)((filter->filter_env_prepared_consumed == 0U)
                && (filter->filter_env_prepared_frames == frames)
                && ((i / MIXER_FILTER_UPDATE_PERIOD) < filter->filter_env_prepared_count));
        if (prepared != 0U)
        {
            first_value =
                filter->filter_env_prepared_first[i / MIXER_FILTER_UPDATE_PERIOD];
            terminal_value =
                filter->filter_env_prepared_terminal[i / MIXER_FILTER_UPDATE_PERIOD];
        }
        else
        {
            terminal_value =
                env_adsr_process_advance(&filter->filter_env, chunk, &first_value);
        }
        float env = 0.0f;
        if (filter_env_modulated != 0U)
        {
            const float env_first = (float)first_value * (1.0f / 32767.0f);
            const float env_terminal = (float)terminal_value * (1.0f / 32767.0f);
            env = (prepared != 0U) ? env_first : (0.5f * (env_first + env_terminal));
        }
        const float progress = (float)(i + chunk) / (float)frames;
        const float base_hz = (poly_cutoff_valid != 0U)
            ? poly_cutoff_hz
            : (cutoff_start_hz + ((filter->cutoff_hz - cutoff_start_hz) * progress));
        const float mod_absolute_hz = cutoff_mod_start_hz
            + ((filter->cutoff_mod_hz - cutoff_mod_start_hz) * progress);
        const float modulation_hz = (poly_cutoff_valid != 0U)
            ? 0.0f : (mod_absolute_hz - filter->cutoff_target_hz);
        const float keytrack_ratio = keytrack_ratio_start
            + ((filter->keytrack_ratio - keytrack_ratio_start) * progress);
        const float resonance =
                resonance_start + ((filter->resonance - resonance_start) * progress);
        if (prepared == 0U)
        {
            filter->filter_env_value = (float)terminal_value * (1.0f / 32767.0f);
        }
        const float cutoff_arg = mixer_track_filter_compute_modulated_cutoff(
            filter, base_hz, modulation_hz, keytrack_ratio, env);
        mixer_track_filter_advance_morph(filter, chunk);
        if(filter->filter_mode == 2U)
        {
            fx_deluge_filter_configure(&filter->deluge,
                filter->morph,
                cutoff_arg, resonance);
            fx_deluge_filter_process_mono(&filter->deluge, &mono[i], chunk);
        }
        else
        {
            const float q_arg = mixer_track_filter_resonance_to_biquad_q(resonance);
            fx_biquad_filter_mono_set_params(&filter->biquad_mono, cutoff_arg, q_arg);
            fx_biquad_filter_mono_process_block(&filter->biquad_mono, &mono[i], chunk);
        }
        i += chunk;
    }
    filter->filter_env_prepared_consumed = 1U;
}

static mixer_lane_plan_t mixer_build_lane_plan(uint32_t track_id,
                                               const mixer_audio_track_runtime_t *track,
                                               const mixer_track_filter_t *filter,
                                               uint8_t hw_enabled,
                                               uint8_t ext_enabled,
                                               uint8_t ext_format,
                                               uint32_t ext_frames,
                                               uint32_t frames)
{
    mixer_lane_plan_t plan;
    memset(&plan, 0, sizeof(plan));

    (void)track_id;
    plan.hw_enabled = hw_enabled;
    plan.ext_enabled = ext_enabled;
    plan.ext_format = ext_format;
    plan.ext_frames = ext_frames;

    if ((track == NULL) || (filter == NULL))
    {
        return plan;
    }

    if (((hw_enabled == 0U) && (ext_enabled == 0U))
            || ((track->mute != 0U) && (track->mute_gain_current <= 0.000001f)))
    {
        return plan;
    }

    if ((ext_enabled != 0U)
            && ((ext_frames != frames)
                || ((ext_format != MIXER_EXTERNAL_FORMAT_MONO_NATIVE)
                    && (ext_format != MIXER_EXTERNAL_FORMAT_STEREO)
                    && (ext_format != MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                    && (ext_format != MIXER_EXTERNAL_FORMAT_MULTI_STEREO)
                    && (ext_format != MIXER_EXTERNAL_FORMAT_MULTI_MONO))))
    {
        plan.ext_enabled = 0U;
        plan.ext_format = MIXER_EXTERNAL_FORMAT_NONE;
        plan.ext_frames = 0U;
    }

    if (((plan.hw_enabled == 0U) && (plan.ext_enabled == 0U))
            || ((track->mute != 0U) && (track->mute_gain_current <= 0.000001f)))
    {
        return plan;
    }

    plan.active = 1U;
    plan.exec_kind = MIXER_LANE_EXEC_STEREO;

    if (plan.hw_enabled != 0U)
    {
        if ((plan.ext_enabled != 0U)
            && ((plan.ext_format == MIXER_EXTERNAL_FORMAT_MONO_NATIVE)
                || (plan.ext_format == MIXER_EXTERNAL_FORMAT_MULTI_MONO)))
        {
            plan.source_kind = MIXER_LANE_SOURCE_HW_PLUS_EXT_MONO;
        }
        else if (plan.ext_enabled != 0U)
        {
            plan.source_kind = MIXER_LANE_SOURCE_HW_PLUS_EXT_STEREO;
        }
        else
        {
            plan.source_kind = MIXER_LANE_SOURCE_HW_STEREO;
        }

        return plan;
    }

    if ((plan.ext_enabled != 0U)
            && ((plan.ext_format == MIXER_EXTERNAL_FORMAT_STEREO)
                || (plan.ext_format == MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                || (plan.ext_format == MIXER_EXTERNAL_FORMAT_MULTI_STEREO)))
    {
        plan.source_kind = MIXER_LANE_SOURCE_EXT_STEREO;
        return plan;
    }

    if ((plan.ext_enabled != 0U)
            && ((plan.ext_format == MIXER_EXTERNAL_FORMAT_MONO_NATIVE)
                || (plan.ext_format == MIXER_EXTERNAL_FORMAT_MULTI_MONO)))
    {
        plan.source_kind = MIXER_LANE_SOURCE_EXT_MONO_NATIVE;
        plan.exec_kind = MIXER_LANE_EXEC_MONO_NATIVE;
    }

    return plan;
}

typedef struct
{
    float *mono;
    float *left;
    float *right;
} mixer_lane_buffers_t;

static mixer_lane_buffers_t mixer_lane_prepare_stereo_buffers(uint32_t track_id,
                                                              const mixer_lane_plan_t *plan,
                                                              StereoTrack *tracks)
{
    mixer_lane_buffers_t buffers = {0};

    if ((plan == NULL) || (tracks == NULL))
    {
        return buffers;
    }

    if ((plan->source_kind == MIXER_LANE_SOURCE_HW_STEREO)
            || (plan->source_kind == MIXER_LANE_SOURCE_HW_PLUS_EXT_STEREO)
            || (plan->source_kind == MIXER_LANE_SOURCE_HW_PLUS_EXT_MONO))
    {
        buffers.left = tracks[track_id].L;
        buffers.right = tracks[track_id].R;
        return buffers;
    }

    if (plan->source_kind == MIXER_LANE_SOURCE_EXT_STEREO)
    {
        buffers.left = g_external_track_l[track_id];
        buffers.right = g_external_track_r[track_id];
    }

    return buffers;
}

static ITCM_TEXT void mixer_lane_accumulate_external_source(uint32_t track_id,
                                                  const mixer_lane_plan_t *plan,
                                                  float *left,
                                                  float *right)
{
    if ((plan == NULL) || (left == NULL) || (right == NULL))
    {
        return;
    }

    if (plan->source_kind == MIXER_LANE_SOURCE_HW_PLUS_EXT_MONO)
    {
        for (uint32_t i = 0U; i < plan->ext_frames; ++i)
        {
            const float s = g_external_track_mono[track_id][i];
            left[i] += s;
            right[i] += s;
        }
        return;
    }

    if (plan->source_kind == MIXER_LANE_SOURCE_HW_PLUS_EXT_STEREO)
    {
        for (uint32_t i = 0U; i < plan->ext_frames; ++i)
        {
            left[i] += g_external_track_l[track_id][i];
            right[i] += g_external_track_r[track_id][i];
        }
    }
}

static ITCM_TEXT mixer_lane_buffers_t mixer_lane_run_mono_native_path(uint32_t track_id,
                                                            const mixer_audio_track_runtime_t *track,
                                                            mixer_track_filter_t *filter,
                                                            uint32_t frames)
{
    mixer_lane_buffers_t buffers = {0};

    if ((track == NULL) || (filter == NULL))
    {
        return buffers;
    }

    (void)mixer_track_filter_process_block_mono(filter, g_external_track_mono[track_id], frames, NULL);

    buffers.mono = g_external_track_mono[track_id];
    return buffers;
}

static ITCM_TEXT void mixer_lane_run_stereo_path(uint32_t track_id,
                                       const mixer_audio_track_runtime_t *track,
                                       mixer_track_filter_t *filter,
                                       float *left,
                                       float *right,
                                       uint32_t frames)
{
    if ((track == NULL) || (filter == NULL) || (left == NULL) || (right == NULL))
    {
        return;
    }

    mixer_track_filter_process_block(filter, left, right, frames);
}

static uint8_t ITCM_TEXT mixer_track_filter_process_block_mono(mixer_track_filter_t *filter,
                                                     float *mono,
                                                     uint32_t frames,
                                                     const mixer_poly_cutoff_override_t *poly_cutoff)
{
    if((filter == NULL) || (mono == NULL))
    {
        return 0U;
    }

    if(filter->filter_mode != 2U)
        mixer_track_filter_set_dsp_format(filter, MIXER_FILTER_DSP_MONO);

    if(filter->filter_mode == 0U)
    {
        filter->filter_env_prepared_consumed = 1U;
        return 1U;
    }

    const float cutoff_start_hz = filter->cutoff_hz;
    const float cutoff_mod_start_hz = filter->cutoff_mod_hz;
    const float resonance_start = filter->resonance;
    const float keytrack_ratio_start = filter->keytrack_ratio;
    filter->cutoff_hz = mixer_smooth_block(cutoff_start_hz,
                                           filter->cutoff_target_hz,
                                           MIXER_FILTER_BLOCK_SMOOTH);
    filter->cutoff_mod_hz = filter->cutoff_mod_target_hz;
    filter->resonance = mixer_smooth_block(resonance_start, filter->resonance_target, MIXER_FILTER_BLOCK_SMOOTH);
    filter->keytrack_ratio = filter->keytrack_ratio_target;
    mixer_track_filter_process_biquad_mono_block(filter, mono, frames,
                                                 cutoff_start_hz,
                                                 cutoff_mod_start_hz,
                                                 resonance_start,
                                                 keytrack_ratio_start,
                                                 poly_cutoff);

    return 1U;
}

static void mixer_multi_filter_apply_core_params(multi_voice_dsp_slot_t *slot)
{
    if (slot == NULL)
    {
        return;
    }

    if (slot->filter_mode == 2U)
    {
        fx_deluge_filter_configure(&slot->deluge, slot->morph,
                                   slot->cutoff_hz, slot->resonance);
        return;
    }

    const uint8_t enabled = (slot->filter_mode != 0U) ? 1U : 0U;
    if (slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
    {
        fx_biquad_filter_set_sample_rate(&slot->filter.stereo.biquad, slot->sample_rate);
        fx_biquad_filter_set_cutoff(&slot->filter.stereo.biquad, slot->cutoff_hz);
        fx_biquad_filter_set_q(&slot->filter.stereo.biquad,
                               mixer_track_filter_resonance_to_biquad_q(slot->resonance));
        fx_biquad_filter_set_morph(&slot->filter.stereo.biquad, slot->morph);
        fx_biquad_filter_set_bypass(&slot->filter.stereo.biquad,
                                    (enabled != 0U) ? 0U : 1U);
    }
    else
    {
        fx_biquad_filter_mono_set_sample_rate(&slot->filter.mono.biquad,
                                              slot->sample_rate);
        fx_biquad_filter_mono_set_cutoff(&slot->filter.mono.biquad, slot->cutoff_hz);
        fx_biquad_filter_mono_set_q(&slot->filter.mono.biquad,
                                    mixer_track_filter_resonance_to_biquad_q(slot->resonance));
        fx_biquad_filter_mono_set_morph(&slot->filter.mono.biquad, slot->morph);
        fx_biquad_filter_mono_set_bypass(&slot->filter.mono.biquad,
                                         (enabled != 0U) ? 0U : 1U);
    }
}

static void mixer_multi_filter_sync_config(uint32_t track_id,
                                           multi_voice_dsp_slot_t *slot)
{
    if ((track_id >= MIXER_MAX_TRACKS) || (slot == NULL))
    {
        return;
    }

    const mixer_track_filter_t *const source = &g_track_filters[track_id];
    const uint8_t previous_mode = slot->filter_mode;
    slot->cutoff_target_hz = source->cutoff_target_hz;
    slot->cutoff_mod_target_hz = source->cutoff_mod_target_hz;
    slot->resonance_target = source->resonance_target;
    slot->eg_amount = source->eg_amount;
    slot->keytrack = source->keytrack;
    if (slot->morph_target != source->morph_target) {
        slot->morph_target = source->morph_target;
        slot->morph_step = (slot->morph_target - slot->morph) * (1.0f / 64.0f);
        slot->morph_ramp_remaining = 64U;
    }
    slot->filter_mode = source->filter_mode;
    slot->filter_retrigger_hard = source->filter_retrigger_hard;
    if (slot->filter_env.attack != source->filter_env.attack)
        env_adsr_set_attack(&slot->filter_env, source->filter_env.attack);
    if (slot->filter_env.decay != source->filter_env.decay)
        env_adsr_set_decay(&slot->filter_env, source->filter_env.decay);
    if (slot->filter_env.sustain != source->filter_env.sustain)
        env_adsr_set_sustain(&slot->filter_env, source->filter_env.sustain);
    if (slot->filter_env.release != source->filter_env.release)
        env_adsr_set_release(&slot->filter_env, source->filter_env.release);
    if (slot->vca_env.attack != source->vca_env.attack)
        env_adsr_set_attack(&slot->vca_env, source->vca_env.attack);
    if (slot->vca_env.decay != source->vca_env.decay)
        env_adsr_set_decay(&slot->vca_env, source->vca_env.decay);
    if (slot->vca_env.sustain != source->vca_env.sustain)
        env_adsr_set_sustain(&slot->vca_env, source->vca_env.sustain);
    if (slot->vca_env.release != source->vca_env.release)
        env_adsr_set_release(&slot->vca_env, source->vca_env.release);
    slot->vca_retrigger_hard = source->vca_retrigger_hard;
    slot->filter_config_version = source->config_version;
    if(previous_mode != slot->filter_mode)
        fx_deluge_filter_init(&slot->deluge, slot->sample_rate);
    if((previous_mode != slot->filter_mode) && (slot->filter_mode == 1U))
    {
        mixer_multi_filter_apply_core_params(slot);
    }
}

static float mixer_multi_filter_compute_modulated_cutoff(
    const multi_voice_dsp_slot_t *slot,
    float base_hz,
    float modulation_hz,
    float keytrack_ratio,
    float env)
{
    float cutoff_hz = (base_hz + modulation_hz) * keytrack_ratio;
    cutoff_hz = clampf_local(cutoff_hz,
                             MIXER_FILTER_CUTOFF_MIN_HZ,
                             MIXER_FILTER_CUTOFF_MAX_HZ);
    if (slot->eg_amount == 0.0f)
    {
        return cutoff_hz;
    }
    const float env_value = clampf_local(env, 0.0f, 1.0f);
    if (slot->eg_amount >= 0.0f)
    {
        cutoff_hz += (MIXER_FILTER_CUTOFF_MAX_HZ - cutoff_hz)
                   * slot->eg_amount * env_value;
    }
    else
    {
        cutoff_hz += (cutoff_hz - MIXER_FILTER_CUTOFF_MIN_HZ)
                   * slot->eg_amount * env_value;
    }
    return clampf_local(cutoff_hz,
                        MIXER_FILTER_CUTOFF_MIN_HZ,
                        MIXER_FILTER_CUTOFF_MAX_HZ);
}

static void mixer_multi_filter_advance_morph(multi_voice_dsp_slot_t *slot, uint32_t frames)
{
    if ((slot == NULL) || (slot->morph_ramp_remaining == 0U)) return;
    if (frames >= slot->morph_ramp_remaining) {
        slot->morph = slot->morph_target;
        slot->morph_ramp_remaining = 0U;
    } else {
        slot->morph += slot->morph_step * (float)frames;
        slot->morph_ramp_remaining = (uint8_t)(slot->morph_ramp_remaining - frames);
    }
    if (slot->filter_mode == 1U) {
        if (slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
            fx_biquad_filter_set_morph(&slot->filter.stereo.biquad, slot->morph);
        else
            fx_biquad_filter_mono_set_morph(&slot->filter.mono.biquad, slot->morph);
    }
}

static void mixer_multi_filter_process_biquad(multi_voice_dsp_slot_t *slot,
                                               float *left,
                                               float *right,
                                               uint32_t frames)
{
    const float cutoff_start_hz = slot->cutoff_hz;
    const float cutoff_mod_start_hz = slot->cutoff_mod_hz;
    const float resonance_start = slot->resonance;
    const float keytrack_ratio_start = slot->keytrack_ratio;
    const uint8_t filter_env_modulated = (slot->eg_amount != 0.0f) ? 1U : 0U;
    const env_adsr_peaks_stage_t env_stage = env_adsr_stage(&slot->filter_env);
    const uint8_t env_static = (uint8_t)((filter_env_modulated == 0U)
        || (env_stage == ENV_ADSR_PEAKS_STAGE_IDLE)
        || (env_stage == ENV_ADSR_PEAKS_STAGE_SUSTAIN));
    const uint8_t transition_active = 0U;
    if ((slot->morph_ramp_remaining == 0U) && (env_static != 0U) && (transition_active == 0U))
    {
        int16_t first_value = 0;
        const int16_t terminal_value =
            env_adsr_process_advance(&slot->filter_env, frames, &first_value);
        const float env = (filter_env_modulated != 0U)
            ? ((float)first_value * (1.0f / 32767.0f)) : 0.0f;
        const float cutoff = mixer_multi_filter_compute_modulated_cutoff(
            slot,
            slot->cutoff_hz,
            slot->cutoff_mod_hz - slot->cutoff_target_hz,
            slot->keytrack_ratio,
            env);
        (void)terminal_value;
        if(slot->filter_mode == 2U)
        {
            fx_deluge_filter_configure(&slot->deluge,
                slot->morph, cutoff, slot->resonance);
            if(slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
                fx_deluge_filter_process(&slot->deluge, left, right, frames);
            else
                fx_deluge_filter_process_mono(&slot->deluge, left, frames);
            return;
        }
        const float q = mixer_track_filter_resonance_to_biquad_q(slot->resonance);
        if (slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
        {
            fx_biquad_filter_set_params(&slot->filter.stereo.biquad, cutoff, q);
            fx_biquad_filter_process_block(&slot->filter.stereo.biquad,
                                           left,
                                           right,
                                           frames);
        }
        else
        {
            fx_biquad_filter_mono_set_params(&slot->filter.mono.biquad, cutoff, q);
            fx_biquad_filter_mono_process_block(&slot->filter.mono.biquad,
                                                left,
                                                frames);
        }
        return;
    }
    uint32_t offset = 0U;
    while (offset < frames)
    {
        uint32_t chunk = frames - offset;
        if (chunk > MIXER_FILTER_UPDATE_PERIOD)
        {
            chunk = MIXER_FILTER_UPDATE_PERIOD;
        }

        int16_t first_value = 0;
        const int16_t terminal_value =
            env_adsr_process_advance(&slot->filter_env, chunk, &first_value);
        float env = 0.0f;
        if (filter_env_modulated != 0U)
        {
            env = 0.5f * ((float)first_value + (float)terminal_value)
                * (1.0f / 32767.0f);
        }
        const float progress = (float)(offset + chunk) / (float)frames;
        const float base_hz = cutoff_start_hz
            + ((slot->cutoff_hz - cutoff_start_hz) * progress);
        const float mod_absolute_hz = cutoff_mod_start_hz
            + ((slot->cutoff_mod_hz - cutoff_mod_start_hz) * progress);
        const float modulation_hz = mod_absolute_hz - slot->cutoff_target_hz;
        const float keytrack_ratio = keytrack_ratio_start
            + ((slot->keytrack_ratio - keytrack_ratio_start) * progress);
        const float resonance = resonance_start
            + ((slot->resonance - resonance_start) * progress);
        const float cutoff = mixer_multi_filter_compute_modulated_cutoff(
            slot,
            base_hz,
            modulation_hz,
            keytrack_ratio,
            env);
        mixer_multi_filter_advance_morph(slot, chunk);
        if(slot->filter_mode == 2U)
        {
            fx_deluge_filter_configure(&slot->deluge,
                slot->morph, cutoff, resonance);
            if(slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
                fx_deluge_filter_process(&slot->deluge, &left[offset], &right[offset], chunk);
            else
                fx_deluge_filter_process_mono(&slot->deluge, &left[offset], chunk);
            offset += chunk;
            continue;
        }
        const float q = mixer_track_filter_resonance_to_biquad_q(resonance);
        if (slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
        {
            fx_biquad_filter_set_params(&slot->filter.stereo.biquad, cutoff, q);
            fx_biquad_filter_process_block(&slot->filter.stereo.biquad,
                                           &left[offset],
                                           &right[offset],
                                           chunk);
        }
        else
        {
            fx_biquad_filter_mono_set_params(&slot->filter.mono.biquad, cutoff, q);
            fx_biquad_filter_mono_process_block(&slot->filter.mono.biquad,
                                                &left[offset],
                                                chunk);
        }
        offset += chunk;
    }
}

void mixer_multi_filter_note_on(uint32_t track_id,
                                struct multi_voice_dsp_slot_t *slot,
                                uint8_t midi_note)
{
    if (slot == NULL)
    {
        return;
    }

    mixer_multi_filter_sync_config(track_id, slot);
    slot->current_note = midi_note;
    const float semitones = ((float)((int32_t)midi_note
                                     - (int32_t)MIXER_FILTER_NOTE_REF_MIDI))
                          * clampf_local(slot->keytrack, 0.0f, 1.0f);
    slot->keytrack_ratio_target = exp2f(semitones * (1.0f / 12.0f));
    env_adsr_retrigger(&slot->filter_env, slot->filter_retrigger_hard != 0U);
    slot->vca_enabled = 1U;
    slot->vca_gate = 1U;
    env_adsr_retrigger(&slot->vca_env, slot->vca_retrigger_hard != 0U);
}

void mixer_multi_filter_note_off(struct multi_voice_dsp_slot_t *slot)
{
    if (slot != NULL)
    {
        env_adsr_gate_off(&slot->filter_env);
        slot->vca_gate = 0U;
        env_adsr_gate_off(&slot->vca_env);
    }
}

uint8_t mixer_multi_voice_vca_requires_source(
    const struct multi_voice_dsp_slot_t *slot)
{
    if ((slot == NULL) || (slot->vca_enabled == 0U))
    {
        return 0U;
    }
    return (env_adsr_stage(&slot->vca_env) != ENV_ADSR_PEAKS_STAGE_IDLE) ? 1U : 0U;
}

static void mixer_multi_filter_prepare_block(uint32_t track_id,
                                             multi_voice_dsp_slot_t *slot)
{
    mixer_multi_filter_sync_config(track_id, slot);
    slot->cutoff_hz = mixer_smooth_block(slot->cutoff_hz,
                                         slot->cutoff_target_hz,
                                         MIXER_FILTER_BLOCK_SMOOTH);
    slot->cutoff_mod_hz = mixer_smooth_block(slot->cutoff_mod_hz,
                                             slot->cutoff_mod_target_hz,
                                             MIXER_FILTER_BLOCK_SMOOTH);
    slot->resonance = mixer_smooth_block(slot->resonance,
                                         slot->resonance_target,
                                         MIXER_FILTER_BLOCK_SMOOTH);
    slot->keytrack_ratio = mixer_smooth_block(slot->keytrack_ratio,
                                              slot->keytrack_ratio_target,
                                              MIXER_FILTER_BLOCK_SMOOTH);
}

void mixer_multi_filter_prepare_voice_block(uint32_t track_id,
                                            struct multi_voice_dsp_slot_t *slot)
{
    mixer_multi_filter_prepare_block(track_id, slot);
}

void mixer_multi_filter_set_voice_cutoff(multi_voice_dsp_slot_t *slot, float cutoff_hz)
{
    if (slot != NULL)
    {
        /* The kernel consumes cutoff_mod_hz as an absolute modulation
         * value relative to the current prepared base. Encode the
         * voice-local absolute cutoff so that
         * base + (mod - target) == cutoff_hz. */
        slot->cutoff_mod_hz = cutoff_hz
            - slot->cutoff_hz
            + slot->cutoff_target_hz;
        slot->cutoff_mod_target_hz = slot->cutoff_mod_hz;
    }
}

void mixer_multi_filter_set_voice_resonance(multi_voice_dsp_slot_t *slot, float resonance)
{
    if (slot != NULL)
    {
        slot->resonance = resonance;
        slot->resonance_target = resonance;
    }
}

void mixer_multi_filter_set_voice_eg_amount(multi_voice_dsp_slot_t *slot, float amount)
{
    if (slot != NULL) slot->eg_amount = amount;
}

void mixer_multi_filter_set_voice_env_attack(multi_voice_dsp_slot_t *slot, float seconds)
{
    if (slot != NULL) env_adsr_set_attack(&slot->filter_env,
        mixer_track_filter_time_s_to_peaks(seconds, slot->sample_rate));
}

void mixer_multi_filter_set_voice_env_decay(multi_voice_dsp_slot_t *slot, float seconds)
{
    if (slot != NULL) env_adsr_set_decay(&slot->filter_env,
        mixer_track_filter_time_s_to_peaks(seconds, slot->sample_rate));
}

void mixer_multi_filter_set_voice_env_sustain(multi_voice_dsp_slot_t *slot, float sustain)
{
    if (slot != NULL) env_adsr_set_sustain(&slot->filter_env,
        mixer_track_filter_sustain_to_peaks(sustain));
}

void mixer_multi_filter_set_voice_env_release(multi_voice_dsp_slot_t *slot, float seconds)
{
    if (slot != NULL) env_adsr_set_release(&slot->filter_env,
        mixer_track_filter_time_s_to_peaks(seconds, slot->sample_rate));
}

void mixer_multi_filter_set_voice_vca_attack(multi_voice_dsp_slot_t *slot, float seconds)
{
    if (slot != NULL) env_adsr_set_attack(&slot->vca_env,
        mixer_track_filter_time_s_to_peaks(seconds, slot->sample_rate));
}

void mixer_multi_filter_set_voice_vca_decay(multi_voice_dsp_slot_t *slot, float seconds)
{
    if (slot != NULL) env_adsr_set_decay(&slot->vca_env,
        mixer_track_filter_time_s_to_peaks(seconds, slot->sample_rate));
}

void mixer_multi_filter_set_voice_vca_sustain(multi_voice_dsp_slot_t *slot, float sustain)
{
    if (slot != NULL) env_adsr_set_sustain(&slot->vca_env,
        mixer_track_filter_sustain_to_peaks(sustain));
}

void mixer_multi_filter_set_voice_vca_release(multi_voice_dsp_slot_t *slot, float seconds)
{
    if (slot != NULL) env_adsr_set_release(&slot->vca_env,
        mixer_track_filter_time_s_to_peaks(seconds, slot->sample_rate));
}

void mixer_multi_filter_process_prepared(uint32_t track_id,
                                         struct multi_voice_dsp_slot_t *slot,
                                         float *left,
                                         float *right,
                                         uint32_t frames)
{
    if ((slot == NULL) || (left == NULL) || (right == NULL) || (frames == 0U))
    {
        return;
    }
    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    (void)track_id;

    if(slot->filter_mode != 0U)
        mixer_multi_filter_process_biquad(slot, left, right, frames);

}

void mixer_multi_filter_process(uint32_t track_id,
                                struct multi_voice_dsp_slot_t *slot,
                                float *left,
                                float *right,
                                uint32_t frames)
{
    mixer_multi_filter_prepare_block(track_id, slot);
    mixer_multi_filter_process_prepared(track_id, slot, left, right, frames);
}

void mixer_multi_filter_process_mono_prepared(uint32_t track_id,
                                              struct multi_voice_dsp_slot_t *slot,
                                              float *mono,
                                              uint32_t frames)
{
    if ((slot == NULL) || (mono == NULL) || (frames == 0U)
        || (slot->format != (uint8_t)MULTI_VOICE_DSP_FORMAT_MONO))
    {
        return;
    }
    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    (void)track_id;
    if(slot->filter_mode != 0U)
        mixer_multi_filter_process_biquad(slot, mono, NULL, frames);

}

void mixer_multi_filter_process_mono(uint32_t track_id,
                                     struct multi_voice_dsp_slot_t *slot,
                                     float *mono,
                                     uint32_t frames)
{
    mixer_multi_filter_prepare_block(track_id, slot);
    mixer_multi_filter_process_mono_prepared(track_id, slot, mono, frames);
}

