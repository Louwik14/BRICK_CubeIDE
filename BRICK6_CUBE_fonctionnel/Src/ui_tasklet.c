/**
 * @file ui_tasklet.c
 * @brief Tasklet UI (boutons/écran) — stub.
 *
 * Ce module sert de point d'entrée pour la logique UI future et
 * clarifie la structure de la boucle principale.
 *
 * Rôle dans le système:
 * - Emplacement dédié au traitement UI hors IRQ.
 * - Maintient la séparation des responsabilités dans la main loop.
 *
 * Contraintes temps réel:
 * - Critique audio: non.
 * - Tasklet: oui (boucle principale).
 * - IRQ: non.
 * - Borné: oui (traitement court attendu).
 *
 * Architecture:
 * - Appelé par: main loop (ui_tasklet_poll).
 * - Appelle: aucun module pour l'instant.
 *
 * Règles:
 * - Pas de malloc.
 * - Ne pas bloquer la boucle principale.
 *
 * @note L’API publique est déclarée dans ui_tasklet.h.
 */

#include "ui_tasklet.h"
#include "brick6_refactor.h"

void ui_tasklet_poll(void)
{
#if BRICK6_REFACTOR_STEP_7
  (void)0;
#endif
}
