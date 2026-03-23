/**
 * @file param_registry.c
 * @brief Module applicatif param_registry.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à param_registry.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "param_registry.h"

#include "audio_float.h"
#include "Audio/juno_synth.h"
#include "Audio/microdexed_synth.h"
#include "Audio/monob_synth.h"
#include "fx_daisy_comp.h"
#include "fx_granular.h"
#include "fx_pool.h"
#include "mixer.h"
#include "ui_core.h"
#include <math.h>
#include <stddef.h>


/**
 * @brief Point d'entrée clamp_value.
 *
 * Rôle:
 * - Exécuter le traitement associé à clamp_value.
 *
 * @param v Paramètre d'entrée de l'API.
 * @param lo Paramètre d'entrée de l'API.
 * @param hi Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float clamp_value(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/**
 * @brief Point d'entrée control_float_to_slot.
 *
 * Rôle:
 * - Exécuter le traitement associé à control_float_to_slot.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static int8_t control_float_to_slot(float v)
{
    if (v < 0.0f)
        return -1;
    return (int8_t)v;
}

/**
 * @brief Point d'entrée control_float_to_ui127.
 *
 * Rôle:
 * - Exécuter le traitement associé à control_float_to_ui127.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static uint8_t control_float_to_ui127(float v)
{
    if (v <= 0.0f)
        return 0U;
    if (v >= 1.0f)
        return 127U;
    return (uint8_t)(v * 127.0f + 0.5f);
}

static float filter_ui127_clamp(float v)
{
    return clamp_value(v, 0.0f, 127.0f);
}

static float filter_ui127_to_unit(float v)
{
    return filter_ui127_clamp(v) * (1.0f / 127.0f);
}

static float filter_ui127_to_cutoff_hz(float v)
{
    const float t = filter_ui127_to_unit(v);
    const float min_hz = 20.0f;
    const float max_hz = 16000.0f;
    const float ratio = max_hz / min_hz;

    return min_hz * powf(ratio, t);
}

static float filter_ui127_to_resonance(float v)
{
    const float t = filter_ui127_to_unit(v);
    const float shaped = t * t;

    return shaped * 0.75f;
}

static float filter_ui127_to_eg_amount(float v)
{
    return filter_ui127_to_unit(v);
}

static float filter_ui127_to_time_s(float v, float min_s, float max_s)
{
    const float t = filter_ui127_to_unit(v);
    const float ratio = max_s / min_s;

    return min_s * powf(ratio, t);
}

static float filter_ui127_to_attack_s(float v)
{
    return filter_ui127_to_time_s(v, 0.001f, 5.0f);
}

static float filter_ui127_to_decay_s(float v)
{
    return filter_ui127_to_time_s(v, 0.001f, 5.0f);
}

static float filter_ui127_to_sustain(float v)
{
    return filter_ui127_to_unit(v);
}

static float filter_ui127_to_release_s(float v)
{
    return filter_ui127_to_time_s(v, 0.001f, 5.0f);
}

static float filter_eq_ui127_to_db(float v)
{
    const float clamped = filter_ui127_clamp(v);

    if(clamped <= 64.0f)
    {
        return -80.0f + ((clamped / 64.0f) * 80.0f);
    }

    return ((clamped - 64.0f) / 63.0f) * 12.0f;
}

static void apply_mix_track0_gain(float v) { mixer_set_track_gain(0U, v); }
static void apply_mix_track1_gain(float v) { mixer_set_track_gain(1U, v); }
static void apply_mix_track2_gain(float v) { mixer_set_track_gain(2U, v); }
static void apply_mix_track3_gain(float v) { mixer_set_track_gain(3U, v); }

static void apply_mix_track0_pan(float v) { mixer_set_track_pan(0U, v); }
static void apply_mix_track1_pan(float v) { mixer_set_track_pan(1U, v); }
static void apply_mix_track2_pan(float v) { mixer_set_track_pan(2U, v); }
static void apply_mix_track3_pan(float v) { mixer_set_track_pan(3U, v); }

static void apply_mix_track0_mute(float v) { mixer_set_track_mute(0U, (v >= 0.5f) ? 1U : 0U); }
static void apply_mix_track1_mute(float v) { mixer_set_track_mute(1U, (v >= 0.5f) ? 1U : 0U); }
static void apply_mix_track2_mute(float v) { mixer_set_track_mute(2U, (v >= 0.5f) ? 1U : 0U); }
static void apply_mix_track3_mute(float v) { mixer_set_track_mute(3U, (v >= 0.5f) ? 1U : 0U); }

static void apply_mix_track0_route(float v) { mixer_set_track_route(0U, (mixer_route_t)((uint32_t)v & 0x3U)); }
static void apply_mix_track1_route(float v) { mixer_set_track_route(1U, (mixer_route_t)((uint32_t)v & 0x3U)); }
static void apply_mix_track2_route(float v) { mixer_set_track_route(2U, (mixer_route_t)((uint32_t)v & 0x3U)); }
static void apply_mix_track3_route(float v) { mixer_set_track_route(3U, (mixer_route_t)((uint32_t)v & 0x3U)); }

static void apply_mix_track0_insert0(float v) { mixer_set_track_insert_slot(0U, 0U, control_float_to_slot(v)); }
static void apply_mix_track0_insert1(float v) { mixer_set_track_insert_slot(0U, 1U, control_float_to_slot(v)); }
static void apply_mix_track1_insert0(float v) { mixer_set_track_insert_slot(1U, 0U, control_float_to_slot(v)); }
static void apply_mix_track1_insert1(float v) { mixer_set_track_insert_slot(1U, 1U, control_float_to_slot(v)); }
static void apply_mix_track2_insert0(float v) { mixer_set_track_insert_slot(2U, 0U, control_float_to_slot(v)); }
static void apply_mix_track2_insert1(float v) { mixer_set_track_insert_slot(2U, 1U, control_float_to_slot(v)); }
static void apply_mix_track3_insert0(float v) { mixer_set_track_insert_slot(3U, 0U, control_float_to_slot(v)); }
static void apply_mix_track3_insert1(float v) { mixer_set_track_insert_slot(3U, 1U, control_float_to_slot(v)); }

static void apply_mix_track0_send0(float v) { mixer_set_track_send_level(0U, 0U, v); }
static void apply_mix_track0_send1(float v) { mixer_set_track_send_level(0U, 1U, v); }
static void apply_mix_track1_send0(float v) { mixer_set_track_send_level(1U, 0U, v); }
static void apply_mix_track1_send1(float v) { mixer_set_track_send_level(1U, 1U, v); }
static void apply_mix_track2_send0(float v) { mixer_set_track_send_level(2U, 0U, v); }
static void apply_mix_track2_send1(float v) { mixer_set_track_send_level(2U, 1U, v); }
static void apply_mix_track3_send0(float v) { mixer_set_track_send_level(3U, 0U, v); }
static void apply_mix_track3_send1(float v) { mixer_set_track_send_level(3U, 1U, v); }

static void apply_mix_send0_fx(float v) { mixer_set_send_fx_slot(0U, control_float_to_slot(v)); }
static void apply_mix_send1_fx(float v) { mixer_set_send_fx_slot(1U, control_float_to_slot(v)); }

static void apply_dx7_algorithm(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_ALGORITHM, v); }
static void apply_dx7_feedback(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_FEEDBACK, v); }
static void apply_dx7_transpose(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_TRANSPOSE, v); }
static void apply_dx7_lfo_speed(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_LFO_SPEED, v); }
static void apply_dx7_lfo_delay(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_LFO_DELAY, v); }
static void apply_dx7_lfo_pitch_mod_depth(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_LFO_PITCH_MOD_DEPTH, v); }
static void apply_dx7_lfo_amp_mod_depth(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_LFO_AMP_MOD_DEPTH, v); }
static void apply_dx7_pitch_bend_range(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_PITCH_BEND_RANGE, v); }
static void apply_dx7_portamento_time(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_PORTAMENTO_TIME, v); }
static void apply_dx7_mono_mode(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_MONO_MODE, v); }
static void apply_dx7_operator_mask(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_OPERATOR_MASK, v); }
static void apply_dx7_operator_1_level(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_OPERATOR_1_LEVEL, v); }
static void apply_dx7_operator_2_level(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_OPERATOR_2_LEVEL, v); }
static void apply_dx7_operator_3_level(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_OPERATOR_3_LEVEL, v); }
static void apply_dx7_operator_4_level(float v) { microdexed_synth_set_param(MICRODEXED_PARAM_OPERATOR_4_LEVEL, v); }

static fx_granular_state_t *get_active_granular_state(void)
{
    for (uint32_t i = 0U;; ++i)
    {
        fx_slot_t *slot = fx_pool_get_slot(i);
        if (slot == NULL)
            break;

        if ((slot->active != 0U) && ((fx_type_t)slot->type == FX_GRANULAR) && (slot->state != NULL))
            return (fx_granular_state_t *)slot->state;
    }

    return NULL;
}

/**
 * @brief Point d'entrée apply_gran_density.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_gran_density.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_gran_density(float v)
{
    fx_granular_state_t *state = get_active_granular_state();
    if (state != NULL) fx_granular_set_density(state, v);
}

/**
 * @brief Point d'entrée apply_gran_pitch.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_gran_pitch.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_gran_pitch(float v)
{
    fx_granular_state_t *state = get_active_granular_state();
    if (state != NULL) fx_granular_set_pitch(state, v);
}

/**
 * @brief Point d'entrée apply_gran_mix.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_gran_mix.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_gran_mix(float v)
{
    fx_granular_state_t *state = get_active_granular_state();
    if (state != NULL) fx_granular_set_mix(state, v);
}

/**
 * @brief Point d'entrée apply_gran_freeze.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_gran_freeze.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_gran_freeze(float v)
{
    fx_granular_state_t *state = get_active_granular_state();
    if (state != NULL) fx_granular_set_freeze(state, (v >= 0.5f));
}

/**
 * @brief Point d'entrée apply_gran_spread.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_gran_spread.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_gran_spread(float v)
{
    fx_granular_state_t *state = get_active_granular_state();
    if (state != NULL) fx_granular_set_spread(state, v);
}

/**
 * @brief Point d'entrée apply_gran_stereo.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_gran_stereo.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_gran_stereo(float v)
{
    fx_granular_state_t *state = get_active_granular_state();
    if (state != NULL) fx_granular_set_stereo_offset(state, v);
}

static void apply_eq_low_db(float v) { audio_float_set_dj_eq_low_db(v); }
static void apply_eq_mid_db(float v) { audio_float_set_dj_eq_mid_db(v); }
static void apply_eq_high_db(float v) { audio_float_set_dj_eq_high_db(v); }

static void apply_sat_tone(float v) { audio_float_set_saturation_tone_ui(control_float_to_ui127(v)); }
static void apply_sat_bias(float v) { audio_float_set_saturation_bias_ui(control_float_to_ui127(v)); }
static void apply_sat_drive(float v) { audio_float_set_saturation_drive_ui(control_float_to_ui127(v)); }
static void apply_sat_mix(float v) { audio_float_set_saturation_mix_ui(control_float_to_ui127(v)); }

/*
 * Variante FILTER audio:
 * - le runtime audio n'expose plus que Off / EQ3 / biquad CMSIS
 * - le système de paramètres conserve pour l'instant un jeu global `PARAM_FILTER_*`
 * - ces paramètres pilotent donc la track 0 par convention provisoire
 */
