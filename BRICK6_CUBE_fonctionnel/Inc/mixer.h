#pragma once

#include <stdint.h>
#include "audio_float.h"

/**
 * @file mixer.h
 * @brief Interface du module mixer (étage de routage, sans DSP lourd).
 *
 * Rôle du module:
 * - Fournir une API de haut niveau pour la partie "mixer" applicative.
 * - Dans l'état actuel: routage minimal, sans modification d'échantillons.
 *
 * Architecture:
 * - Appelé par: brick6_app_init.c, app_controls.c, callback DSP principal.
 * - Délègue le gain master à audio_float.c pour centraliser le gain runtime.
 */

/**
 * @brief Initialise le module mixer.
 *
 * Contexte d'appel:
 * - Main loop (init système).
 */
void mixer_init(void);

/**
 * @brief Configure le gain master (gain linéaire).
 *
 * @param gain Gain attendu en linéaire (clamp effectué côté audio_float).
 */
void mixer_set_master(float gain);

/**
 * @brief Lit le gain master courant.
 *
 * @return Gain master linéaire.
 */
float mixer_get_master(void);

/**
 * @brief Étape de mix/routage sur les tracks du bloc courant.
 *
 * @param tracks Tableau de tracks stéréo.
 * @param track_count Nombre de tracks valides.
 * @param frames Taille du bloc en frames.
 *
 * Contexte d'appel:
 * - IRQ audio (appelé depuis callback DSP).
 *
 * @note Dans l'implémentation actuelle: routage-only, pas de gain et pas de
 *       modification d'échantillons.
 */
void mixer_process(StereoTrack *tracks,
                   uint32_t track_count,
                   uint32_t frames);
