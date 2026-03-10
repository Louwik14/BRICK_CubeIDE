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

typedef struct {
    float gain;
    float pan;
    uint8_t mute;

    uint8_t route_master;
    uint8_t route_cue;

    int8_t insert_slot[MIXER_INSERTS_PER_TRACK];
    float send_level[MIXER_NUM_SENDS];
} mixer_track_t;

static mixer_track_t g_tracks[MIXER_MAX_TRACKS];
static int8_t g_send_fx_slot[MIXER_NUM_SENDS];

static float clamp01(float v)
{
    if(v < 0.0f) return 0.0f;
    if(v > 1.0f) return 1.0f;
    return v;
}

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
void mixer_set_master(float gain)
{
    audio_float_set_master_gain(gain);
}

/**
 * @brief Lit le gain master courant.
 *
 * @return Gain master linéaire.
 */
float mixer_get_master(void)
{
    return audio_float_get_master_gain();
}

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

float mixer_get_track_gain(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return 0.0f;

    return g_tracks[track_id].gain;
}

void mixer_set_track_pan(uint32_t track_id, float pan)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_tracks[track_id].pan = clamp_pan(pan);
}

void mixer_set_track_mute(uint32_t track_id, uint8_t mute)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_tracks[track_id].mute = mute ? 1U : 0U;
}

void mixer_set_track_route(uint32_t track_id, mixer_route_t route)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_tracks[track_id].route_master = ((route & MIXER_ROUTE_MASTER) != 0U) ? 1U : 0U;
    g_tracks[track_id].route_cue = ((route & MIXER_ROUTE_CUE) != 0U) ? 1U : 0U;
}

void mixer_set_track_insert_slot(uint32_t track_id, uint32_t insert_idx, int8_t slot)
{
    if(track_id >= MIXER_MAX_TRACKS || insert_idx >= MIXER_INSERTS_PER_TRACK)
        return;

    g_tracks[track_id].insert_slot[insert_idx] = slot;
}

void mixer_set_track_send_level(uint32_t track_id, uint32_t send_idx, float level)
{
    if(track_id >= MIXER_MAX_TRACKS || send_idx >= MIXER_NUM_SENDS)
        return;

    g_tracks[track_id].send_level[send_idx] = clamp01(level);
}

void mixer_set_send_fx_slot(uint32_t send_idx, int8_t slot)
{
    if(send_idx >= MIXER_NUM_SENDS)
        return;

    g_send_fx_slot[send_idx] = slot;
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
void mixer_process(StereoTrack *tracks, uint32_t track_count, uint32_t frames)
{
    static float bus_main_l[AUDIO_BLOCK_SIZE];
    static float bus_main_r[AUDIO_BLOCK_SIZE];
    static float bus_cue_l[AUDIO_BLOCK_SIZE];
    static float bus_cue_r[AUDIO_BLOCK_SIZE];
    static float send_l[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE];
    static float send_r[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE];

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
