static void mixer_track_filter_apply_core_params(mixer_track_filter_t *filter);

static uint32_t mixer_track_filter_next_config_version(void)
{
    g_mixer_filter_config_version++;
    if (g_mixer_filter_config_version == 0U)
    {
        g_mixer_filter_config_version = 1U;
    }
    return g_mixer_filter_config_version;
}

static void mixer_track_filter_touch_config(mixer_track_filter_t *filter)
{
    if (filter != NULL)
    {
        filter->config_version = mixer_track_filter_next_config_version();
    }
}

static void mixer_track_filter_touch_continuous(mixer_track_filter_t *filter,
                                                mixer_continuous_param_t param)
{
    if ((filter != NULL) && (param < MIXER_CONT_COUNT))
    {
        const uint32_t version = mixer_track_filter_next_config_version();
        filter->continuous_version[param] = version;
        filter->continuous_epoch = version;
    }
}

static void mixer_track_filter_advance_morph(mixer_track_filter_t *filter, uint32_t frames)
{
    if ((filter == NULL) || (filter->morph_ramp_remaining == 0U)) return;
    if (frames >= filter->morph_ramp_remaining)
    {
        filter->morph = filter->morph_target;
        filter->morph_ramp_remaining = 0U;
    }
    else
    {
        filter->morph += filter->morph_step * (float)frames;
        filter->morph_ramp_remaining = (uint8_t)(filter->morph_ramp_remaining - frames);
    }
    if (filter->filter_mode == 1U)
    {
        if (filter->dsp_format == MIXER_FILTER_DSP_MONO)
            fx_biquad_filter_mono_set_morph(&filter->biquad_mono, filter->morph);
        else
            fx_biquad_filter_set_morph(&filter->biquad, filter->morph);
    }
}

enum
{
    MIXER_EXTERNAL_FORMAT_NONE = 0U,
    MIXER_EXTERNAL_FORMAT_MONO_NATIVE = 1U,
    MIXER_EXTERNAL_FORMAT_STEREO = 2U,
    MIXER_EXTERNAL_FORMAT_POLY_STEREO = 3U,
    MIXER_EXTERNAL_FORMAT_MULTI_STEREO = 4U,
    MIXER_EXTERNAL_FORMAT_MULTI_MONO = 5U
};

