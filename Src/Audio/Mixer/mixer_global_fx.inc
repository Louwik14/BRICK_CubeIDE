/**
 * @brief Point d'entrée clamp01.
 *
 * Role:
 * - Exécuter le traitement associé à clamp01.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour definie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float clamp01(float v)
{
    if(v < 0.0f) return 0.0f;
    if(v > 1.0f) return 1.0f;
    return v;
}

/**
 * @brief Point d'entrée clamp_pan.
 *
 * Role:
 * - Exécuter le traitement associé à clamp_pan.
 *
 * @param pan Parametre d'entree de l'API.
 *
 * @return Valeur de retour definie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float clamp_pan(float pan)
{
    if(pan < -1.0f) return -1.0f;
    if(pan > 1.0f) return 1.0f;
    return pan;
}

static inline void mixer_advance_track_ramps(float *gain_cur,
                                             float gain_step,
                                             float *pan_cur,
                                             float pan_step,
                                             float *mute_gain_cur,
                                             float mute_target,
                                             float mute_step)
{
    *gain_cur += gain_step;
    *pan_cur += pan_step;
    if (*mute_gain_cur < mute_target)
    {
        *mute_gain_cur += mute_step;
        if (*mute_gain_cur > mute_target) *mute_gain_cur = mute_target;
    }
    else if (*mute_gain_cur > mute_target)
    {
        *mute_gain_cur -= mute_step;
        if (*mute_gain_cur < mute_target) *mute_gain_cur = mute_target;
    }
}

typedef struct
{
    float pan_l;
    float pan_r;
    float gain_l;
    float gain_r;
    float mono_gain;
    uint8_t stable;
} mixer_track_coefficient_plan_t;

static ITCM_TEXT mixer_track_coefficient_plan_t mixer_prepare_track_coefficients(
    float gain_current,
    float gain_target,
    float pan_current,
    float pan_target,
    float mute_current,
    float mute_target)
{
    mixer_track_coefficient_plan_t plan = {0};
    plan.stable = (uint8_t)((gain_current == gain_target)
        && (pan_current == pan_target)
        && (mute_current == mute_target));
    if (plan.stable == 0U)
    {
        return plan;
    }

    const float pan_for_mix = -pan_current;
    plan.pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
    plan.pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
    plan.gain_l = gain_current * plan.pan_l * mute_current;
    plan.gain_r = gain_current * plan.pan_r * mute_current;
    plan.mono_gain = gain_current * mute_current;
    return plan;
}

static inline void mixer_track_coefficients_at(
    const mixer_track_coefficient_plan_t *plan,
    float gain,
    float pan,
    float mute_gain,
    float *gain_l,
    float *gain_r)
{
    if (plan->stable != 0U)
    {
        *gain_l = plan->gain_l;
        *gain_r = plan->gain_r;
        return;
    }
    const float pan_for_mix = -pan;
    const float pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
    const float pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
    *gain_l = gain * pan_l * mute_gain;
    *gain_r = gain * pan_r * mute_gain;
}

/**
 * @brief Initialise l'état interne du mixer.
 *
 * Contexte d'appel:
 * - Init application, hors IRQ.
 */
