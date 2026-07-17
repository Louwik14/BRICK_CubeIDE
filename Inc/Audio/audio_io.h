#pragma once

#include <stdint.h>
#include "audio_float.h"

/**
 * @file audio_io.h
 * @brief API de conversion audio stéréo int24 <-> tracks float.
 *
 * Rôle du module:
 * - Dépaqueter l'entrée ligne stéréo RX dans les tracks float.
 * - Repaqueter MAIN float vers TX stéréo.
 *
 * Architecture:
 * - Appelé par: audio_float.c.
 * - Appelle: aucun module externe.
 *
 * Contraintes temps réel:
 * - Exécuté en IRQ audio.
 * - Hard realtime, sans malloc.
 */

/**
 * @brief Dépaquette RX stéréo vers buffers de tracks stéréo.
 *
 * @param rx Buffer RX int32 (int24 packed).
 * @param track_buf Tableau des tracks destination.
 * @param frames Nombre de frames à traiter.
 * @param in_scale Gain d'échelle d'entrée.
 */
void audio_io_unpack(const int32_t *AUDIO_RESTRICT rx,
                     StereoTrack *AUDIO_RESTRICT track_buf,
                     uint32_t frames,
                     float in_scale);

/**
 * @brief Repaquette MAIN float vers buffer TX stéréo.
 *
 * @param tx Buffer TX int32 destination.
 * @param bus_main_l Bus MAIN gauche.
 * @param bus_main_r Bus MAIN droit.
 * @param frames Nombre de frames.
 * @param out_gain Gain global de sortie.
 */
void audio_io_pack(int32_t *AUDIO_RESTRICT tx,
                   const float *AUDIO_RESTRICT bus_main_l,
                   const float *AUDIO_RESTRICT bus_main_r,
                   uint32_t frames,
                   float out_gain);

/**
 * @brief Repaquette MAIN float vers buffer TX stéréo avec gain rampé.
 *
 * @param tx Buffer TX int32 destination.
 * @param bus_main_l Bus MAIN gauche.
 * @param bus_main_r Bus MAIN droit.
 * @param frames Nombre de frames.
 * @param out_gain_start Gain global de sortie en début de bloc.
 * @param out_gain_end Gain global de sortie en fin de bloc.
 */
void audio_io_pack_ramped(int32_t *AUDIO_RESTRICT tx,
                          const float *AUDIO_RESTRICT bus_main_l,
                          const float *AUDIO_RESTRICT bus_main_r,
                          uint32_t frames,
                          float out_gain_start,
                          float out_gain_end);
