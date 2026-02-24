/**
 * @file dsp_engine.c
 * @brief Pont minimal entre la frontière audio_float et le callback DSP applicatif.
 *
 * Rôle du module:
 * - Stocker le callback DSP utilisateur global.
 * - Déléguer le traitement d'un bloc audio au callback enregistré.
 *
 * Architecture:
 * - Appelé par: audio_float.c (audio_dsp_process).
 * - Appelle: callback utilisateur (my_dsp dans brick6_app_init.c).
 *
 * Contraintes temps réel:
 * - IRQ: oui (dsp_engine_process_block est appelé dans le chemin audio IRQ).
 * - Hard realtime: oui.
 * - malloc: interdit.
 *
 * Notes:
 * - Aucun traitement DSP ici, uniquement du dispatch.
 */

#include "dsp_engine.h"

/** Callback DSP applicatif courant (NULL si non configuré). */
static audio_dsp_cb s_cb = 0;

/**
 * @brief Enregistre le callback DSP utilisateur.
 *
 * @param cb Fonction de traitement bloc (peut être NULL).
 *
 * Rôle:
 * - Met à jour la cible appelée par le moteur DSP à chaque bloc.
 *
 * Contexte d'appel:
 * - Init / configuration (main loop).
 *
 * Contraintes:
 * - O(1), sans allocation.
 */
void dsp_engine_set_callback(audio_dsp_cb cb)
{
    s_cb = cb;
}

/**
 * @brief Exécute le callback DSP sur le bloc courant si présent.
 *
 * @param tracks Tableau des tracks stéréo.
 * @param track_count Nombre de tracks valides.
 * @param frames Taille du bloc en frames.
 *
 * Rôle:
 * - Point d'entrée unique du DSP applicatif depuis la frontière audio.
 *
 * Contexte d'appel:
 * - IRQ audio.
 *
 * Contraintes:
 * - Hard realtime: aucune attente, aucun appel bloquant.
 */
void dsp_engine_process_block(StereoTrack *tracks,
                              uint32_t track_count,
                              uint32_t frames)
{
    if(s_cb)
        s_cb(tracks, track_count, frames);
}
