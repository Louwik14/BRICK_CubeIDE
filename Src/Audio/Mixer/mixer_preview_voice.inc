#include "Audio/preview_voice_dsp.h"

static void preview_voice_dsp_apply_filter_state(
    const patch_preview_filter_t *state)
{
    mixer_track_filter_t *const filter = &g_patch_preview_filter;
    filter->morph = state->morph;
    filter->morph_target = state->morph;
    filter->morph_step = 0.0f;
    filter->morph_ramp_remaining = 0U;
    filter->cutoff_hz = param_filter_audio_cutoff_hz(state->cutoff);
    filter->cutoff_target_hz = filter->cutoff_hz;
    filter->cutoff_mod_hz = filter->cutoff_hz;
    filter->cutoff_mod_target_hz = filter->cutoff_hz;
    filter->resonance = param_filter_audio_resonance(state->resonance);
    filter->resonance_target = filter->resonance;
    filter->eg_amount = param_filter_audio_eg_amount(state->eg_amount);
    filter->keytrack = param_filter_audio_keytrack(state->keytrack);
    filter->filter_retrigger_hard = (state->retrigger >= 0.5f) ? 1U : 0U;
    env_adsr_set_attack(&filter->filter_env,
        mixer_track_filter_time_s_to_env_units(
            param_filter_audio_attack_s(state->attack), filter->sample_rate));
    env_adsr_set_decay(&filter->filter_env,
        mixer_track_filter_time_s_to_env_units(
            param_filter_audio_decay_s(state->decay), filter->sample_rate));
    env_adsr_set_sustain(&filter->filter_env,
        mixer_track_filter_sustain_to_env_units(
            param_filter_audio_sustain(state->sustain)));
    env_adsr_set_release(&filter->filter_env,
        mixer_track_filter_time_s_to_env_units(
            param_filter_audio_release_s(state->release), filter->sample_rate));
    mixer_track_filter_apply_core_params(filter);
}

static void preview_voice_dsp_apply_vca_state(const patch_preview_vca_t *state)
{
    mixer_track_filter_t *const filter = &g_patch_preview_filter;
    vca_env_set_attack(&filter->synth_vca_env,
                       param_filter_audio_attack_s(state->attack));
    vca_env_set_decay(&filter->synth_vca_env,
                      param_filter_audio_decay_s(state->decay));
    vca_env_set_sustain(&filter->synth_vca_env,
                        param_filter_audio_sustain(state->sustain));
    vca_env_set_release(&filter->synth_vca_env,
                        param_filter_audio_release_s(state->release));
    filter->vca_retrigger_hard = (state->retrigger >= 0.5f) ? 1U : 0U;
    filter->filter_mode = (uint8_t)state->filter_mode;
    if (filter->filter_mode == 2U)
        fx_deluge_filter_init(&filter->deluge, filter->sample_rate);
    else mixer_track_filter_reset_dsp(filter);
}

void preview_voice_dsp_reset(void)
{
    mixer_track_filter_init(&g_patch_preview_filter,
                            MIXER_FILTER_SAMPLE_RATE_DEFAULT);
}

uint8_t preview_voice_dsp_prepare(
    const patch_preview_filter_t *filter,
    const patch_preview_vca_t *vca)
{
    if ((filter == NULL) || (vca == NULL)) return 0U;
    preview_voice_dsp_reset();
    preview_voice_dsp_apply_filter_state(filter);
    preview_voice_dsp_apply_vca_state(vca);
    return 1U;
}

void preview_voice_dsp_note_on(uint8_t note, uint8_t velocity)
{
    (void)velocity;
    mixer_track_filter_t *const filter = &g_patch_preview_filter;
    filter->current_note = note;
    filter->keytrack_ratio_target = mixer_track_filter_keytrack_ratio(filter);
    filter->note_active = 1U;
    env_adsr_retrigger(&filter->filter_env,
                       filter->filter_retrigger_hard != 0U);
    filter->vca_current_note = note;
    filter->vca_note_count = 1U;
    filter->vca_note_active = 1U;
    filter->vca_gate = 1U;
    vca_env_retrigger(&filter->synth_vca_env,
                      filter->vca_retrigger_hard != 0U);
}

void preview_voice_dsp_note_off(uint8_t note)
{
    mixer_track_filter_t *const filter = &g_patch_preview_filter;
    if (filter->current_note != note) return;
    filter->note_active = 0U;
    env_adsr_gate_off(&filter->filter_env);
    filter->vca_note_active = 0U;
    filter->vca_note_count = 0U;
    filter->vca_gate = 0U;
    vca_env_gate_off(&filter->synth_vca_env);
}

void preview_voice_dsp_prepare_mod_sources(uint32_t frames,
                                            float *filter_envelope,
                                            float *vca_envelope)
{
    mixer_track_filter_t *const filter = &g_patch_preview_filter;
    if (filter_envelope != NULL)
    {
        const int16_t segment_first = env_adsr_value(&filter->filter_env);
        uint32_t offset = 0U;
        uint8_t count = 0U;
        int16_t terminal = segment_first;
        while ((offset < frames)
                && (count < (AUDIO_BLOCK_SIZE / MIXER_FILTER_UPDATE_PERIOD)))
        {
            uint32_t chunk = frames - offset;
            if (chunk > MIXER_FILTER_UPDATE_PERIOD)
                chunk = MIXER_FILTER_UPDATE_PERIOD;
            int16_t first;
            terminal = env_adsr_process_advance(&filter->filter_env,
                                                chunk, &first);
            filter->filter_env_prepared_first[count] = first;
            filter->filter_env_prepared_terminal[count] = terminal;
            ++count;
            offset += chunk;
        }
        filter->filter_env_value = (float)terminal * (1.0f / 32767.0f);
        filter->filter_env_prepared_frames = (uint16_t)frames;
        filter->filter_env_prepared_count = count;
        filter->filter_env_prepared_consumed = 0U;
        *filter_envelope = clampf_local(
            (float)segment_first * (1.0f / 32767.0f), 0.0f, 1.0f);
    }
    if (vca_envelope != NULL)
        *vca_envelope = clampf_local(filter->vca_env_value, 0.0f, 1.0f);
}

