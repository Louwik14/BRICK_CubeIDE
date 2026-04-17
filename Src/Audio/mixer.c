/**
 * @file mixer.c
 * @brief Moteur de mixage final track-based (gain/pan/mute/routing/inserts/sends).
 *
 * Rôle du module:
 * - Maintenir l'état runtime des tracks du mixer.
 * - Effectuer le mix final MAIN/CUE avec inserts et send FX.
 *
 * Architecture:
 * - Appelé par: my_dsp() (brick6_app_init.c via dsp_engine).
 * - Appelle: fx_chain_process_slot(), audio_float_set_master_gain().
 *
 * Contraintes temps réel:
 * - IRQ: oui (mixer_process est dans le chemin DSP audio).
 * - Hard realtime: oui.
 * - malloc: interdit.
 *
 * Notes:
 * - Slots insert/send à -1 => FX inactif (coût CPU nul sur le slot).
 */

#include "mixer.h"

#include "adsr_daisy_c.h"
#include "env_adsr.h"
#include "fx_biquad_filter.h"
#include "fx_reverb.h"

#include <math.h>
#include <string.h>

#include "fx_chain.h"
#include "Core/brick6_master_buffer.h"
#include "Core/track_runtime.h"
#include "sd_multitrack_recorder.h"
#include "memory_layout.h"

typedef struct {
    float gain;
    float pan;
    float gain_current;
    float pan_current;
    uint8_t mute;

    uint8_t route_master;
    uint8_t route_cue;

    int8_t insert_slot[MIXER_INSERTS_PER_TRACK];
    float send_level[MIXER_NUM_SENDS];
    float send_level_current[MIXER_NUM_SENDS];
} mixer_track_t;

typedef struct {
    fx_biquad_filter_t biquad;
    env_adsr_t filter_env;
    adsr_daisy_c_t vca_env;
    fx_dj_eq3_t eq3;
    float sample_rate;
    float cutoff_hz;
    float cutoff_target_hz;
    float resonance;
    float resonance_target;
    float eg_amount;
    float keytrack;
    float eq_low_db;
    float eq_low_target_db;
    float eq_mid_db;
    float eq_mid_target_db;
    float eq_high_db;
    float eq_high_target_db;
    uint8_t note_active;
    uint8_t current_note;
    uint8_t vca_enabled;
    uint8_t vca_note_active;
    uint8_t vca_note_count;
    uint8_t vca_current_note;
    uint8_t vca_gate;
    uint8_t type;
} mixer_track_filter_t;

static mixer_track_t g_tracks[MIXER_MAX_TRACKS];
static int8_t g_send_fx_slot[MIXER_NUM_SENDS];
static mixer_track_filter_t g_track_filters[MIXER_MAX_TRACKS];
static AUDIO_HOT float g_external_track_l[MIXER_MAX_TRACKS][AUDIO_BLOCK_SIZE];
static AUDIO_HOT float g_external_track_r[MIXER_MAX_TRACKS][AUDIO_BLOCK_SIZE];
static uint8_t g_external_track_enabled[MIXER_MAX_TRACKS];

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
#define MIXER_REVERB_PREDELAY_MAX_S 0.090f
#define MIXER_REVERB_SURROUND_MAX_S 0.018f
#define MIXER_ENV_ADSR_MAX_SEGMENT_SECONDS 30.0f

typedef struct
{
    fx_reverb_global_type_t type;
    float wet;
    float size;
    float decay;
    float pre_delay;
    float surround;
} mixer_reverb_state_t;

static mixer_reverb_state_t g_reverb = {
    .type = FX_REVERB_GLOBAL_TYPE_MONO,
    .wet = 0.0f,
    .size = 0.70f,
    .decay = 0.20f,
    .pre_delay = 0.0f,
    .surround = 1.0f,
};

