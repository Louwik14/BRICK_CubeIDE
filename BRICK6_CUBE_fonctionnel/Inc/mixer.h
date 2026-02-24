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

#define MIXER_MAX_TRACKS 4U
#define MIXER_NUM_SENDS 2U
#define MIXER_INSERTS_PER_TRACK 2U

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

void mixer_process(StereoTrack *tracks,
                   uint32_t track_count,
                   uint32_t frames);