static void apply_filter_type(float v)
{
    mixer_set_track_filter_type(0U, (mixer_track_filter_type_t)((uint32_t)(clamp_value(v, 0.0f, 4.0f) + 0.5f)));
}

static void apply_filter_cutoff(float v) { mixer_set_track_filter_cutoff(0U, filter_ui127_to_cutoff_hz(v)); }
static void apply_filter_resonance(float v) { mixer_set_track_filter_resonance(0U, filter_ui127_to_resonance(v)); }
static void apply_filter_eg_amount(float v) { mixer_set_track_filter_eg_amount(0U, filter_ui127_to_eg_amount(v)); }
static void apply_filter_attack(float v) { mixer_set_track_filter_attack(0U, filter_ui127_to_attack_s(v)); }
static void apply_filter_decay(float v) { mixer_set_track_filter_decay(0U, filter_ui127_to_decay_s(v)); }
static void apply_filter_sustain(float v) { mixer_set_track_filter_sustain(0U, filter_ui127_to_sustain(v)); }
static void apply_filter_release(float v) { mixer_set_track_filter_release(0U, filter_ui127_to_release_s(v)); }
static void apply_filter_eq_low(float v) { mixer_set_track_filter_eq_low(0U, filter_eq_ui127_to_db(v)); }
static void apply_filter_eq_mid(float v) { mixer_set_track_filter_eq_mid(0U, filter_eq_ui127_to_db(v)); }
static void apply_filter_eq_high(float v) { mixer_set_track_filter_eq_high(0U, filter_eq_ui127_to_db(v)); }