static float mixer_reverb_predelay_ui_to_seconds(float v)
{
    const float clamped = (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
    return clamped * MIXER_REVERB_PREDELAY_MAX_S;
}

static float mixer_reverb_surround_ui_to_seconds(float v)
{
    const float clamped = (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
    return clamped * MIXER_REVERB_SURROUND_MAX_S;
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

static uint8_t mixer_track_filter_type_is_biquad(mixer_track_filter_type_t type)
{
    return ((type == MIXER_TRACK_FILTER_LP_BI)
         || (type == MIXER_TRACK_FILTER_HP_BI)
         || (type == MIXER_TRACK_FILTER_BP_BI)) ? 1U : 0U;
}

static fx_biquad_filter_mode_t mixer_track_filter_type_to_biquad_mode(mixer_track_filter_type_t type)
{
    switch(type)
    {
        case MIXER_TRACK_FILTER_HP_BI:
            return FX_BIQUAD_FILTER_MODE_HP;

        case MIXER_TRACK_FILTER_BP_BI:
            return FX_BIQUAD_FILTER_MODE_BP;

        case MIXER_TRACK_FILTER_LP_BI:
        default:
            return FX_BIQUAD_FILTER_MODE_LP;
    }
}

static float mixer_track_filter_resonance_to_biquad_q(float resonance)
{
    const float clamped = clampf_local(resonance, 0.0f, 1.0f);
    return 0.70710678f + (clamped * 11.29289322f);
}

static uint16_t mixer_track_filter_time_s_to_peaks(float time_s, float sample_rate)
{
    const float clamped = clampf_local(time_s, MIXER_FILTER_ATTACK_MIN_S, MIXER_FILTER_ATTACK_MAX_S);
    const float sr = (sample_rate > 1.0f) ? sample_rate : MIXER_FILTER_SAMPLE_RATE_DEFAULT;
    const float max_segment_samples = sr * MIXER_ENV_ADSR_MAX_SEGMENT_SECONDS;
    const float desired_samples = clampf_local(clamped * sr, 1.0f, max_segment_samples);
    const float normalized = cbrtf((desired_samples - 1.0f) / max_segment_samples);
    const float scaled = clampf_local(normalized, 0.0f, 1.0f) * 65535.0f;
    return (uint16_t)(scaled + 0.5f);
}

static uint16_t mixer_track_filter_sustain_to_peaks(float sustain)
{
    const float clamped = clampf_local(sustain, 0.0f, 1.0f);
    return (uint16_t)(clamped * 32767.0f + 0.5f);
}

static float mixer_track_vca_clamp_time_s(float time_s)
{
    return clampf_local(time_s, MIXER_FILTER_ATTACK_MIN_S, MIXER_FILTER_ATTACK_MAX_S);
}

static float mixer_track_filter_compute_modulated_cutoff(const mixer_track_filter_t *filter, float env)
{
    float cutoff_hz = filter->cutoff_hz;
    cutoff_hz = clampf_local(cutoff_hz, MIXER_FILTER_CUTOFF_MIN_HZ, MIXER_FILTER_CUTOFF_MAX_HZ);

    if(filter->eg_amount >= 0.0f)
    {
        cutoff_hz += (MIXER_FILTER_CUTOFF_MAX_HZ - cutoff_hz) * filter->eg_amount * env;
    }
    else
    {
        cutoff_hz += (cutoff_hz - MIXER_FILTER_CUTOFF_MIN_HZ) * filter->eg_amount * env;
    }

    return clampf_local(cutoff_hz, MIXER_FILTER_CUTOFF_MIN_HZ, MIXER_FILTER_CUTOFF_MAX_HZ);
}

static void mixer_track_filter_apply_core_params(mixer_track_filter_t *filter)
{
    if(filter == NULL)
        return;

    fx_biquad_filter_set_cutoff(&filter->biquad, filter->cutoff_hz);
    fx_biquad_filter_set_q(&filter->biquad, mixer_track_filter_resonance_to_biquad_q(filter->resonance));
    fx_biquad_filter_set_mode(&filter->biquad,
                              mixer_track_filter_type_to_biquad_mode((mixer_track_filter_type_t)filter->type));
    fx_biquad_filter_set_bypass(&filter->biquad,
                                (mixer_track_filter_type_is_biquad((mixer_track_filter_type_t)filter->type) != 0U) ? 0U : 1U);

    fx_dj_eq3_set_low_db(&filter->eq3, filter->eq_low_db);
    fx_dj_eq3_set_mid_db(&filter->eq3, filter->eq_mid_db);
    fx_dj_eq3_set_high_db(&filter->eq3, filter->eq_high_db);
    fx_dj_eq3_set_bypass(&filter->eq3, (filter->type == (uint8_t)MIXER_TRACK_FILTER_EQ3) ? 0U : 1U);
}

static void mixer_track_filter_reset_dsp(mixer_track_filter_t *filter)
{
    if(filter == NULL)
        return;

    fx_biquad_filter_init(&filter->biquad, filter->sample_rate);
    fx_dj_eq3_init(&filter->eq3, filter->sample_rate, 300.0f, 1000.0f, 0.8f, 4000.0f);
    fx_biquad_filter_reset(&filter->biquad);
    mixer_track_filter_apply_core_params(filter);
}

static void mixer_track_filter_init(mixer_track_filter_t *filter, float sample_rate)
{
    if(filter == NULL)
        return;

    filter->sample_rate = (sample_rate > 0.0f) ? sample_rate : MIXER_FILTER_SAMPLE_RATE_DEFAULT;
    filter->cutoff_hz = MIXER_FILTER_CUTOFF_MAX_HZ;
    filter->cutoff_target_hz = MIXER_FILTER_CUTOFF_MAX_HZ;
    filter->resonance = 0.0f;
    filter->resonance_target = 0.0f;
    filter->eg_amount = 0.0f;
    filter->current_note = MIXER_FILTER_NOTE_REF_MIDI;
    filter->eq_low_db = 0.0f;
    filter->eq_low_target_db = 0.0f;
    filter->eq_mid_db = 0.0f;
    filter->eq_mid_target_db = 0.0f;
    filter->eq_high_db = 0.0f;
    filter->eq_high_target_db = 0.0f;
    filter->type = (uint8_t)MIXER_TRACK_FILTER_OFF;
    filter->note_active = 0U;
    filter->vca_enabled = 0U;
    filter->vca_note_active = 0U;
    filter->vca_note_count = 0U;
    filter->vca_current_note = MIXER_FILTER_NOTE_REF_MIDI;
    filter->vca_gate = 0U;

    env_adsr_init(&filter->filter_env, filter->sample_rate);
    env_adsr_set_attack(&filter->filter_env, mixer_track_filter_time_s_to_peaks(0.01f, filter->sample_rate));
    env_adsr_set_decay(&filter->filter_env, mixer_track_filter_time_s_to_peaks(0.10f, filter->sample_rate));
    env_adsr_set_sustain(&filter->filter_env, mixer_track_filter_sustain_to_peaks(1.0f));
    env_adsr_set_release(&filter->filter_env, mixer_track_filter_time_s_to_peaks(0.10f, filter->sample_rate));
    adsr_daisy_c_init(&filter->vca_env, filter->sample_rate, 1U);
    adsr_daisy_c_set_attack(&filter->vca_env, mixer_track_vca_clamp_time_s(0.001f));
    adsr_daisy_c_set_decay(&filter->vca_env, mixer_track_vca_clamp_time_s(0.001f));
    adsr_daisy_c_set_sustain(&filter->vca_env, 1.0f);
    adsr_daisy_c_set_release(&filter->vca_env, mixer_track_vca_clamp_time_s(0.001f));
    adsr_daisy_c_reset(&filter->vca_env);

    mixer_track_filter_reset_dsp(filter);
}

static void mixer_track_state_reset(mixer_track_t *track)
{
    if (track == NULL)
    {
        return;
    }

    track->gain = 1.0f;
    track->pan = 0.0f;
    track->gain_current = 1.0f;
    track->pan_current = 0.0f;
    track->mute = 0U;
    track->route_master = 1U;
    track->route_cue = 0U;

    for (uint32_t i = 0U; i < MIXER_INSERTS_PER_TRACK; ++i)
    {
        track->insert_slot[i] = -1;
    }

    for (uint32_t s = 0U; s < MIXER_NUM_SENDS; ++s)
    {
        track->send_level[s] = 0.0f;
        track->send_level_current[s] = 0.0f;
    }
}

void mixer_rebind_track_states(const uint8_t *previous_mix_tracks,
                               const uint8_t *next_mix_tracks,
                               uint32_t track_count)
{
    static mixer_track_t previous_tracks[MIXER_MAX_TRACKS];
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
}

static void mixer_track_filter_process_block(mixer_track_filter_t *filter,
                                             float *left,
                                             float *right,
                                             uint32_t frames)
{
    if((filter == NULL) || (left == NULL) || (right == NULL))
        return;

    if(filter->type == (uint8_t)MIXER_TRACK_FILTER_OFF)
        return;

    filter->cutoff_hz = mixer_smooth_block(filter->cutoff_hz, filter->cutoff_target_hz, MIXER_FILTER_BLOCK_SMOOTH);
    filter->resonance = mixer_smooth_block(filter->resonance, filter->resonance_target, MIXER_FILTER_BLOCK_SMOOTH);
    filter->eq_low_db = mixer_smooth_block(filter->eq_low_db, filter->eq_low_target_db, MIXER_FILTER_BLOCK_SMOOTH);
    filter->eq_mid_db = mixer_smooth_block(filter->eq_mid_db, filter->eq_mid_target_db, MIXER_FILTER_BLOCK_SMOOTH);
    filter->eq_high_db = mixer_smooth_block(filter->eq_high_db, filter->eq_high_target_db, MIXER_FILTER_BLOCK_SMOOTH);
    fx_biquad_filter_set_q(&filter->biquad, mixer_track_filter_resonance_to_biquad_q(filter->resonance));
    fx_dj_eq3_set_low_db(&filter->eq3, filter->eq_low_db);
    fx_dj_eq3_set_mid_db(&filter->eq3, filter->eq_mid_db);
    fx_dj_eq3_set_high_db(&filter->eq3, filter->eq_high_db);

    switch((mixer_track_filter_type_t)filter->type)
    {
        case MIXER_TRACK_FILTER_EQ3:
            fx_dj_eq3_process_block(&filter->eq3, left, right, frames);
            break;

        case MIXER_TRACK_FILTER_LP_BI:
        case MIXER_TRACK_FILTER_HP_BI:
        case MIXER_TRACK_FILTER_BP_BI:
            {
                uint32_t cutoff_update_countdown = 0U;
                for(uint32_t i = 0U; i < frames; ++i)
                {
                    const float env = (float)env_adsr_process_step(&filter->filter_env) * (1.0f / 32767.0f);
                    if(cutoff_update_countdown == 0U)
                    {
                        fx_biquad_filter_set_cutoff(&filter->biquad, mixer_track_filter_compute_modulated_cutoff(filter, env));
                        cutoff_update_countdown = MIXER_FILTER_UPDATE_PERIOD - 1U;
                    }
                    else
                    {
                        --cutoff_update_countdown;
                    }

                    fx_biquad_filter_process_block(&filter->biquad, &left[i], &right[i], 1U);
                }
            }
            break;

        default:
            {
                break;
            }
    }
}

/**
 * @brief Point d'entrée clamp01.
 *
 * Rôle:
 * - Exécuter le traitement associé à clamp01.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
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
 * Rôle:
 * - Exécuter le traitement associé à clamp_pan.
 *
 * @param pan Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
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

/**
 * @brief Initialise l'état interne du mixer.
 *
 * Contexte d'appel:
 * - Init application, hors IRQ.
 */
/**
 * @brief Point d'entrée mixer_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à mixer_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_init(void)
{
    fx_reverb_global_init(MIXER_FILTER_SAMPLE_RATE_DEFAULT);
    fx_reverb_global_set_type(g_reverb.type);
    fx_reverb_global_set_wet(g_reverb.wet);
    fx_reverb_global_set_size(g_reverb.size);
    fx_reverb_global_set_decay(g_reverb.decay);
    fx_reverb_global_set_predelay(mixer_reverb_predelay_ui_to_seconds(g_reverb.pre_delay));
    fx_reverb_global_set_surround(mixer_reverb_surround_ui_to_seconds(g_reverb.surround));

    for(uint32_t t = 0; t < MIXER_MAX_TRACKS; t++)
    {
        g_tracks[t].gain = 1.0f;
        g_tracks[t].pan = 0.0f;
        g_tracks[t].gain_current = 1.0f;
        g_tracks[t].pan_current = 0.0f;
        g_tracks[t].mute = 0U;

        g_tracks[t].route_master = 1U;
        g_tracks[t].route_cue = 0U;

        for(uint32_t i = 0; i < MIXER_INSERTS_PER_TRACK; i++)
            g_tracks[t].insert_slot[i] = -1;

        for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
        {
            g_tracks[t].send_level[s] = 0.0f;
            g_tracks[t].send_level_current[s] = 0.0f;
        }

        mixer_track_filter_init(&g_track_filters[t], MIXER_FILTER_SAMPLE_RATE_DEFAULT);

        if(t < MAX_TRACKS)
            track_set_gain(t, 1.0f);
    }

    for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
        g_send_fx_slot[s] = -1;

    mixer_external_inputs_clear();
}

/**
 * @brief Définit le gain master global.
 *
 * @param gain Gain linéaire master.
 */
/**
 * @brief Point d'entrée mixer_set_master.
 *
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_master.
 *
 * @param gain Paramètre d'entrée de l'API.
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
 * Rôle:
 * - Exécuter le traitement associé à mixer_get_master.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
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
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_track_gain.
 *
 * @param track_id Paramètre d'entrée de l'API.
 * @param gain Paramètre d'entrée de l'API.
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
    if(track_id < MAX_TRACKS)
        track_set_gain(track_id, gain);
}

/**
 * @brief Point d'entrée mixer_get_track_gain.
 *
 * Rôle:
 * - Exécuter le traitement associé à mixer_get_track_gain.
 *
 * @param track_id Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
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
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_track_pan.
 *
 * @param track_id Paramètre d'entrée de l'API.
 * @param pan Paramètre d'entrée de l'API.
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
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_track_mute.
 *
 * @param track_id Paramètre d'entrée de l'API.
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
 * @brief Point d'entrée mixer_set_track_route.
 *
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_track_route.
 *
 * @param track_id Paramètre d'entrée de l'API.
 * @param route Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_track_route(uint32_t track_id, mixer_route_t route)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_tracks[track_id].route_master = ((route & MIXER_ROUTE_MASTER) != 0U) ? 1U : 0U;
    g_tracks[track_id].route_cue = ((route & MIXER_ROUTE_CUE) != 0U) ? 1U : 0U;
}

/**
 * @brief Point d'entrée mixer_set_track_insert_slot.
 *
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_track_insert_slot.
 *
 * @param track_id Paramètre d'entrée de l'API.
 * @param insert_idx Paramètre d'entrée de l'API.
 * @param slot Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_track_insert_slot(uint32_t track_id, uint32_t insert_idx, int8_t slot)
{
    if(track_id >= MIXER_MAX_TRACKS || insert_idx >= MIXER_INSERTS_PER_TRACK)
        return;

    g_tracks[track_id].insert_slot[insert_idx] = slot;
}

/**
 * @brief Point d'entrée mixer_set_track_send_level.
 *
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_track_send_level.
 *
 * @param track_id Paramètre d'entrée de l'API.
 * @param send_idx Paramètre d'entrée de l'API.
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
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_send_fx_slot.
 *
 * @param send_idx Paramètre d'entrée de l'API.
 * @param slot Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_send_fx_slot(uint32_t send_idx, int8_t slot)
{
    if(send_idx >= MIXER_NUM_SENDS)
        return;

    g_send_fx_slot[send_idx] = slot;
}

void mixer_set_reverb_wet(float wet)
{
    g_reverb.wet = clamp01(wet);
    fx_reverb_global_set_wet(g_reverb.wet);
}

void mixer_set_reverb_size(float size)
{
    g_reverb.size = clamp01(size);
    fx_reverb_global_set_size(g_reverb.size);
}

void mixer_set_reverb_decay(float decay)
{
    g_reverb.decay = clamp01(decay);
    fx_reverb_global_set_decay(g_reverb.decay);
}

void mixer_set_reverb_pre_delay(float pre_delay)
{
    g_reverb.pre_delay = clamp01(pre_delay);
    fx_reverb_global_set_predelay(mixer_reverb_predelay_ui_to_seconds(g_reverb.pre_delay));
}

void mixer_set_reverb_surround(float surround)
{
    g_reverb.surround = clamp01(surround);
    fx_reverb_global_set_surround(mixer_reverb_surround_ui_to_seconds(g_reverb.surround));
}

void mixer_set_reverb_type(uint8_t type)
{
    g_reverb.type = (type == (uint8_t)FX_REVERB_GLOBAL_TYPE_STEREO) ? FX_REVERB_GLOBAL_TYPE_STEREO : FX_REVERB_GLOBAL_TYPE_MONO;
    fx_reverb_global_set_type(g_reverb.type);
}

void mixer_set_track_filter_type(uint32_t track_id, mixer_track_filter_type_t type)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    if(type > MIXER_TRACK_FILTER_BP_BI)
        type = MIXER_TRACK_FILTER_BP_BI;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    if(filter->type == (uint8_t)type)
    {
        return;
    }

    filter->type = (uint8_t)type;
    mixer_track_filter_apply_core_params(filter);
}

void mixer_set_track_filter_cutoff(uint32_t track_id, float cutoff_hz)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->cutoff_target_hz = clampf_local(cutoff_hz, MIXER_FILTER_CUTOFF_MIN_HZ, MIXER_FILTER_CUTOFF_MAX_HZ);
}

void mixer_set_track_filter_resonance(uint32_t track_id, float resonance)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->resonance_target = clampf_local(resonance, 0.0f, 1.0f);
}

void mixer_set_track_filter_eg_amount(uint32_t track_id, float eg_amount)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_track_filters[track_id].eg_amount = clampf_local(eg_amount, -1.0f, 1.0f);
}

void mixer_set_track_filter_attack(uint32_t track_id, float attack_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    env_adsr_set_attack(&g_track_filters[track_id].filter_env,
                        mixer_track_filter_time_s_to_peaks(attack_s, g_track_filters[track_id].sample_rate));
}

void mixer_set_track_filter_decay(uint32_t track_id, float decay_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    env_adsr_set_decay(&g_track_filters[track_id].filter_env,
                       mixer_track_filter_time_s_to_peaks(decay_s, g_track_filters[track_id].sample_rate));
}

void mixer_set_track_filter_sustain(uint32_t track_id, float sustain)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    env_adsr_set_sustain(&g_track_filters[track_id].filter_env,
                         mixer_track_filter_sustain_to_peaks(sustain));
}

void mixer_set_track_filter_release(uint32_t track_id, float release_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    env_adsr_set_release(&g_track_filters[track_id].filter_env,
                         mixer_track_filter_time_s_to_peaks(release_s, g_track_filters[track_id].sample_rate));
}

void mixer_set_track_filter_keytrack(uint32_t track_id, float amount)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_track_filters[track_id].keytrack = clampf_local(amount, 0.0f, 1.0f);
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

void mixer_set_track_filter_eq_low(uint32_t track_id, float gain_db)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->eq_low_target_db = gain_db;
}

void mixer_set_track_filter_eq_mid(uint32_t track_id, float gain_db)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->eq_mid_target_db = gain_db;
}

void mixer_set_track_filter_eq_high(uint32_t track_id, float gain_db)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->eq_high_target_db = gain_db;
}

void mixer_track_filter_note_on(uint32_t track_id, uint8_t midi_note, uint8_t velocity)
{
    (void)velocity;

    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->current_note = midi_note;
    filter->note_active = 1U;
    env_adsr_retrigger(&filter->filter_env, true);
}

void mixer_set_track_vca_attack(uint32_t track_id, float attack_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    adsr_daisy_c_set_attack(&g_track_filters[track_id].vca_env, mixer_track_vca_clamp_time_s(attack_s));
}

void mixer_set_track_vca_decay(uint32_t track_id, float decay_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    adsr_daisy_c_set_decay(&g_track_filters[track_id].vca_env, mixer_track_vca_clamp_time_s(decay_s));
}

void mixer_set_track_vca_sustain(uint32_t track_id, float sustain)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    adsr_daisy_c_set_sustain(&g_track_filters[track_id].vca_env, clampf_local(sustain, 0.0f, 1.0f));
}

void mixer_set_track_vca_release(uint32_t track_id, float release_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    adsr_daisy_c_set_release(&g_track_filters[track_id].vca_env, mixer_track_vca_clamp_time_s(release_s));
}

void mixer_set_track_vca_enabled(uint32_t track_id, uint8_t enabled)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_track_filters[track_id].vca_enabled = (enabled != 0U) ? 1U : 0U;
    if (enabled == 0U)
    {
        g_track_filters[track_id].vca_note_active = 0U;
        g_track_filters[track_id].vca_note_count = 0U;
        g_track_filters[track_id].vca_current_note = MIXER_FILTER_NOTE_REF_MIDI;
        g_track_filters[track_id].vca_gate = 0U;
        adsr_daisy_c_reset(&g_track_filters[track_id].vca_env);
    }
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
        adsr_daisy_c_retrigger(&filter->vca_env, 1U);
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
    adsr_daisy_c_reset(&filter->vca_env);
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
    env_adsr_reset(&filter->filter_env);
}

void __attribute__((used)) mixer_external_inputs_clear(void)
{
    memset(g_external_track_enabled, 0, sizeof(g_external_track_enabled));
}

void __attribute__((used)) mixer_submit_external_mono(uint32_t track_id, const float *mono, uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS) || (mono == NULL))
    {
        return;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        g_external_track_l[track_id][i] = mono[i];
        g_external_track_r[track_id][i] = mono[i];
    }

    g_external_track_enabled[track_id] = 1U;
}

/**
 * @brief Traite un bloc de mixage final MAIN/CUE.
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
 * Rôle:
 * - Exécuter le traitement associé à mixer_process.
 *
 * @param tracks Paramètre d'entrée de l'API.
 * @param track_count Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_process(StereoTrack *tracks, uint32_t track_count, uint32_t frames)
{
    AUDIO_HOT ALIGN32 static float bus_main_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_main_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_cue_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_cue_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float send_l[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float send_r[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float reverb_return_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float reverb_return_r[AUDIO_BLOCK_SIZE];

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    brick6_master_buffer_begin_block(frames);

    uint8_t send_fx_active = 0U;
    for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
    {
        if(g_send_fx_slot[s] >= 0)
        {
            send_fx_active = 1U;
            break;
        }
    }
    const uint8_t reverb_active = fx_reverb_global_is_active();
    const uint8_t send_bus_active = ((send_fx_active != 0U) || (reverb_active != 0U)) ? 1U : 0U;

    memset(bus_main_l, 0, sizeof(bus_main_l));
    memset(bus_main_r, 0, sizeof(bus_main_r));
    memset(bus_cue_l, 0, sizeof(bus_cue_l));
    memset(bus_cue_r, 0, sizeof(bus_cue_r));
    if(send_bus_active != 0U)
    {
        memset(send_l, 0, sizeof(send_l));
        memset(send_r, 0, sizeof(send_r));
    }

    const uint32_t ntracks = (track_count < MIXER_MAX_TRACKS) ? track_count : MIXER_MAX_TRACKS;

    for(uint32_t t = 0; t < MIXER_MAX_TRACKS; t++)
    {
        mixer_track_t *mt = &g_tracks[t];
        const uint8_t hw_enabled = (t < ntracks) ? tracks[t].enabled : 0U;
        const uint8_t ext_enabled = g_external_track_enabled[t];
        if (((hw_enabled == 0U) && (ext_enabled == 0U)) || mt->mute)
            continue;

        float *L = NULL;
        float *R = NULL;
        if (hw_enabled != 0U)
        {
            L = tracks[t].L;
            R = tracks[t].R;
        }
        else
        {
            L = g_external_track_l[t];
            R = g_external_track_r[t];
        }

        if ((hw_enabled != 0U) && (ext_enabled != 0U))
        {
            for (uint32_t i = 0U; i < frames; ++i)
            {
                L[i] += g_external_track_l[t][i];
                R[i] += g_external_track_r[t][i];
            }
        }

        for(uint32_t i = 0; i < MIXER_INSERTS_PER_TRACK; i++)
        {
            const int8_t slot = mt->insert_slot[i];
            if(slot >= 0)
                fx_chain_process_slot_for_track(t, (uint32_t)slot, L, R, frames);
        }

        mixer_track_filter_process_block(&g_track_filters[t], L, R, frames);

        sd_recorder_capture_tap_block(SD_RECORDER_TAP_TRACK_POST_INSERT,
                                      t,
                                      L,
                                      R,
                                      frames);

        {
            float gain_cur = mt->gain_current;
            float pan_cur = mt->pan_current;
            const float inv_frames = (frames > 0U) ? (1.0f / (float)frames) : 0.0f;
            const float gain_step = (mt->gain - gain_cur) * inv_frames;
            const float pan_step = (mt->pan - pan_cur) * inv_frames;

            for(uint32_t i = 0; i < frames; i++)
            {
                /* Standard user convention: pan<0 => left, pan>0 => right.
                 * Runtime output stage wiring is mirrored, so mixer pan is compensated here. */
                const float pan_for_mix = -pan_cur;
                const float pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
                const float pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
                const float gain_l = gain_cur * pan_l;
                const float gain_r = gain_cur * pan_r;
                const float vca_gain = (g_track_filters[t].vca_enabled != 0U)
                        ? adsr_daisy_c_process(&g_track_filters[t].vca_env, g_track_filters[t].vca_gate)
                        : 1.0f;
                L[i] *= (gain_l * vca_gain);
                R[i] *= (gain_r * vca_gain);

                gain_cur += gain_step;
                pan_cur += pan_step;
            }

            mt->gain_current = mt->gain;
            mt->pan_current = mt->pan;
        }

        sd_recorder_capture_tap_block(SD_RECORDER_TAP_TRACK_POST_FADER,
                                      t,
                                      L,
                                      R,
                                      frames);
        uint8_t source_track = (uint8_t)t;
        if (track_runtime_get_logical_track_for_mix_track((uint8_t)t, &source_track) == 0U)
        {
            source_track = (uint8_t)t;
        }
        brick6_master_buffer_submit_track_post_fader(source_track, L, R, frames);

        if(send_bus_active != 0U)
        {
            float send_cur[MIXER_NUM_SENDS];
            float send_step[MIXER_NUM_SENDS];
            for(uint32_t s = 0U; s < MIXER_NUM_SENDS; ++s)
            {
                send_cur[s] = mt->send_level_current[s];
                send_step[s] = (mt->send_level[s] - send_cur[s]) * ((frames > 0U) ? (1.0f / (float)frames) : 0.0f);
            }

            for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
            {
                const uint8_t send_enabled = ((g_send_fx_slot[s] >= 0)
                        || ((reverb_active != 0U) && (s == MIXER_REVERB_SEND_INDEX))) ? 1U : 0U;
                if(send_enabled != 0U)
                {
                    if((send_cur[s] <= 0.0f) && (mt->send_level[s] <= 0.0f))
                        continue;

                    for(uint32_t i = 0; i < frames; i++)
                    {
                        send_l[s][i] += L[i] * send_cur[s];
                        send_r[s][i] += R[i] * send_cur[s];
                        send_cur[s] += send_step[s];
                    }
                }

                mt->send_level_current[s] = mt->send_level[s];
            }
        }

        sd_recorder_capture_tap_block(SD_RECORDER_TAP_TRACK_POST_SEND,
                                      t,
                                      L,
                                      R,
                                      frames);

        if(mt->route_master && mt->route_cue)
        {
            for(uint32_t i = 0; i < frames; i++)
            {
                bus_main_l[i] += L[i];
                bus_main_r[i] += R[i];
                bus_cue_l[i] += L[i];
                bus_cue_r[i] += R[i];
            }
        }
        else if(mt->route_master)
        {
            for(uint32_t i = 0; i < frames; i++)
            {
                bus_main_l[i] += L[i];
                bus_main_r[i] += R[i];
            }
        }
        else if(mt->route_cue)
        {
            for(uint32_t i = 0; i < frames; i++)
            {
                bus_cue_l[i] += L[i];
                bus_cue_r[i] += R[i];
            }
        }
    }

    brick6_master_buffer_commit_block(frames);
    mixer_external_inputs_clear();

    if(send_bus_active != 0U)
    {
        if(reverb_active != 0U)
        {
            fx_reverb_global_process_block(send_l[MIXER_REVERB_SEND_INDEX],
                                           send_r[MIXER_REVERB_SEND_INDEX],
                                           reverb_return_l,
                                           reverb_return_r,
                                           frames);
            for(uint32_t i = 0; i < frames; i++)
            {
                bus_main_l[i] += reverb_return_l[i];
                bus_main_r[i] += reverb_return_r[i];
            }
        }

        for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
        {
            const int8_t slot = g_send_fx_slot[s];
            if(slot >= 0)
            {
                fx_chain_process_slot((uint32_t)slot, send_l[s], send_r[s], frames);
                for(uint32_t i = 0; i < frames; i++)
                {
                    bus_main_l[i] += send_l[s][i];
                    bus_main_r[i] += send_r[s][i];
                }
            }
        }
    }

    if(track_count > 0U)
    {
        memcpy(tracks[0].L, bus_main_l, sizeof(float) * frames);
        memcpy(tracks[0].R, bus_main_r, sizeof(float) * frames);
    }

    if(track_count > 1U)
    {
        memcpy(tracks[1].L, bus_cue_l, sizeof(float) * frames);
        memcpy(tracks[1].R, bus_cue_r, sizeof(float) * frames);
    }
}