static mixer_track_filter_t *mixer_poly_filter(uint32_t poly_track_id, uint8_t voice)
{
    if ((poly_track_id >= SYNTH_POLYPHONY_TRACK_CAPACITY)
            || (voice >= SYNTH_POLYPHONY_MAX_VOICES))
    {
        return NULL;
    }
    const uint8_t index = synth_polyphony_get_slot((uint8_t)poly_track_id, voice);
    return (index < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
        ? &g_poly_filters_hot[index] : NULL;
}

static ITCM_TEXT void mixer_poly_filter_sync_config(mixer_track_filter_t *dst,
                                          const mixer_track_filter_t *src)
{
    if ((dst == NULL) || (src == NULL))
    {
        return;
    }
    if ((dst->config_version == src->config_version)
            && (dst->continuous_epoch == src->continuous_epoch))
    {
        return;
    }
    const uint8_t full = (dst->config_version != src->config_version) ? 1U : 0U;
    if (full != 0U)
    {
        dst->sample_rate = src->sample_rate;
        dst->keytrack = src->keytrack;
        dst->morph_target = src->morph_target;
        if (dst->morph != dst->morph_target) {
            dst->morph_step = (dst->morph_target - dst->morph) * (1.0f / 64.0f);
            dst->morph_ramp_remaining = 64U;
        }
        dst->filter_retrigger_hard = src->filter_retrigger_hard;
        dst->vca_retrigger_hard = src->vca_retrigger_hard;
        if(dst->filter_mode != src->filter_mode)
        {
            dst->filter_mode = src->filter_mode;
            fx_deluge_filter_init(&dst->deluge, dst->sample_rate);
        }
        dst->config_version = src->config_version;
    }
    /*
     * A poly voice always needs its own amplitude envelope.  The track-level
     * VCA enable flag only selects the mono track processor; using it here
     * leaves free-running oscillators permanently audible and prevents the
     * allocator from ever observing an IDLE release.
     */
    dst->vca_enabled = 1U;
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_CUTOFF]
            != src->continuous_version[MIXER_CONT_CUTOFF]))
    {
        dst->cutoff_target_hz = src->cutoff_target_hz;
        dst->cutoff_mod_target_hz = src->cutoff_mod_target_hz;
        dst->continuous_version[MIXER_CONT_CUTOFF] = src->continuous_version[MIXER_CONT_CUTOFF];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_MORPH]
            != src->continuous_version[MIXER_CONT_MORPH]))
    {
        dst->morph_target = src->morph_target;
        dst->morph_step = (dst->morph_target - dst->morph) * (1.0f / 64.0f);
        dst->morph_ramp_remaining = (dst->morph == dst->morph_target) ? 0U : 64U;
        dst->continuous_version[MIXER_CONT_MORPH] = src->continuous_version[MIXER_CONT_MORPH];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_RESONANCE]
            != src->continuous_version[MIXER_CONT_RESONANCE]))
    {
        dst->resonance_target = src->resonance_target;
        dst->continuous_version[MIXER_CONT_RESONANCE] = src->continuous_version[MIXER_CONT_RESONANCE];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_EG_AMOUNT]
            != src->continuous_version[MIXER_CONT_EG_AMOUNT]))
    {
        dst->eg_amount = src->eg_amount;
        dst->continuous_version[MIXER_CONT_EG_AMOUNT] = src->continuous_version[MIXER_CONT_EG_AMOUNT];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_FILTER_ATTACK]
            != src->continuous_version[MIXER_CONT_FILTER_ATTACK]))
    {
        env_adsr_set_attack(&dst->filter_env, src->filter_env.attack);
        dst->continuous_version[MIXER_CONT_FILTER_ATTACK] = src->continuous_version[MIXER_CONT_FILTER_ATTACK];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_FILTER_DECAY]
            != src->continuous_version[MIXER_CONT_FILTER_DECAY]))
    {
        env_adsr_set_decay(&dst->filter_env, src->filter_env.decay);
        dst->continuous_version[MIXER_CONT_FILTER_DECAY] = src->continuous_version[MIXER_CONT_FILTER_DECAY];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_FILTER_SUSTAIN]
            != src->continuous_version[MIXER_CONT_FILTER_SUSTAIN]))
    {
        env_adsr_set_sustain(&dst->filter_env, src->filter_env.sustain);
        dst->continuous_version[MIXER_CONT_FILTER_SUSTAIN] = src->continuous_version[MIXER_CONT_FILTER_SUSTAIN];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_FILTER_RELEASE]
            != src->continuous_version[MIXER_CONT_FILTER_RELEASE]))
    {
        env_adsr_set_release(&dst->filter_env, src->filter_env.release);
        dst->continuous_version[MIXER_CONT_FILTER_RELEASE] = src->continuous_version[MIXER_CONT_FILTER_RELEASE];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_VCA_ATTACK]
            != src->continuous_version[MIXER_CONT_VCA_ATTACK]))
    {
        vca_env_set_attack(&dst->synth_vca_env, src->synth_vca_env.attack_time);
        dst->continuous_version[MIXER_CONT_VCA_ATTACK] = src->continuous_version[MIXER_CONT_VCA_ATTACK];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_VCA_DECAY]
            != src->continuous_version[MIXER_CONT_VCA_DECAY]))
    {
        vca_env_set_decay(&dst->synth_vca_env, src->synth_vca_env.decay_time);
        dst->continuous_version[MIXER_CONT_VCA_DECAY] = src->continuous_version[MIXER_CONT_VCA_DECAY];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_VCA_SUSTAIN]
            != src->continuous_version[MIXER_CONT_VCA_SUSTAIN]))
    {
        vca_env_set_sustain(&dst->synth_vca_env, src->synth_vca_env.sustain);
        dst->continuous_version[MIXER_CONT_VCA_SUSTAIN] = src->continuous_version[MIXER_CONT_VCA_SUSTAIN];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_VCA_RELEASE]
            != src->continuous_version[MIXER_CONT_VCA_RELEASE]))
    {
        vca_env_set_release(&dst->synth_vca_env, src->synth_vca_env.release_time);
        dst->continuous_version[MIXER_CONT_VCA_RELEASE] = src->continuous_version[MIXER_CONT_VCA_RELEASE];
    }
    if(full != 0U) mixer_track_filter_apply_core_params(dst);
    dst->continuous_epoch = src->continuous_epoch;
}