static void apply_monob_filter_type(float v) { monob_synth_set_filter_type((uint8_t)(clamp_value(v, 0.0f, 1.0f) + 0.5f)); }
static void apply_monob_filter_cutoff(float v) { monob_synth_set_filter_cutoff(filter_ui127_to_cutoff_hz(v)); }
static void apply_monob_filter_resonance(float v) { monob_synth_set_filter_resonance(filter_ui127_to_resonance(v)); }
static void apply_monob_filter_eg_amount(float v) { monob_synth_set_filter_eg_amount(filter_ui127_to_eg_amount(v)); }
static void apply_monob_filter_attack(float v) { monob_synth_set_filter_attack(filter_ui127_to_attack_s(v)); }
static void apply_monob_filter_decay(float v) { monob_synth_set_filter_decay(filter_ui127_to_decay_s(v)); }
static void apply_monob_filter_sustain(float v) { monob_synth_set_filter_sustain(filter_ui127_to_sustain(v)); }
static void apply_monob_filter_release(float v) { monob_synth_set_filter_release(filter_ui127_to_release_s(v)); }

static void apply_cfg_track(float v)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_track_family_t requested_family = (ui_track_family_t)((uint8_t)(clamp_value(v, 0.0f, (float)((uint8_t)UI_TRACK_FAMILY_COUNT - 1U)) + 0.5f));

    if (ui_set_track_family(active_track, requested_family) == false)
    {
        param_store_set_active(PARAM_CFG_TRACK, (float)ui_get_track_family(active_track));
        param_store_set_active(PARAM_CFG_TRACK_TYPE, (float)ui_get_track_type_index_for_family(ui_get_track_family(active_track), ui_get_track_type(active_track)));
        return;
    }

    param_store_set_active(PARAM_CFG_TRACK, (float)ui_get_track_family(active_track));
    param_store_set_active(PARAM_CFG_TRACK_TYPE, (float)ui_get_track_type_index_for_family(ui_get_track_family(active_track), ui_get_track_type(active_track)));
}

