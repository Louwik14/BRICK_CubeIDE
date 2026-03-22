#pragma once

#include <stdint.h>
#include "audio_float.h"

/**
 * @file mixer.h
 * @brief Interface du moteur de mixage final (tracks/inserts/sends/routing).
 *
 * Rôle du module:
 * - Exposer la configuration runtime du mixer.
 * - Exécuter le mix final MAIN/CUE par bloc audio.
 *
 * Architecture:
 * - Appelé par: brick6_app_init.c, control_router.c, callback DSP principal.
 * - Appelle: fx_chain/fx_pool via implémentation mixer.c.
 *
 * Contraintes temps réel:
 * - mixer_process(): IRQ audio, hard realtime.
 * - malloc: interdit.
 */

/*
 * Mixer runtime is prepared for up to 8 tracks so per-track processors such as
 * the SVF can own an independent state per track. The current audio path still
 * exposes 4 DSP tracks (`MAX_TRACKS` in audio_float.h), but the storage is
 * deliberately sized for future expansion.
 */
#define MIXER_MAX_TRACKS 8U
#define MIXER_NUM_SENDS 2U
#define MIXER_INSERTS_PER_TRACK 2U

typedef enum
{
    MIXER_TRACK_FILTER_OFF = 0,
    MIXER_TRACK_FILTER_LP,
    MIXER_TRACK_FILTER_HP,
    MIXER_TRACK_FILTER_BP
} mixer_track_filter_type_t;

typedef enum
{
    MIXER_ROUTE_NONE = 0,
    MIXER_ROUTE_MASTER = 1,
    MIXER_ROUTE_CUE = 2,
    MIXER_ROUTE_BOTH = 3
} mixer_route_t;

void mixer_init(void);
void mixer_set_master(float gain);
float mixer_get_master(void);

void mixer_set_track_gain(uint32_t track_id, float gain);
float mixer_get_track_gain(uint32_t track_id);

void mixer_set_track_pan(uint32_t track_id, float pan);
void mixer_set_track_mute(uint32_t track_id, uint8_t mute);
void mixer_set_track_route(uint32_t track_id, mixer_route_t route);
void mixer_set_track_insert_slot(uint32_t track_id, uint32_t insert_idx, int8_t slot);
void mixer_set_track_send_level(uint32_t track_id, uint32_t send_idx, float level);
void mixer_set_send_fx_slot(uint32_t send_idx, int8_t slot);
void mixer_set_track_filter_type(uint32_t track_id, mixer_track_filter_type_t type);
void mixer_set_track_filter_cutoff(uint32_t track_id, float cutoff_hz);
void mixer_set_track_filter_resonance(uint32_t track_id, float resonance);
void mixer_set_track_filter_eg_amount(uint32_t track_id, float eg_amount);
void mixer_set_track_filter_attack(uint32_t track_id, float attack_s);
void mixer_set_track_filter_decay(uint32_t track_id, float decay_s);
void mixer_set_track_filter_sustain(uint32_t track_id, float sustain);
void mixer_set_track_filter_release(uint32_t track_id, float release_s);

void mixer_process(StereoTrack *tracks,
                   uint32_t track_count,
                   uint32_t frames);