#define MIXER_FILTER_SAMPLE_RATE_DEFAULT 48000.0f
#define MIXER_FILTER_CUTOFF_MIN_HZ 20.0f
#define MIXER_FILTER_CUTOFF_MAX_HZ 16000.0f
#define MIXER_FILTER_ATTACK_MIN_S 0.001f
#define MIXER_FILTER_ATTACK_MAX_S 5.0f
#define MIXER_FILTER_DECAY_MIN_S 0.001f
#define MIXER_FILTER_DECAY_MAX_S 5.0f
#define MIXER_FILTER_RELEASE_MIN_S 0.001f
#define MIXER_FILTER_RELEASE_MAX_S 5.0f
#define MIXER_FILTER_NOTE_REF_MIDI 60U
#define MIXER_FILTER_UPDATE_PERIOD 8U
#define MIXER_FILTER_BLOCK_SMOOTH 0.25f
#define MIXER_REVERB_SEND_INDEX 0U
#define MIXER_DELAY_SEND_INDEX 1U
#define MIXER_LOOPER_RECORD_PCM24_MAX 8388607.0f
#define MIXER_LOOPER_RECORD_PCM24_MIN (-8388608.0f)
#define MIXER_LOOPER_XFADE_EPS 0.0001f

typedef enum
{
    MIXER_DELAY_TYPE_CLASSIC = 0U,
    MIXER_DELAY_TYPE_DUAL = 1U
} mixer_delay_type_t;

static uint8_t g_delay_type = (uint8_t)MIXER_DELAY_TYPE_CLASSIC;
static float g_delay_diag_volume;
static float g_delay_diag_reverb_send;
#define MIXER_ENV_ADSR_MAX_SEGMENT_SECONDS 30.0f
typedef struct
{
    float wet;
    float room_size;
    float damping;
    float width;
    float hpf;
    float lpf;
} mixer_reverb_state_t;

static AUDIO_HOT mixer_reverb_state_t g_reverb;

static float g_delay_spectral_position = 0.50f;
static float g_delay_spectral_width = 1.0f;

static int32_t mixer_looper_float_to_pcm24(float sample)
{
    float scaled = sample * MIXER_LOOPER_RECORD_PCM24_MAX;
    if(scaled > MIXER_LOOPER_RECORD_PCM24_MAX)
    {
        scaled = MIXER_LOOPER_RECORD_PCM24_MAX;
    }
    else if(scaled < MIXER_LOOPER_RECORD_PCM24_MIN)
    {
        scaled = MIXER_LOOPER_RECORD_PCM24_MIN;
    }

    return (int32_t)scaled;
}

