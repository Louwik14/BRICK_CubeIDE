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
#include "drv_display.h"
#include "app_controls.h"

void ui_tasklet_poll(void)
{
    static uint8_t init = 0;
    static uint32_t div = 0;

    if (!init)
    {
        init = 1;

        drv_display_init();
        app_controls_init();
    }

    app_controls_process();

    div++;
    if (div >= 25)
    {
        div = 0;
        app_controls_render();
    }
}