/**
 * @brief Point d'entrée mixer_init.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void mixer_reverb_state_reset_defaults(void)
{
    g_reverb.wet = 0.0f;
    g_reverb.room_size = 0.60f;
    g_reverb.damping = 0.72f;
    g_reverb.width = 1.0f;
    g_reverb.hpf = 0.0f;
    g_reverb.lpf = 1.0f;
    g_delay_spectral_position = 0.50f;
    g_delay_spectral_width = 1.0f;
}

static void mixer_apply_delay_spectral_window(void)
{
    spectral_window_result_t result;
    spectral_window_calculate(g_delay_spectral_position,
                              g_delay_spectral_width,
                              spectral_window_delay_limits(),
                              &result);
    fx_delay_stereo_global_set_filter_hz(result.low_cut_hz, result.high_cut_hz);
    fx_delay_dual_global_set_filter_hz(result.low_cut_hz, result.high_cut_hz);
}

void mixer_reset_runtime_state(void)
{
    mixer_reverb_state_reset_defaults();
    fx_reverb_global_init(MIXER_FILTER_SAMPLE_RATE_DEFAULT);
    fx_reverb_global_set_wet(g_reverb.wet);
    fx_reverb_global_set_room_size(g_reverb.room_size);
    fx_reverb_global_set_damping(g_reverb.damping);
    fx_reverb_global_set_width(g_reverb.width);
    fx_reverb_global_set_hpf(g_reverb.hpf);
    fx_reverb_global_set_lpf(g_reverb.lpf);
    fx_delay_stereo_global_init(MIXER_FILTER_SAMPLE_RATE_DEFAULT);
    fx_delay_dual_global_init(MIXER_FILTER_SAMPLE_RATE_DEFAULT);
    mixer_apply_delay_spectral_window();
    g_delay_type = (uint8_t)MIXER_DELAY_TYPE_CLASSIC;
    fx_delay_stereo_global_clear();
    g_looper_xfade_smoothed = 0.0f;
    g_looper_xfade_prev = 0.0f;

    for(uint32_t t = 0; t < MIXER_MAX_TRACKS; t++)
    {
        g_tracks[t].gain = 1.0f;
        g_tracks[t].pan = 0.0f;
        g_tracks[t].gain_current = 1.0f;
        g_tracks[t].pan_current = 0.0f;
        g_tracks[t].mute_gain_current = 1.0f;
        g_tracks[t].mute = 0U;

        g_tracks[t].route_master = 1U;

        for(uint32_t i = 0; i < MIXER_INSERTS_PER_TRACK; i++)
            g_tracks[t].insert_slot[i] = -1;

        for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
        {
            g_tracks[t].send_level[s] = 0.0f;
            g_tracks[t].send_level_current[s] = 0.0f;
        }
        g_tracks[t].group_fx_level[0] = 0.0f;
        g_tracks[t].group_fx_level[1] = 0.0f;
        g_tracks[t].group_fx_level_current[0] = 0.0f;
        g_tracks[t].group_fx_level_current[1] = 0.0f;

        mixer_track_filter_init(&g_track_filters[t], MIXER_FILTER_SAMPLE_RATE_DEFAULT);

    }

    for (uint32_t i = 0U; i < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET; ++i)
    {
        mixer_track_filter_init(&g_poly_filters_hot[i], MIXER_FILTER_SAMPLE_RATE_DEFAULT);
        g_poly_cutoff_override[i].effective_hz = MIXER_FILTER_CUTOFF_MAX_HZ;
        g_poly_cutoff_override[i].valid = 0U;
    }

    for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
        g_send_fx_slot[s] = -1;

    mixer_external_inputs_clear();
}

void mixer_init(void)
{
    audio_rec_bus_projection_audio_init();
    audio_rec_level_snapshot_audio_init();
    mixer_track_filter_init_time_lut();
    fx_modfx_global_init();
    mixer_reset_runtime_state();
}

/**
 * @brief Définit le gain master global.
 *
 * @param gain Gain linéaire master.
 */
/**
 * @brief Point d'entrée mixer_set_master.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_set_master.
 *
 * @param gain Parametre d'entree de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_master(float gain)
{
    audio_float_set_master_gain(gain);
}

/**
 * @brief Lit le gain master courant.
 *
 * @return Gain master linéaire.
 */
