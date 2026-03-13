#pragma once

#include <stdint.h>
#include "audio_float.h"

/**
 * @file audio_io.h
 * @brief API de conversion audio 2xTDM4 int24 <-> tracks float.
 *
 * Rôle du module:
 * - Dépaqueter les slots TDM4 RX SAI1/SAI2 dans les tracks float.
 * - Repaqueter les 4 tracks vers TX TDM4 SAI1/SAI2.
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
 * @brief Dépaquette RX TDM4 SAI1/SAI2 vers buffers de tracks stéréo.
 *
 * @param rx_sai1 Buffer RX SAI1 int32 (int24 packed).
 * @param rx_sai2 Buffer RX SAI2 int32 (int24 packed).
 * @param track_buf Tableau des tracks destination.
 * @param frames Nombre de frames à traiter.
 * @param in_scale Gain d'échelle d'entrée.
 */
void audio_io_unpack(const int32_t *AUDIO_RESTRICT rx_sai1,
                     const int32_t *AUDIO_RESTRICT rx_sai2,
                     StereoTrack *AUDIO_RESTRICT track_buf,
                     uint32_t frames,
                     float in_scale);

/**
 * @brief Repaquette 4 tracks float vers buffers TX TDM4 SAI1/SAI2.
 *
 * @param tx_sai1 Buffer TX SAI1 int32 destination.
 * @param tx_sai2 Buffer TX SAI2 int32 destination.
 * @param tracks Tableau logique des 4 tracks stéréo.
 * @param frames Nombre de frames.
 * @param out_gain Gain global de sortie.
 */
void audio_io_pack(int32_t *AUDIO_RESTRICT tx_sai1,
                   int32_t *AUDIO_RESTRICT tx_sai2,
                   const StereoTrack *AUDIO_RESTRICT tracks,
                   uint32_t frames,
                   float out_gain);