static void apply_cfg_track_type(float v)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_track_family_t active_family = ui_get_track_family(active_track);
    const uint8_t requested_index = (uint8_t)(clamp_value(v, 0.0f, (float)((uint8_t)UI_TRACK_TYPE_COUNT - 1U)) + 0.5f);
    const ui_track_type_t requested_type = ui_get_track_type_from_family_index(active_family, requested_index);

    if (ui_set_track_type(active_track, requested_type) == false)
    {
        param_store_set_active(PARAM_CFG_TRACK_TYPE, (float)ui_get_track_type_index_for_family(active_family, ui_get_track_type(active_track)));
        return;
    }

    param_store_set_active(PARAM_CFG_TRACK_TYPE, (float)ui_get_track_type_index_for_family(active_family, ui_get_track_type(active_track)));
}

static void apply_master_gain(float v) { audio_float_set_master_gain(v); }
static void apply_post_gain(float v) { audio_float_set_postgain(v); }
static void apply_output_comp(float v) { audio_float_set_output_compensation(v); }

static void apply_juno_saw(float v) { juno_synth_set_param(JUNO_PARAM_SAW, v); }
static void apply_juno_pulse(float v) { juno_synth_set_param(JUNO_PARAM_PULSE, v); }
static void apply_juno_sub(float v) { juno_synth_set_param(JUNO_PARAM_SUB, v); }
static void apply_juno_pwm(float v) { juno_synth_set_param(JUNO_PARAM_PWM, v); }
static void apply_juno_vcf_freq(float v) { juno_synth_set_param(JUNO_PARAM_VCF_FREQ, v); }
static void apply_juno_vcf_res(float v) { juno_synth_set_param(JUNO_PARAM_VCF_RES, v); }
static void apply_juno_vcf_env(float v) { juno_synth_set_param(JUNO_PARAM_VCF_ENV, v); }
static void apply_juno_vcf_lfo(float v) { juno_synth_set_param(JUNO_PARAM_VCF_LFO, v); }
static void apply_juno_attack(float v) { juno_synth_set_param(JUNO_PARAM_ATTACK, v); }
static void apply_juno_decay(float v) { juno_synth_set_param(JUNO_PARAM_DECAY, v); }
static void apply_juno_sustain(float v) { juno_synth_set_param(JUNO_PARAM_SUSTAIN, v); }
static void apply_juno_release(float v) { juno_synth_set_param(JUNO_PARAM_RELEASE, v); }
static void apply_juno_lfo_rate(float v) { juno_synth_set_param(JUNO_PARAM_LFO_RATE, v); }
static void apply_juno_hpf(float v) { juno_synth_set_param(JUNO_PARAM_HPF, v); }
static void apply_juno_porta(float v) { juno_synth_set_param(JUNO_PARAM_PORTA, v); }
static void apply_juno_mode(float v) { juno_synth_set_param(JUNO_PARAM_MODE, v); }

static void apply_bus_comp_threshold(float v) { audio_float_set_bus_comp_threshold_db(v); }
static void apply_bus_comp_ratio(float v) { audio_float_set_bus_comp_ratio(v); }
static void apply_bus_comp_attack_index(float v) { audio_float_set_bus_comp_attack_index((uint8_t)v); }
static void apply_bus_comp_release_index(float v) { audio_float_set_bus_comp_release_index((uint8_t)v); }
static void apply_bus_comp_makeup(float v) { audio_float_set_bus_comp_makeup_db(v); }
static void apply_bus_comp_auto_makeup(float v) { audio_float_set_bus_comp_auto_makeup((v >= 0.5f) ? 1U : 0U); }