/**
 * @brief Point d'entrée mixer_get_master.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_get_master.
 *
 *
 * @return Valeur de retour definie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
float mixer_get_master(void)
{
    return audio_float_get_master_gain();
}

/**
 * @brief Point d'entrée mixer_set_track_gain.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_set_track_gain.
 *
 * @param track_id Parametre d'entree de l'API.
 * @param gain Parametre d'entree de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_track_gain(uint32_t track_id, float gain)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    if(gain < 0.0f)
        gain = 0.0f;

    g_tracks[track_id].gain = gain;
}

void mixer_set_track_group_fx_level(uint32_t track_id,
                                    uint32_t slot,
                                    float level)
{
    if ((track_id >= MIXER_MAX_TRACKS) || (slot >= 2U))
        return;
    g_tracks[track_id].group_fx_level[slot] =
        (level <= 0.0f) ? 0.0f : (level >= 1.0f) ? 1.0f : level;
}

/**
 * @brief Point d'entrée mixer_get_track_gain.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_get_track_gain.
 *
 * @param track_id Parametre d'entree de l'API.
 *
 * @return Valeur de retour definie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
float mixer_get_track_gain(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return 0.0f;

    return g_tracks[track_id].gain;
}

/**
 * @brief Point d'entrée mixer_set_track_pan.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_set_track_pan.
 *
 * @param track_id Parametre d'entree de l'API.
 * @param pan Parametre d'entree de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_track_pan(uint32_t track_id, float pan)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_tracks[track_id].pan = clamp_pan(pan);
}

/**
 * @brief Point d'entrée mixer_set_track_mute.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_set_track_mute.
 *
 * @param track_id Parametre d'entree de l'API.
 * @param mute Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_track_mute(uint32_t track_id, uint8_t mute)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_tracks[track_id].mute = mute ? 1U : 0U;
}

uint8_t mixer_get_track_mute(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return 0U;

    return g_tracks[track_id].mute;
}

/**
 * @brief Frontière de publication du routing track.
 *
 * Role:
 * - Le routing CONTROL est publié puis appliqué côté AUDIO au début du bloc.
 *
 * @param track_id Parametre d'entree de l'API.
 * @param route Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */

/**
 * @brief Frontière de publication des inserts track.
 *
 * Role:
 * - Les inserts CONTROL sont publiés puis appliqués côté AUDIO au début du bloc.
 *
 * @param track_id Parametre d'entree de l'API.
 * @param insert_idx Paramètre d'entrée de l'API.
 * @param slot Parametre d'entree de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */

/**
 * @brief Point d'entrée mixer_set_track_send_level.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_set_track_send_level.
 *
 * @param track_id Parametre d'entree de l'API.
 * @param send_idx Parametre d'entree de l'API.
 * @param level Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_track_send_level(uint32_t track_id, uint32_t send_idx, float level)
{
    if(track_id >= MIXER_MAX_TRACKS || send_idx >= MIXER_NUM_SENDS)
        return;

    g_tracks[track_id].send_level[send_idx] = clamp01(level);
}

/**
 * @brief Point d'entrée mixer_set_send_fx_slot.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_set_send_fx_slot.
 *
 * @param send_idx Parametre d'entree de l'API.
 * @param slot Parametre d'entree de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_send_fx_slot(uint32_t send_idx, int8_t slot)
{
    if(send_idx >= MIXER_NUM_SENDS)
        return;

    if(send_idx == MIXER_DELAY_SEND_INDEX)
    {
        /* Send2 is owned by the dedicated global delay path; pooled slot state is kept inactive. */
        g_send_fx_slot[send_idx] = -1;
        return;
    }

    g_send_fx_slot[send_idx] = slot;
}

void mixer_set_reverb_wet(float wet)
{
    g_reverb.wet = clamp01(wet);
    fx_reverb_global_set_wet(g_reverb.wet);
}

void mixer_set_reverb_room_size(float room_size)
{
    g_reverb.room_size = clamp01(room_size);
    fx_reverb_global_set_room_size(g_reverb.room_size);
}

void mixer_set_reverb_damping(float damping)
{
    g_reverb.damping = clamp01(damping);
    fx_reverb_global_set_damping(g_reverb.damping);
}

