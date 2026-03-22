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

#include <string.h>

#include "fx_chain.h"
#include "sd_multitrack_recorder.h"
#include "memory_layout.h"
#include "svf.h"

typedef struct {
    float gain;
    float pan;
    uint8_t mute;

    uint8_t route_master;
    uint8_t route_cue;

    int8_t insert_slot[MIXER_INSERTS_PER_TRACK];
    float send_level[MIXER_NUM_SENDS];
} mixer_track_t;

typedef struct {
    svf_t left;
    svf_t right;
    float sample_rate;
    float cutoff_hz;
    float resonance;
    /* First pass: stored for future envelope modulation, not applied yet. */
    float eg_amount;
    float attack_s;
    float decay_s;
    float sustain;
    float release_s;
    uint8_t type;
} mixer_track_filter_t;

static mixer_track_t g_tracks[MIXER_MAX_TRACKS];
static int8_t g_send_fx_slot[MIXER_NUM_SENDS];
static mixer_track_filter_t g_track_filters[MIXER_MAX_TRACKS];

#define MIXER_FILTER_SAMPLE_RATE_DEFAULT 48000.0f
#define MIXER_FILTER_CUTOFF_MIN_HZ 20.0f
#define MIXER_FILTER_CUTOFF_MAX_HZ 16000.0f
#define MIXER_FILTER_ATTACK_MIN_S 0.001f
#define MIXER_FILTER_ATTACK_MAX_S 5.0f
#define MIXER_FILTER_DECAY_MIN_S 0.001f
#define MIXER_FILTER_DECAY_MAX_S 5.0f
#define MIXER_FILTER_RELEASE_MIN_S 0.001f
#define MIXER_FILTER_RELEASE_MAX_S 5.0f

static float clampf_local(float v, float lo, float hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static void mixer_track_filter_apply_core_params(mixer_track_filter_t *filter)
{
    if(filter == NULL)
        return;

    svf_set_freq(&filter->left, filter->cutoff_hz);
    svf_set_freq(&filter->right, filter->cutoff_hz);
    svf_set_res(&filter->left, filter->resonance);
    svf_set_res(&filter->right, filter->resonance);
    svf_set_drive(&filter->left, 0.0f);
    svf_set_drive(&filter->right, 0.0f);
}

static void mixer_track_filter_reset_dsp(mixer_track_filter_t *filter)
{
    if(filter == NULL)
        return;

    svf_init(&filter->left, filter->sample_rate);
    svf_init(&filter->right, filter->sample_rate);
    mixer_track_filter_apply_core_params(filter);
}

static void mixer_track_filter_init(mixer_track_filter_t *filter, float sample_rate)
{
    if(filter == NULL)
        return;

    filter->sample_rate = (sample_rate > 0.0f) ? sample_rate : MIXER_FILTER_SAMPLE_RATE_DEFAULT;
    filter->cutoff_hz = MIXER_FILTER_CUTOFF_MAX_HZ;
    filter->resonance = 0.0f;
    filter->eg_amount = 0.0f;
    filter->attack_s = 0.01f;
    filter->decay_s = 0.10f;
    filter->sustain = 1.0f;
    filter->release_s = 0.10f;
    filter->type = (uint8_t)MIXER_TRACK_FILTER_OFF;

    mixer_track_filter_reset_dsp(filter);
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

    switch((mixer_track_filter_type_t)filter->type)
    {
        case MIXER_TRACK_FILTER_LP:
            for(uint32_t i = 0U; i < frames; ++i)
            {
                left[i] = svf_process_mode(&filter->left, left[i], SVF_MODE_LP);
                right[i] = svf_process_mode(&filter->right, right[i], SVF_MODE_LP);
            }
            break;

        case MIXER_TRACK_FILTER_HP:
            for(uint32_t i = 0U; i < frames; ++i)
            {
                left[i] = svf_process_mode(&filter->left, left[i], SVF_MODE_HP);
                right[i] = svf_process_mode(&filter->right, right[i], SVF_MODE_HP);
            }
            break;

        case MIXER_TRACK_FILTER_BP:
            for(uint32_t i = 0U; i < frames; ++i)
            {
                left[i] = svf_process_mode(&filter->left, left[i], SVF_MODE_BP);
                right[i] = svf_process_mode(&filter->right, right[i], SVF_MODE_BP);
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
    for(uint32_t t = 0; t < MIXER_MAX_TRACKS; t++)
    {
        g_tracks[t].gain = 1.0f;
        g_tracks[t].pan = 0.0f;
        g_tracks[t].mute = 0U;

        g_tracks[t].route_master = 1U;
        g_tracks[t].route_cue = 0U;

        for(uint32_t i = 0; i < MIXER_INSERTS_PER_TRACK; i++)
            g_tracks[t].insert_slot[i] = -1;

        for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
            g_tracks[t].send_level[s] = 0.0f;

        mixer_track_filter_init(&g_track_filters[t], MIXER_FILTER_SAMPLE_RATE_DEFAULT);

        if(t < MAX_TRACKS)
            track_set_gain(t, 1.0f);
    }

    for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
        g_send_fx_slot[s] = -1;
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

void mixer_set_track_filter_type(uint32_t track_id, mixer_track_filter_type_t type)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    if(type > MIXER_TRACK_FILTER_BP)
        type = MIXER_TRACK_FILTER_BP;

    g_track_filters[track_id].type = (uint8_t)type;

    if(type != MIXER_TRACK_FILTER_OFF)
        mixer_track_filter_reset_dsp(&g_track_filters[track_id]);
}

void mixer_set_track_filter_cutoff(uint32_t track_id, float cutoff_hz)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->cutoff_hz = clampf_local(cutoff_hz, MIXER_FILTER_CUTOFF_MIN_HZ, MIXER_FILTER_CUTOFF_MAX_HZ);
    svf_set_freq(&filter->left, filter->cutoff_hz);
    svf_set_freq(&filter->right, filter->cutoff_hz);
}

void mixer_set_track_filter_resonance(uint32_t track_id, float resonance)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->resonance = clampf_local(resonance, 0.0f, 1.0f);
    svf_set_res(&filter->left, filter->resonance);
    svf_set_res(&filter->right, filter->resonance);
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

    g_track_filters[track_id].attack_s = clampf_local(attack_s,
                                                      MIXER_FILTER_ATTACK_MIN_S,
                                                      MIXER_FILTER_ATTACK_MAX_S);
}

void mixer_set_track_filter_decay(uint32_t track_id, float decay_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_track_filters[track_id].decay_s = clampf_local(decay_s,
                                                     MIXER_FILTER_DECAY_MIN_S,
                                                     MIXER_FILTER_DECAY_MAX_S);
}

void mixer_set_track_filter_sustain(uint32_t track_id, float sustain)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_track_filters[track_id].sustain = clamp01(sustain);
}