/**
 * @brief Point d'entrée apply_daisy_threshold.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_threshold.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_threshold(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_threshold_db(comp, v);
}

/**
 * @brief Point d'entrée apply_daisy_ratio.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_ratio.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_ratio(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_ratio(comp, v);
}

/**
 * @brief Point d'entrée apply_daisy_attack.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_attack.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_attack(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_attack_s(comp, v);
}

/**
 * @brief Point d'entrée apply_daisy_release.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_release.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_release(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_release_s(comp, v);
}

/**
 * @brief Point d'entrée apply_daisy_makeup.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_makeup.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_makeup(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_makeup_db(comp, v);
}

/**
 * @brief Point d'entrée apply_daisy_auto_makeup.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_auto_makeup.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_auto_makeup(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_auto_makeup(comp, (v >= 0.5f) ? 1U : 0U);
}

/**
 * @brief Point d'entrée apply_daisy_mix.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_mix.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_mix(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_mix(comp, v);
}

#define PARAM_DESC_EX(_id, _name, _type, _min, _max, _step, _default, _display, _unit, _labels, _apply) \
    [(_id)] = {                                                                  \
        .id = (_id),                                                             \
        .name = (_name),                                                         \
        .type = (_type),                                                         \
        .min = (_min),                                                           \
        .max = (_max),                                                           \
        .step = (_step),                                                         \
        .default_value = (_default),                                             \
        .display_type = (_display),                                              \
        .unit = (_unit),                                                         \
        .labels = (_labels),                                                     \
        .apply = (_apply),                                                       \
    }

#define PARAM_DESC(_id, _name, _type, _min, _max, _step, _default, _unit, _apply)                           \
    PARAM_DESC_EX((_id),                                                                                      \
                  (_name),                                                                                    \
                  (_type),                                                                                    \
                  (_min),                                                                                     \
                  (_max),                                                                                     \
                  (_step),                                                                                    \
                  (_default),                                                                                 \
                  (((_type) == PARAM_TYPE_BOOL) ? PARAM_DISPLAY_BOOL                                         \
                                                 : (((_type) == PARAM_TYPE_ENUM) ? PARAM_DISPLAY_ENUM        \
                                                                            : PARAM_DISPLAY_FLOAT)),         \
                  (_unit),                                                                                    \
                  NULL,                                                                                       \
                  (_apply))

static const char *const g_bool_labels[] = {"Off", "On", NULL};
static const char *const g_juno_mode_labels[] = {"Poly", "Poly+Porta", "Unison", NULL};
static const char *const g_juno_hpf_labels[] = {"0", "1", "2", "3", NULL};
static const char *const g_route_labels[] = {"None", "Master", "Cue", "Both", NULL};
static const char *const g_filter_type_labels[] = {"Off", "EQ3", "LP BI", "HP BI", "BP BI", NULL};
static const char *const g_monob_filter_type_labels[] = {"Off", "On", NULL};
static const char *const g_track_family_labels[] = {"Input1", "Input2", "Input3", "Input4", "Synth", NULL};

const param_desc_t param_registry[PARAM_COUNT] = {
    PARAM_DESC_EX(PARAM_GRAN_DENSITY, "Gran Density", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_gran_density),
    PARAM_DESC(PARAM_GRAN_PITCH, "Gran Pitch", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 0.1f, 0.0f, "st", apply_gran_pitch),
    PARAM_DESC_EX(PARAM_GRAN_MIX, "Gran Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_gran_mix),
    PARAM_DESC_EX(PARAM_GRAN_FREEZE, "Gran Freeze", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_gran_freeze),
    PARAM_DESC_EX(PARAM_GRAN_SPREAD, "Gran Spread", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_gran_spread),
    PARAM_DESC_EX(PARAM_GRAN_STEREO, "Gran Stereo", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_gran_stereo),

    PARAM_DESC(PARAM_MIX_TRACK0_GAIN, "T0 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", apply_mix_track0_gain),
    PARAM_DESC(PARAM_MIX_TRACK1_GAIN, "T1 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", apply_mix_track1_gain),
    PARAM_DESC(PARAM_MIX_TRACK2_GAIN, "T2 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", apply_mix_track2_gain),
    PARAM_DESC(PARAM_MIX_TRACK3_GAIN, "T3 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", apply_mix_track3_gain),

    PARAM_DESC(PARAM_MIX_TRACK0_PAN, "T0 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track0_pan),
    PARAM_DESC(PARAM_MIX_TRACK1_PAN, "T1 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track1_pan),
    PARAM_DESC(PARAM_MIX_TRACK2_PAN, "T2 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track2_pan),
    PARAM_DESC(PARAM_MIX_TRACK3_PAN, "T3 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track3_pan),

    PARAM_DESC_EX(PARAM_MIX_TRACK0_MUTE, "T0 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_mix_track0_mute),
    PARAM_DESC_EX(PARAM_MIX_TRACK1_MUTE, "T1 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_mix_track1_mute),
    PARAM_DESC_EX(PARAM_MIX_TRACK2_MUTE, "T2 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_mix_track2_mute),
    PARAM_DESC_EX(PARAM_MIX_TRACK3_MUTE, "T3 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_mix_track3_mute),

    PARAM_DESC_EX(PARAM_MIX_TRACK0_ROUTE, "T0 Route", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_route_labels, apply_mix_track0_route),
    PARAM_DESC_EX(PARAM_MIX_TRACK1_ROUTE, "T1 Route", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_route_labels, apply_mix_track1_route),
    PARAM_DESC_EX(PARAM_MIX_TRACK2_ROUTE, "T2 Route", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_route_labels, apply_mix_track2_route),
    PARAM_DESC_EX(PARAM_MIX_TRACK3_ROUTE, "T3 Route", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_route_labels, apply_mix_track3_route),

    PARAM_DESC(PARAM_MIX_TRACK0_INSERT0, "T0 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track0_insert0),
    PARAM_DESC(PARAM_MIX_TRACK0_INSERT1, "T0 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track0_insert1),
    PARAM_DESC(PARAM_MIX_TRACK1_INSERT0, "T1 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track1_insert0),
    PARAM_DESC(PARAM_MIX_TRACK1_INSERT1, "T1 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track1_insert1),
    PARAM_DESC(PARAM_MIX_TRACK2_INSERT0, "T2 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track2_insert0),
    PARAM_DESC(PARAM_MIX_TRACK2_INSERT1, "T2 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track2_insert1),
    PARAM_DESC(PARAM_MIX_TRACK3_INSERT0, "T3 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track3_insert0),
    PARAM_DESC(PARAM_MIX_TRACK3_INSERT1, "T3 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track3_insert1),

    PARAM_DESC(PARAM_MIX_TRACK0_SEND0, "T0 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track0_send0),
    PARAM_DESC(PARAM_MIX_TRACK0_SEND1, "T0 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track0_send1),
    PARAM_DESC(PARAM_MIX_TRACK1_SEND0, "T1 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track1_send0),
    PARAM_DESC(PARAM_MIX_TRACK1_SEND1, "T1 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track1_send1),
    PARAM_DESC(PARAM_MIX_TRACK2_SEND0, "T2 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track2_send0),
    PARAM_DESC(PARAM_MIX_TRACK2_SEND1, "T2 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track2_send1),
    PARAM_DESC(PARAM_MIX_TRACK3_SEND0, "T3 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track3_send0),
    PARAM_DESC(PARAM_MIX_TRACK3_SEND1, "T3 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track3_send1),

    PARAM_DESC(PARAM_MIX_SEND0_FX, "Send0 FX", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_send0_fx),
    PARAM_DESC(PARAM_MIX_SEND1_FX, "Send1 FX", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_send1_fx),

    PARAM_DESC_EX(PARAM_BUS_COMP_THRESHOLD_DB, "BusComp Threshold", PARAM_TYPE_FLOAT, -60.0f, 0.0f, 0.5f, -18.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_bus_comp_threshold),
    PARAM_DESC_EX(PARAM_BUS_COMP_RATIO, "BusComp Ratio", PARAM_TYPE_FLOAT, 1.0f, 20.0f, 0.1f, 2.0f, PARAM_DISPLAY_RATIO, "", NULL, apply_bus_comp_ratio),
    /* DSP side clamps attack index to [0..5], keep registry range aligned for consistent UI. */
    PARAM_DESC(PARAM_BUS_COMP_ATTACK_INDEX, "BusComp Attack", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 0.0f, "idx", apply_bus_comp_attack_index),
    /* DSP side clamps release index to [0..4], keep registry range aligned for consistent UI. */
    PARAM_DESC(PARAM_BUS_COMP_RELEASE_INDEX, "BusComp Release", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, "idx", apply_bus_comp_release_index),
    PARAM_DESC_EX(PARAM_BUS_COMP_MAKEUP_DB, "BusComp Makeup", PARAM_TYPE_FLOAT, 0.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_bus_comp_makeup),
    PARAM_DESC_EX(PARAM_BUS_COMP_AUTO_MAKEUP, "BusComp AutoMakeup", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_bus_comp_auto_makeup),
    PARAM_DESC_EX(PARAM_BUS_COMP_DRYWET, "BusComp DryWet", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_BUS_COMP_HPF_HZ, "BusComp HPF", PARAM_TYPE_FLOAT, 0.0f, 1000.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "Hz", NULL, NULL),

    PARAM_DESC_EX(PARAM_DAISY_COMP_THRESHOLD_DB, "Daisy Threshold", PARAM_TYPE_FLOAT, -40.0f, 0.0f, 0.5f, -18.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_daisy_threshold),
    PARAM_DESC_EX(PARAM_DAISY_COMP_RATIO, "Daisy Ratio", PARAM_TYPE_FLOAT, 1.0f, 20.0f, 0.1f, 2.0f, PARAM_DISPLAY_RATIO, "", NULL, apply_daisy_ratio),
    PARAM_DESC_EX(PARAM_DAISY_COMP_ATTACK_S, "Daisy Attack", PARAM_TYPE_FLOAT, 0.0001f, 0.5f, 0.0001f, 0.001f, PARAM_DISPLAY_TIME_MS, "s", NULL, apply_daisy_attack),
    PARAM_DESC_EX(PARAM_DAISY_COMP_RELEASE_S, "Daisy Release", PARAM_TYPE_FLOAT, 0.01f, 5.0f, 0.01f, 0.6f, PARAM_DISPLAY_TIME_MS, "s", NULL, apply_daisy_release),
    PARAM_DESC_EX(PARAM_DAISY_COMP_MAKEUP_DB, "Daisy Makeup", PARAM_TYPE_FLOAT, 0.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_daisy_makeup),
    PARAM_DESC_EX(PARAM_DAISY_COMP_AUTO_MAKEUP, "Daisy AutoMakeup", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_daisy_auto_makeup),
    PARAM_DESC_EX(PARAM_DAISY_COMP_MIX, "Daisy Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_daisy_mix),

    PARAM_DESC_EX(PARAM_EQ_LOW_DB, "EQ Low", PARAM_TYPE_FLOAT, -24.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_eq_low_db),
    PARAM_DESC_EX(PARAM_EQ_MID_DB, "EQ Mid", PARAM_TYPE_FLOAT, -24.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_eq_mid_db),
    PARAM_DESC_EX(PARAM_EQ_HIGH_DB, "EQ High", PARAM_TYPE_FLOAT, -24.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_eq_high_db),

    PARAM_DESC_EX(PARAM_SAT_TONE, "Sat Tone", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_tone),
    PARAM_DESC_EX(PARAM_SAT_BIAS, "Sat Bias", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_bias),
    PARAM_DESC_EX(PARAM_SAT_DRIVE, "Sat Drive", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_drive),
    PARAM_DESC_EX(PARAM_SAT_MIX, "Sat Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_mix),

    PARAM_DESC_EX(PARAM_FILTER_TYPE, "F Type", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_filter_type_labels, apply_filter_type),
    PARAM_DESC_EX(PARAM_FILTER_CUTOFF, "Cutoff", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_cutoff),
    PARAM_DESC_EX(PARAM_FILTER_RESONANCE, "Res", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_resonance),
    PARAM_DESC_EX(PARAM_FILTER_EG_AMT, "EG Amt", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_eg_amount),
    PARAM_DESC_EX(PARAM_FILTER_ATTACK, "Attack", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 34.3f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_attack),
    PARAM_DESC_EX(PARAM_FILTER_DECAY, "Decay", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 68.7f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_decay),
    PARAM_DESC_EX(PARAM_FILTER_SUSTAIN, "Sus", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_sustain),
    PARAM_DESC_EX(PARAM_FILTER_RELEASE, "Rel", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 68.7f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_release),
    PARAM_DESC_EX(PARAM_FILTER_EQ_LOW, "Low", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_eq_low),
    PARAM_DESC_EX(PARAM_FILTER_EQ_MID, "Mid", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_eq_mid),
    PARAM_DESC_EX(PARAM_FILTER_EQ_HIGH, "High", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_eq_high),

    PARAM_DESC_EX(PARAM_CFG_TRACK, "Track", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_track_family_labels, apply_cfg_track),
    PARAM_DESC_EX(PARAM_CFG_TRACK_TYPE, "Type", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", NULL, apply_cfg_track_type),

    PARAM_DESC_EX(PARAM_MASTER_GAIN, "Master Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_master_gain),
    PARAM_DESC_EX(PARAM_POST_GAIN, "Post Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_post_gain),
    PARAM_DESC_EX(PARAM_OUTPUT_COMP, "Output Comp", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_output_comp),

    PARAM_DESC_EX(PARAM_JUNO_SAW, "SAW", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.05f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_saw),
    PARAM_DESC_EX(PARAM_JUNO_PULSE, "PULSE", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.05f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_pulse),
    PARAM_DESC_EX(PARAM_JUNO_SUB, "SUB", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.05f, 0.32f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_sub),
    PARAM_DESC_EX(PARAM_JUNO_PWM, "PWM", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.38f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_pwm),
    PARAM_DESC_EX(PARAM_JUNO_VCF_FREQ, "VCF FREQ", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.58f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_vcf_freq),
    PARAM_DESC_EX(PARAM_JUNO_VCF_RES, "VCF RES", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.18f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_vcf_res),
    PARAM_DESC_EX(PARAM_JUNO_VCF_ENV, "VCF ENV", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.42f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_vcf_env),
    PARAM_DESC_EX(PARAM_JUNO_VCF_LFO, "VCF LFO", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.06f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_vcf_lfo),
    PARAM_DESC_EX(PARAM_JUNO_ATTACK, "ATTACK", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.22f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_attack),
    PARAM_DESC_EX(PARAM_JUNO_DECAY, "DECAY", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.44f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_decay),
    PARAM_DESC_EX(PARAM_JUNO_SUSTAIN, "SUSTAIN", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.68f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_sustain),
    PARAM_DESC_EX(PARAM_JUNO_RELEASE, "RELEASE", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.46f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_release),
    PARAM_DESC_EX(PARAM_JUNO_LFO_RATE, "LFO RATE", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.18f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_lfo_rate),
    PARAM_DESC_EX(PARAM_JUNO_HPF, "HPF", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_juno_hpf_labels, apply_juno_hpf),
    PARAM_DESC_EX(PARAM_JUNO_PORTA, "PORTA", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.22f, PARAM_DISPLAY_PERCENT, "", NULL, apply_juno_porta),
    PARAM_DESC_EX(PARAM_JUNO_MODE, "MODE", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_juno_mode_labels, apply_juno_mode),

    PARAM_DESC(PARAM_DX7_ALGORITHM, "ALGO", PARAM_TYPE_INT, 0.0f, 31.0f, 1.0f, 4.0f, "", apply_dx7_algorithm),
    PARAM_DESC(PARAM_DX7_FEEDBACK, "FDBK", PARAM_TYPE_INT, 0.0f, 7.0f, 1.0f, 6.0f, "", apply_dx7_feedback),
    PARAM_DESC(PARAM_DX7_TRANSPOSE, "TRANS", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 1.0f, 0.0f, "st", apply_dx7_transpose),
    PARAM_DESC(PARAM_DX7_LFO_SPEED, "LFO SPD", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 34.0f, "", apply_dx7_lfo_speed),
    PARAM_DESC(PARAM_DX7_LFO_DELAY, "LFO DLY", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 33.0f, "", apply_dx7_lfo_delay),
    PARAM_DESC(PARAM_DX7_LFO_PITCH_MOD_DEPTH, "PMD", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 0.0f, "", apply_dx7_lfo_pitch_mod_depth),
    PARAM_DESC(PARAM_DX7_LFO_AMP_MOD_DEPTH, "AMD", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 0.0f, "", apply_dx7_lfo_amp_mod_depth),
    PARAM_DESC(PARAM_DX7_PITCH_BEND_RANGE, "BEND", PARAM_TYPE_INT, 0.0f, 12.0f, 1.0f, 2.0f, "st", apply_dx7_pitch_bend_range),
    PARAM_DESC(PARAM_DX7_PORTAMENTO_TIME, "PORTA", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, "", apply_dx7_portamento_time),
    PARAM_DESC_EX(PARAM_DX7_MONO_MODE, "MONO", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_dx7_mono_mode),
    PARAM_DESC(PARAM_DX7_OPERATOR_MASK, "OPS", PARAM_TYPE_INT, 0.0f, 63.0f, 1.0f, 63.0f, "", apply_dx7_operator_mask),
    PARAM_DESC(PARAM_DX7_OPERATOR_1_LEVEL, "OP1", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 79.0f, "", apply_dx7_operator_1_level),
    PARAM_DESC(PARAM_DX7_OPERATOR_2_LEVEL, "OP2", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 99.0f, "", apply_dx7_operator_2_level),
    PARAM_DESC(PARAM_DX7_OPERATOR_3_LEVEL, "OP3", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 89.0f, "", apply_dx7_operator_3_level),
    PARAM_DESC(PARAM_DX7_OPERATOR_4_LEVEL, "OP4", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 99.0f, "", apply_dx7_operator_4_level),

    PARAM_DESC_EX(PARAM_MONOB_FILTER_TYPE, "F Type", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_monob_filter_type_labels, apply_monob_filter_type),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_CUTOFF, "Cutoff", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_cutoff),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_RESONANCE, "Res", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_resonance),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_EG_AMT, "Eg amount", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_eg_amount),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_ATTACK, "A", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 34.3f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_attack),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_DECAY, "D", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 68.7f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_decay),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_SUSTAIN, "S", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_sustain),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_RELEASE, "R", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 68.7f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_release),
};

/**
 * @brief Point d'entrée param_registry_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_registry_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void param_registry_init(void)
{
    /* Registry is static metadata; runtime values are in param_store. */
}

/**
 * @brief Point d'entrée param_get.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_get.
 *
 * @param id Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
float param_get(param_id_t id)
{
    return param_store_get_active(id);
}

/**
 * @brief Point d'entrée param_set.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_set.
 *
 * @param id Paramètre d'entrée de l'API.
 * @param value Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void param_set(param_id_t id, float value)
{
    if (id >= PARAM_COUNT)
        return;

    const param_desc_t *desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);

    param_store_set_active(id, clamped);

    if (desc->apply != NULL)
    {
        desc->apply(clamped);
    }
}

/**
 * @brief Point d'entrée param_reset.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_reset.
 *
 * @param id Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void param_reset(param_id_t id)
{
    if (id >= PARAM_COUNT)
        return;

    param_set(id, param_registry[id].default_value);
}