void mixer_set_reverb_width(float width)
{
    g_reverb.width = clamp01(width);
    fx_reverb_global_set_width(g_reverb.width);
}

void mixer_set_reverb_hpf(float hpf)
{
    g_reverb.hpf = clamp01(hpf);
    fx_reverb_global_set_hpf(g_reverb.hpf);
}

void mixer_set_reverb_lpf(float lpf)
{
    g_reverb.lpf = clamp01(lpf);
    fx_reverb_global_set_lpf(g_reverb.lpf);
}

void mixer_set_reverb_delays(uint8_t tbd)
{
    fx_reverb_global_set_delay_mode(tbd);
}

void mixer_set_delay_type(uint8_t type)
{
    const uint8_t next = (type != 0U) ? (uint8_t)MIXER_DELAY_TYPE_DUAL : (uint8_t)MIXER_DELAY_TYPE_CLASSIC;
    if(next == g_delay_type)
        return;

    g_delay_type = next;
    if(g_delay_type == (uint8_t)MIXER_DELAY_TYPE_DUAL)
    {
        fx_delay_dual_global_clear();
    }
    else
    {
        fx_delay_stereo_global_clear();
    }
}

void mixer_set_delay_mode(uint8_t mode)
{
    fx_delay_dual_global_set_mode(mode);
}

void mixer_set_delay_time(float time_s)
{
    fx_delay_stereo_global_set_time(time_s);
    fx_delay_dual_global_set_time_l(time_s);
}

void mixer_set_delay_time_r(float time_s)
{
    fx_delay_dual_global_set_time_r(time_s);
}

void mixer_set_delay_feedback(float feedback)
{
    fx_delay_stereo_global_set_feedback(feedback);
    fx_delay_dual_global_set_feedback(feedback);
}

void mixer_set_delay_spectral_position(float position)
{
    g_delay_spectral_position = clamp01(position);
    mixer_apply_delay_spectral_window();
}

void mixer_set_delay_spectral_width(float width)
{
    g_delay_spectral_width = clamp01(width);
    mixer_apply_delay_spectral_window();
}

void mixer_set_delay_pingpong(uint8_t enabled)
{
    fx_delay_stereo_global_set_pingpong(enabled);
}

void mixer_set_delay_width(float width)
{
    fx_delay_stereo_global_set_width(width);
    fx_delay_dual_global_set_width(width);
}

void mixer_set_delay_feedback_width(float width)
{
    fx_delay_dual_global_set_feedback_width(width);
}

void mixer_set_delay_mod_depth(float depth)
{
    fx_delay_dual_global_set_mod_depth(depth);
}

void mixer_set_delay_mod_rate(float rate_hz)
{
    fx_delay_dual_global_set_mod_rate(rate_hz);
}

void mixer_set_delay_reverb_send(float reverb_send)
{
    g_delay_diag_reverb_send = clamp01(reverb_send);
    fx_delay_stereo_global_set_reverb_send(reverb_send);
    fx_delay_dual_global_set_reverb_send(reverb_send);
}

void mixer_set_delay_volume(float volume)
{
    g_delay_diag_volume = clamp01(volume);
    fx_delay_stereo_global_set_volume(volume);
    fx_delay_dual_global_set_volume(volume);
}

void mixer_get_global_diag_state(mixer_global_diag_state_t *out)
{
    if(out == NULL)
        return;
    out->reverb_active = fx_reverb_global_is_active();
    out->delay_active = (g_delay_type == (uint8_t)MIXER_DELAY_TYPE_DUAL)
        ? fx_delay_dual_global_is_active() : fx_delay_stereo_global_is_active();
    out->delay_type = g_delay_type;
    out->_pad = 0U;
    out->reverb_wet = g_reverb.wet;
    out->delay_volume = g_delay_diag_volume;
    out->delay_reverb_send = g_delay_diag_reverb_send;
}