void mixer_set_track_filter_release(uint32_t track_id, float release_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_track_filters[track_id].release_s = clampf_local(release_s,
                                                       MIXER_FILTER_RELEASE_MIN_S,
                                                       MIXER_FILTER_RELEASE_MAX_S);
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

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    memset(bus_main_l, 0, sizeof(bus_main_l));
    memset(bus_main_r, 0, sizeof(bus_main_r));
    memset(bus_cue_l, 0, sizeof(bus_cue_l));
    memset(bus_cue_r, 0, sizeof(bus_cue_r));
    memset(send_l, 0, sizeof(send_l));
    memset(send_r, 0, sizeof(send_r));

    const uint32_t ntracks = (track_count < MIXER_MAX_TRACKS) ? track_count : MIXER_MAX_TRACKS;

    for(uint32_t t = 0; t < ntracks; t++)
    {
        mixer_track_t *mt = &g_tracks[t];
        StereoTrack *tr = &tracks[t];

        if((tr->enabled == 0U) || mt->mute)
            continue;

        float *L = tr->L;
        float *R = tr->R;

        mixer_track_filter_process_block(&g_track_filters[t], L, R, frames);

        for(uint32_t i = 0; i < MIXER_INSERTS_PER_TRACK; i++)
        {
            const int8_t slot = mt->insert_slot[i];
            if(slot >= 0)
                fx_chain_process_slot((uint32_t)slot, L, R, frames);
        }

        sd_recorder_capture_tap_block(SD_RECORDER_TAP_TRACK_POST_INSERT,
                                      t,
                                      L,
                                      R,
                                      frames);

        const float pan_l = (mt->pan <= 0.0f) ? 1.0f : (1.0f - mt->pan);
        const float pan_r = (mt->pan >= 0.0f) ? 1.0f : (1.0f + mt->pan);
        const float gain_l = mt->gain * pan_l;
        const float gain_r = mt->gain * pan_r;

        for(uint32_t i = 0; i < frames; i++)
        {
            L[i] *= gain_l;
            R[i] *= gain_r;
        }

        sd_recorder_capture_tap_block(SD_RECORDER_TAP_TRACK_POST_FADER,
                                      t,
                                      L,
                                      R,
                                      frames);

        for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
        {
            if(g_send_fx_slot[s] >= 0)
            {
                const float send_g = mt->send_level[s];
                for(uint32_t i = 0; i < frames; i++)
                {
                    send_l[s][i] += L[i] * send_g;
                    send_r[s][i] += R[i] * send_g;
                }
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
