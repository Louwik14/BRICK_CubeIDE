/**
 * @file ui_tasklet.c
 * @brief Module applicatif ui_tasklet.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à ui_tasklet.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "ui_tasklet.h"

#include <stdint.h>

#include "drv_display.h"
#include "ui_core.h"

/**
 * @brief Point d'entrée ui_tasklet_poll.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_tasklet_poll.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static uint8_t g_ui_tasklet_init = 0U;

void ui_tasklet_poll(void)
{
    if (g_ui_tasklet_init == 0U)
    {
        g_ui_tasklet_init = 1U;
        drv_display_init();
        ui_core_init();
    }

    ui_core_tick();
}

uint8_t ui_tasklet_is_initialized(void)
{
    return g_ui_tasklet_init;
}