uint8_t preview_voice_dsp_apply_param(param_id_t parameter, float value)
{
    mixer_track_filter_t *const filter = &g_patch_preview_filter;
    switch (parameter)
    {
        case PARAM_FILTER_MORPH:
            filter->morph_target = clampf_local(value, 0.0f, 127.0f);
            filter->morph_step = (filter->morph_target - filter->morph)
                * (1.0f / 64.0f);
            filter->morph_ramp_remaining = 64U;
            return 1U;
        case PARAM_FILTER_CUTOFF:
            filter->cutoff_mod_target_hz = param_filter_audio_cutoff_hz(value);
            return 1U;
        case PARAM_FILTER_RESONANCE:
            filter->resonance_target = param_filter_audio_resonance(value);
            return 1U;
        case PARAM_FILTER_EG_AMT:
            filter->eg_amount = param_filter_audio_eg_amount(value);
            return 1U;
        case PARAM_FILTER_ATTACK:
            env_adsr_set_attack(&filter->filter_env,
                mixer_track_filter_time_s_to_env_units(
                    param_filter_audio_attack_s(value), filter->sample_rate));
            return 1U;
        case PARAM_FILTER_DECAY:
            env_adsr_set_decay(&filter->filter_env,
                mixer_track_filter_time_s_to_env_units(
                    param_filter_audio_decay_s(value), filter->sample_rate));
            return 1U;
        case PARAM_FILTER_SUSTAIN:
            env_adsr_set_sustain(&filter->filter_env,
                mixer_track_filter_sustain_to_env_units(
                    param_filter_audio_sustain(value)));
            return 1U;
        case PARAM_FILTER_RELEASE:
            env_adsr_set_release(&filter->filter_env,
                mixer_track_filter_time_s_to_env_units(
                    param_filter_audio_release_s(value), filter->sample_rate));
            return 1U;
        case PARAM_FILTER_KEYTRK:
            filter->keytrack = param_filter_audio_keytrack(value);
            filter->keytrack_ratio_target =
                mixer_track_filter_keytrack_ratio(filter);
            return 1U;
        case PARAM_VCA_ATTACK:
            vca_env_set_attack(&filter->synth_vca_env,
                               param_filter_audio_attack_s(value)); return 1U;
        case PARAM_VCA_DECAY:
            vca_env_set_decay(&filter->synth_vca_env,
                              param_filter_audio_decay_s(value)); return 1U;
        case PARAM_VCA_SUSTAIN:
            vca_env_set_sustain(&filter->synth_vca_env,
                                param_filter_audio_sustain(value)); return 1U;
        case PARAM_VCA_RELEASE:
            vca_env_set_release(&filter->synth_vca_env,
                                param_filter_audio_release_s(value)); return 1U;
        default: return 0U;
    }
}

ITCM_TEXT uint8_t preview_voice_dsp_process(float *mono, uint32_t frames)
{
    mixer_track_filter_t *const filter = &g_patch_preview_filter;
    if ((mono == NULL) || (frames == 0U) || (frames > AUDIO_BLOCK_SIZE))
        return 0U;
    (void)mixer_track_filter_process_block_mono(filter, mono, frames, NULL);
    float gain[AUDIO_BLOCK_SIZE];
    const uint32_t produced = vca_env_process_block(&filter->synth_vca_env,
                                                    gain, frames);
    uint32_t frame = 0U;
    for (; frame < produced; ++frame)
    {
        filter->vca_env_value = gain[frame];
        mono[frame] *= gain[frame];
    }
    if (frame < frames)
    {
        memset(&mono[frame], 0, (frames - frame) * sizeof(*mono));
        filter->vca_env_value = 0.0f;
    }
    return (vca_env_stage(&filter->synth_vca_env) != VCA_ENV_IDLE) ? 1U : 0U;
}

ITCM_TEXT uint8_t preview_voice_dsp_process_stereo(float *left, float *right,
                                                   uint32_t frames)
{
    mixer_track_filter_t *const filter = &g_patch_preview_filter;
    if ((left == NULL) || (right == NULL) || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)) return 0U;
    mixer_track_filter_process_block(filter, left, right, frames);
    float gain[AUDIO_BLOCK_SIZE];
    const uint32_t produced = vca_env_process_block(&filter->synth_vca_env,
                                                    gain, frames);
    uint32_t frame = 0U;
    for (; frame < produced; ++frame)
    {
        filter->vca_env_value = gain[frame];
        left[frame] *= gain[frame];
        right[frame] *= gain[frame];
    }
    if (frame < frames)
    {
        memset(&left[frame], 0, (frames - frame) * sizeof(*left));
        memset(&right[frame], 0, (frames - frame) * sizeof(*right));
        filter->vca_env_value = 0.0f;
    }
    return (vca_env_stage(&filter->synth_vca_env) != VCA_ENV_IDLE) ? 1U : 0U;
}