static uint8_t mixer_looper_record_capture_is_active(uint8_t *out_looper_track)
{
    if((out_looper_track == NULL)
            || (brick6_looper_runtime_get_record_capture_track(out_looper_track) == 0U)
            || (*out_looper_track >= MIXER_MAX_TRACKS))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t mixer_lane_routes_to_looper(uint8_t looper_track,
                                           uint8_t mix_track)
{
    const brick_entity_id_t entity =
        audio_note_engine_adapter_entity_for_mix_lane(mix_track);
    const uint8_t source_track = (entity < BRICK_ENTITY_CAPACITY)
        ? entity : mix_track;
    if ((looper_track >= MIXER_MAX_TRACKS)
            || (source_track >= MIXER_MAX_TRACKS)
            || (source_track == looper_track)
            || ((g_mixer_static_lane_flags[mix_track]
                & MIXER_STATIC_LOOPER) != 0U))
    {
        return 0U;
    }

    return control_routing_audio_get_looper_source(looper_track, source_track);
}

static float mixer_get_looper_xfade(float target)
{
    float value = g_looper_xfade_smoothed;
    value += (target - value) * 0.25f;
    if (value < 0.0f) value = 0.0f;
    else if (value > 1.0f) value = 1.0f;
    g_looper_xfade_smoothed = value;
    return value;
}

static uint8_t mixer_looper_xfade_value_is_zero(float value)
{
    return (value <= MIXER_LOOPER_XFADE_EPS) ? 1U : 0U;
}

static uint8_t mixer_looper_xfade_value_is_full(float value)
{
    return (value >= (1.0f - MIXER_LOOPER_XFADE_EPS)) ? 1U : 0U;
}

static uint8_t mixer_looper_xfade_values_are_stable(float a, float b)
{
    const float delta = a - b;
    return ((delta <= MIXER_LOOPER_XFADE_EPS) && (delta >= -MIXER_LOOPER_XFADE_EPS)) ? 1U : 0U;
}

static float clampf_local(float v, float lo, float hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static float mixer_smooth_block(float current, float target, float alpha)
{
    return current + ((target - current) * alpha);
}

enum
{
    MIXER_FILTER_TIME_LUT_SIZE = 512U
};

#define MIXER_FILTER_LOG2_MIN_TIME (-9.965784284662087f)
#define MIXER_FILTER_INV_LOG2_TIME_RANGE 0.08138130233100371f
#define MIXER_FILTER_INV_LOG2 1.4426950408889634f

static uint16_t g_mixer_filter_time_lut[MIXER_FILTER_TIME_LUT_SIZE + 1U];
static float g_mixer_filter_log2_lut[257U];
static uint8_t g_mixer_filter_time_lut_ready = 0U;

static void mixer_track_filter_init_time_lut(void)
{
    if(g_mixer_filter_time_lut_ready != 0U)
        return;

    const float time_log_ratio = logf(MIXER_FILTER_ATTACK_MAX_S / MIXER_FILTER_ATTACK_MIN_S);
    const float max_segment_samples = MIXER_FILTER_SAMPLE_RATE_DEFAULT * MIXER_ENV_ADSR_MAX_SEGMENT_SECONDS;

    for(uint32_t i = 0U; i <= 256U; ++i)
    {
        const float x = 1.0f + ((float)i * (1.0f / 256.0f));
        g_mixer_filter_log2_lut[i] = logf(x) * MIXER_FILTER_INV_LOG2;
    }

    for(uint32_t i = 0U; i <= MIXER_FILTER_TIME_LUT_SIZE; ++i)
    {
        const float norm = (float)i * (1.0f / (float)MIXER_FILTER_TIME_LUT_SIZE);
        const float time_s = MIXER_FILTER_ATTACK_MIN_S * expf(time_log_ratio * norm);
        const float desired_samples = clampf_local(time_s * MIXER_FILTER_SAMPLE_RATE_DEFAULT, 1.0f, max_segment_samples);
        const float normalized = cbrtf((desired_samples - 1.0f) / max_segment_samples);
        const float scaled = clampf_local(normalized, 0.0f, 1.0f) * 65535.0f;
        g_mixer_filter_time_lut[i] = (uint16_t)(scaled + 0.5f);
    }

    g_mixer_filter_time_lut_ready = 1U;
}

static float mixer_track_filter_log2_lut(float x)
{
    union
    {
        float f;
        uint32_t u;
    } bits;

    bits.f = clampf_local(x, 1.0e-20f, 1.0e20f);
    const int32_t exponent = (int32_t)((bits.u >> 23) & 0xffU) - 127;
    const uint32_t mantissa_bits = bits.u & 0x7fffffU;
    uint32_t index = mantissa_bits >> 15;
    if(index >= 256U)
        index = 255U;

    const float frac = (float)(mantissa_bits & 0x7fffU) * (1.0f / 32768.0f);
    const float a = g_mixer_filter_log2_lut[index];
    const float b = g_mixer_filter_log2_lut[index + 1U];
    return (float)exponent + a + ((b - a) * frac);
}

static uint16_t mixer_track_filter_time_lut_lookup(float time_s)
{
    const float clamped = clampf_local(time_s, MIXER_FILTER_ATTACK_MIN_S, MIXER_FILTER_ATTACK_MAX_S);
    float pos = (mixer_track_filter_log2_lut(clamped) - MIXER_FILTER_LOG2_MIN_TIME)
                * MIXER_FILTER_INV_LOG2_TIME_RANGE
                * (float)MIXER_FILTER_TIME_LUT_SIZE;
    if(pos <= 0.0f)
        return g_mixer_filter_time_lut[0];
    if(pos >= (float)MIXER_FILTER_TIME_LUT_SIZE)
        return g_mixer_filter_time_lut[MIXER_FILTER_TIME_LUT_SIZE];

    const uint32_t index = (uint32_t)pos;
    const float frac = pos - (float)index;
    const float a = (float)g_mixer_filter_time_lut[index];
    const float b = (float)g_mixer_filter_time_lut[index + 1U];
    return (uint16_t)(a + ((b - a) * frac) + 0.5f);
}

static float mixer_track_filter_resonance_to_biquad_q(float resonance)
{
    const float r = clampf_local(resonance, 0.0f, 1.0f);
    const float distributed = r * (0.35f + (0.65f * r));
    return 0.70710678f + ((6.5f - 0.70710678f) * distributed);
}

static uint16_t mixer_track_filter_time_s_to_peaks(float time_s, float sample_rate)
{
    (void)sample_rate;
    return mixer_track_filter_time_lut_lookup(time_s);
}

static uint16_t mixer_track_filter_sustain_to_peaks(float sustain)
{
    const float clamped = clampf_local(sustain, 0.0f, 1.0f);
    return (uint16_t)(clamped * 32767.0f + 0.5f);
}

static float mixer_track_filter_keytrack_ratio(const mixer_track_filter_t *filter)
{
    if (filter == NULL)
    {
        return 1.0f;
    }
    const float semitones =
        ((float)((int32_t)filter->current_note - (int32_t)MIXER_FILTER_NOTE_REF_MIDI))
        * clampf_local(filter->keytrack, 0.0f, 1.0f);
    return exp2f(semitones * (1.0f / 12.0f));
}

static float mixer_track_filter_compute_modulated_cutoff(const mixer_track_filter_t *filter,
                                                         float base_hz,
                                                         float modulation_hz,
                                                         float keytrack_ratio,
                                                         float env)
{
    float cutoff_hz = (base_hz + modulation_hz) * keytrack_ratio;
    cutoff_hz = clampf_local(cutoff_hz,
                             MIXER_FILTER_CUTOFF_MIN_HZ,
                             MIXER_FILTER_CUTOFF_MAX_HZ);
    if (filter->eg_amount == 0.0f)
    {
        return cutoff_hz;
    }
    const float env_value = clampf_local(env, 0.0f, 1.0f);
    if(filter->eg_amount >= 0.0f)
    {
        cutoff_hz += (MIXER_FILTER_CUTOFF_MAX_HZ - cutoff_hz)
                   * filter->eg_amount * env_value;
    }
    else
    {
        cutoff_hz += (cutoff_hz - MIXER_FILTER_CUTOFF_MIN_HZ)
                   * filter->eg_amount * env_value;
    }
    return clampf_local(cutoff_hz,
                        MIXER_FILTER_CUTOFF_MIN_HZ,
                        MIXER_FILTER_CUTOFF_MAX_HZ);
}

static uint8_t mixer_track_filter_env_control_is_static(
    const mixer_track_filter_t *filter,
    uint32_t frames)
{
    if ((filter == NULL) || (filter->eg_amount == 0.0f))
    {
        return 1U;
    }

    const uint8_t prepared =
        (uint8_t)((filter->filter_env_prepared_consumed == 0U)
            && (filter->filter_env_prepared_frames == frames)
            && (filter->filter_env_prepared_count != 0U));
    if (prepared != 0U)
    {
        const int16_t reference = filter->filter_env_prepared_first[0];
        for (uint8_t i = 0U; i < filter->filter_env_prepared_count; ++i)
        {
            if ((filter->filter_env_prepared_first[i] != reference)
                    || (filter->filter_env_prepared_terminal[i] != reference))
            {
                return 0U;
            }
        }
        return 1U;
    }

    const env_adsr_peaks_stage_t stage = env_adsr_stage(&filter->filter_env);
    return (uint8_t)((stage == ENV_ADSR_PEAKS_STAGE_IDLE)
        || (stage == ENV_ADSR_PEAKS_STAGE_SUSTAIN));
}

static float mixer_track_filter_static_env_value(mixer_track_filter_t *filter,
                                                  uint32_t frames)
{
    const uint8_t prepared =
        (uint8_t)((filter->filter_env_prepared_consumed == 0U)
            && (filter->filter_env_prepared_frames == frames)
            && (filter->filter_env_prepared_count != 0U));
    int16_t value = env_adsr_value(&filter->filter_env);
    if (prepared != 0U)
    {
        value = filter->filter_env_prepared_first[0];
    }
    else
    {
        value = env_adsr_process_advance(&filter->filter_env, frames, NULL);
        filter->filter_env_value = (float)value * (1.0f / 32767.0f);
    }
    filter->filter_env_prepared_consumed = 1U;
    return (float)value * (1.0f / 32767.0f);
}

static void mixer_track_filter_convert_biquad_to_mono(mixer_track_filter_t *filter)
{
    const fx_biquad_filter_t *const stereo = &filter->biquad;
    fx_biquad_filter_mono_t mono = {0};
    mono.sample_rate = stereo->sample_rate;
    mono.cutoff_hz = stereo->cutoff_hz;
    mono.q = stereo->q;
    mono.current = stereo->current;
    mono.ic1eq = stereo->ic1eq_l;
    mono.ic2eq = stereo->ic2eq_l;
    mono.bypass_xfade_remaining = stereo->bypass_xfade_remaining;
    mono.bypass_mix = stereo->bypass_mix;
    mono.morph = stereo->morph;
    mono.morph_a = stereo->morph_a;
    mono.morph_b = stereo->morph_b;
    mono.morph_plan = stereo->morph_plan;
    mono.bypass = stereo->bypass;
    mono.reset_after_bypass = stereo->reset_after_bypass;
    filter->biquad_mono = mono;
}

static void mixer_track_filter_convert_biquad_to_stereo(mixer_track_filter_t *filter)
{
    const fx_biquad_filter_mono_t *const mono = &filter->biquad_mono;
    fx_biquad_filter_t stereo = {0};
    stereo.sample_rate = mono->sample_rate;
    stereo.cutoff_hz = mono->cutoff_hz;
    stereo.q = mono->q;
    stereo.current = mono->current;
    stereo.ic1eq_l = mono->ic1eq;
    stereo.ic2eq_l = mono->ic2eq;
    stereo.ic1eq_r = mono->ic1eq;
    stereo.ic2eq_r = mono->ic2eq;
    stereo.bypass_xfade_remaining = mono->bypass_xfade_remaining;
    stereo.bypass_mix = mono->bypass_mix;
    stereo.morph = mono->morph;
    stereo.morph_a = mono->morph_a;
    stereo.morph_b = mono->morph_b;
    stereo.morph_plan = mono->morph_plan;
    stereo.bypass = mono->bypass;
    stereo.reset_after_bypass = mono->reset_after_bypass;
    filter->biquad = stereo;
}

static void mixer_track_filter_set_dsp_format(mixer_track_filter_t *filter,
                                               mixer_filter_dsp_format_t format)
{
    if ((filter == NULL) || (filter->dsp_format == (uint8_t)format))
        return;

    if (format == MIXER_FILTER_DSP_MONO)
    {
        mixer_track_filter_convert_biquad_to_mono(filter);
    }
    else
    {
        mixer_track_filter_convert_biquad_to_stereo(filter);
    }
    filter->dsp_format = (uint8_t)format;
}

static void mixer_track_filter_apply_core_params(mixer_track_filter_t *filter)
{
    if(filter == NULL)
        return;

    if (filter->filter_mode == 2U)
    {
        fx_deluge_filter_configure(&filter->deluge, filter->morph,
                                   filter->cutoff_hz, filter->resonance);
        return;
    }

    const uint8_t enabled = (filter->filter_mode != 0U) ? 1U : 0U;
    if (filter->dsp_format == (uint8_t)MIXER_FILTER_DSP_MONO)
    {
        fx_biquad_filter_mono_set_sample_rate(&filter->biquad_mono, filter->sample_rate);
        fx_biquad_filter_mono_set_cutoff(&filter->biquad_mono, filter->cutoff_hz);
        fx_biquad_filter_mono_set_q(
            &filter->biquad_mono,
            mixer_track_filter_resonance_to_biquad_q(filter->resonance));
        fx_biquad_filter_mono_set_morph(&filter->biquad_mono, filter->morph);
        fx_biquad_filter_mono_set_bypass(&filter->biquad_mono,
                                         (enabled != 0U) ? 0U : 1U);
        return;
    }

    fx_biquad_filter_set_cutoff(&filter->biquad, filter->cutoff_hz);
    fx_biquad_filter_set_q(
        &filter->biquad,
        mixer_track_filter_resonance_to_biquad_q(filter->resonance));
    fx_biquad_filter_set_morph(&filter->biquad, filter->morph);
    fx_biquad_filter_set_bypass(&filter->biquad,
                                (enabled != 0U) ? 0U : 1U);
}

static void mixer_track_filter_reset_dsp(mixer_track_filter_t *filter)
{
    if(filter == NULL)
        return;

    filter->dsp_format = (uint8_t)MIXER_FILTER_DSP_STEREO;
    fx_biquad_filter_init(&filter->biquad, filter->sample_rate);
    fx_biquad_filter_reset(&filter->biquad);
    mixer_track_filter_apply_core_params(filter);
}

static void mixer_track_filter_init(mixer_track_filter_t *filter, float sample_rate)
{
    if(filter == NULL)
        return;

    filter->sample_rate = (sample_rate > 0.0f) ? sample_rate : MIXER_FILTER_SAMPLE_RATE_DEFAULT;
    filter->cutoff_hz = MIXER_FILTER_CUTOFF_MAX_HZ;
    filter->cutoff_target_hz = filter->cutoff_hz;
    filter->cutoff_mod_hz = filter->cutoff_hz;
    filter->cutoff_mod_target_hz = filter->cutoff_hz;
    filter->resonance = 0.0f;
    filter->resonance_target = 0.0f;
    filter->eg_amount = 0.0f;
    filter->keytrack = 0.0f;
    filter->keytrack_ratio = 1.0f;
    filter->keytrack_ratio_target = 1.0f;
    filter->current_note = MIXER_FILTER_NOTE_REF_MIDI;
    filter->morph = 0.0f;
    filter->morph_target = 0.0f;
    filter->note_active = 0U;
    filter->vca_enabled = 0U;
    filter->vca_note_active = 0U;
    filter->vca_note_count = 0U;
    filter->vca_current_note = MIXER_FILTER_NOTE_REF_MIDI;
    filter->vca_gate = 0U;
    filter->filter_retrigger_hard = 1U;
    filter->vca_retrigger_hard = 1U;
    filter->vca_env_value = 0.0f;
    filter->filter_env_value = 0.0f;
    filter->filter_env_prepared_frames = 0U;
    filter->filter_env_prepared_count = 0U;
    filter->filter_env_prepared_consumed = 1U;
    filter->config_version = mixer_track_filter_next_config_version();

    env_adsr_init(&filter->filter_env, filter->sample_rate);
    env_adsr_set_attack(&filter->filter_env, mixer_track_filter_time_s_to_peaks(0.01f, filter->sample_rate));
    env_adsr_set_decay(&filter->filter_env, mixer_track_filter_time_s_to_peaks(0.10f, filter->sample_rate));
    env_adsr_set_sustain(&filter->filter_env, mixer_track_filter_sustain_to_peaks(1.0f));
    env_adsr_set_release(&filter->filter_env, mixer_track_filter_time_s_to_peaks(0.10f, filter->sample_rate));
    env_adsr_init(&filter->vca_env, filter->sample_rate);
    env_adsr_set_attack(&filter->vca_env, mixer_track_filter_time_s_to_peaks(0.001f, filter->sample_rate));
    env_adsr_set_decay(&filter->vca_env, mixer_track_filter_time_s_to_peaks(0.001f, filter->sample_rate));
    env_adsr_set_sustain(&filter->vca_env, mixer_track_filter_sustain_to_peaks(1.0f));
    env_adsr_set_release(&filter->vca_env, mixer_track_filter_time_s_to_peaks(0.001f, filter->sample_rate));
    env_adsr_reset(&filter->vca_env);
    vca_env_init(&filter->synth_vca_env, filter->sample_rate);
    vca_env_set_attack(&filter->synth_vca_env, 0.001f);
    vca_env_set_decay(&filter->synth_vca_env, 0.001f);
    vca_env_set_sustain(&filter->synth_vca_env, 1.0f);
    vca_env_set_release(&filter->synth_vca_env, 0.001f);
    filter->filter_mode = 0U;
    fx_deluge_filter_init(&filter->deluge, filter->sample_rate);

    mixer_track_filter_reset_dsp(filter);
}

static void mixer_track_state_reset(mixer_audio_track_runtime_t *track)
{
    if (track == NULL)
    {
        return;
    }

    track->gain = 1.0f;
    track->pan = 0.0f;
    track->gain_current = 1.0f;
    track->pan_current = 0.0f;
    track->mute_gain_current = 1.0f;
    track->mute = 0U;
    track->route_master = 1U;

    for (uint32_t i = 0U; i < MIXER_INSERTS_PER_TRACK; ++i)
    {
        track->insert_slot[i] = -1;
    }

    for (uint32_t s = 0U; s < MIXER_NUM_SENDS; ++s)
    {
        track->send_level[s] = 0.0f;
        track->send_level_current[s] = 0.0f;
    }
    track->group_fx_level[0] = 0.0f;
    track->group_fx_level[1] = 0.0f;
    track->group_fx_level_current[0] = 0.0f;
    track->group_fx_level_current[1] = 0.0f;
}

static void mixer_external_input_clear_lane(uint32_t lane)
{
    if (lane >= MIXER_MAX_TRACKS)
    {
        return;
    }

    g_external_track_enabled[lane] = 0U;
    g_external_lane_mask &= ~(uint32_t)(1UL << lane);
    g_external_poly_initialized[lane] = 0U;
    g_external_track_format[lane] = MIXER_EXTERNAL_FORMAT_NONE;
    g_external_track_frames_valid[lane] = 0U;
}

void mixer_rebind_track_states(const uint8_t *previous_mix_tracks,
                               const uint8_t *next_mix_tracks,
                               uint32_t track_count)
{
    static mixer_audio_track_runtime_t previous_tracks[MIXER_MAX_TRACKS];
    static mixer_track_filter_t previous_filters[MIXER_MAX_TRACKS];

    if ((previous_mix_tracks == NULL) || (next_mix_tracks == NULL))
    {
        return;
    }

    memcpy(previous_tracks, g_tracks, sizeof(previous_tracks));
    memcpy(previous_filters, g_track_filters, sizeof(previous_filters));

    for (uint32_t lane = 0U; lane < MIXER_MAX_TRACKS; ++lane)
    {
        mixer_track_state_reset(&g_tracks[lane]);
        mixer_track_filter_init(&g_track_filters[lane], previous_filters[lane].sample_rate);
    }

    for (uint32_t track = 0U; track < track_count; ++track)
    {
        const uint8_t next_mix = next_mix_tracks[track];
        if (next_mix >= MIXER_MAX_TRACKS)
        {
            continue;
        }

        const uint8_t previous_mix = previous_mix_tracks[track];
        if (previous_mix >= MIXER_MAX_TRACKS)
        {
            continue;
        }

        g_tracks[next_mix] = previous_tracks[previous_mix];
        g_track_filters[next_mix] = previous_filters[previous_mix];
    }

    mixer_external_inputs_clear();
    mixer_rebuild_static_plan();
}

void mixer_rebind_track_state(uint8_t previous_mix_track, uint8_t next_mix_track)
{
    mixer_audio_track_runtime_t previous_track = { 0 };
    mixer_track_filter_t previous_filter = { 0 };
    const uint8_t has_previous = (previous_mix_track < MIXER_MAX_TRACKS) ? 1U : 0U;
    const uint8_t has_next = (next_mix_track < MIXER_MAX_TRACKS) ? 1U : 0U;

    if ((has_previous != 0U) && (has_next != 0U) && (previous_mix_track == next_mix_track))
    {
        return;
    }

    if (has_previous != 0U)
    {
        previous_track = g_tracks[previous_mix_track];
        previous_filter = g_track_filters[previous_mix_track];
        mixer_track_state_reset(&g_tracks[previous_mix_track]);
        mixer_track_filter_init(&g_track_filters[previous_mix_track], previous_filter.sample_rate);
        mixer_external_input_clear_lane(previous_mix_track);
    }

    if (has_next != 0U)
    {
        const float sample_rate = (has_previous != 0U)
            ? previous_filter.sample_rate
            : g_track_filters[next_mix_track].sample_rate;
        mixer_track_state_reset(&g_tracks[next_mix_track]);
        mixer_track_filter_init(&g_track_filters[next_mix_track], sample_rate);
        mixer_external_input_clear_lane(next_mix_track);
    }

    if ((has_previous != 0U) && (has_next != 0U))
    {
        g_tracks[next_mix_track] = previous_track;
        g_track_filters[next_mix_track] = previous_filter;
        mixer_track_filter_all_notes_off(next_mix_track);
        mixer_track_vca_all_notes_off(next_mix_track);
    }

    if (has_next != 0U)
    {
        g_mixer_lane_rebind_count[next_mix_track]++;
    }
    else if (has_previous != 0U)
    {
        g_mixer_lane_rebind_count[previous_mix_track]++;
    }
    mixer_rebuild_static_plan();
}

void mixer_snap_track_runtime_state(uint32_t track_id)
{
    if (track_id >= MIXER_MAX_TRACKS)
    {
        return;
    }

    mixer_audio_track_runtime_t *const track = &g_tracks[track_id];
    mixer_track_filter_t *const filter = &g_track_filters[track_id];

    track->gain_current = track->gain;
    track->pan_current = track->pan;
    for (uint32_t s = 0U; s < MIXER_NUM_SENDS; ++s)
    {
        track->send_level_current[s] = track->send_level[s];
