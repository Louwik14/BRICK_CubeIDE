#pragma once

#include <stdint.h>
#include "audio_float.h"

/**
 * @file dsp_engine.h
 * @brief Interface du pont DSP applicatif block-based.
 *
 * Rôle du module:
 * - Enregistrer le callback DSP utilisateur.
 * - Exécuter ce callback sur les blocs audio entrants.
 *
 * Architecture:
 * - Appelé par: audio_float.c.
 * - Appelle: callback applicatif (my_dsp).
 *
 * Contraintes temps réel:
 * - dsp_engine_process_block(): IRQ/hard realtime.
 * - malloc: interdit.
 */

/**
 * @brief Configure le callback DSP principal.
 *
 * @param cb Callback block-based (peut être NULL).
 */
void dsp_engine_set_callback(audio_dsp_cb cb);

/**
 * @brief Traite un bloc via le callback DSP enregistré.
 *
 * @param tracks Tableau des tracks stéréo.
 * @param track_count Nombre de tracks valides.
 * @param frames Taille bloc en frames.
 */
void dsp_engine_process_block(StereoTrack *tracks,
                              uint32_t track_count,
                              uint32_t frames);
