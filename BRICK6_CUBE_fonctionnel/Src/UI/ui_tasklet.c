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

#include "ui_display.h"
#include "ui_core.h"
#include "ui_renderer_oled.h"
#include "stm32h7xx_hal.h"

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
void ui_tasklet_poll(void)
{
    static uint8_t init = 0U;

    if (init == 0U)
    {
        init = 1U;
        ui_display_init();
        ui_core_init();
    }

    ui_core_tick();

    static uint32_t last_render = 0U;
    const uint32_t now = HAL_GetTick();

    if ((now - last_render) >= 20U)
    {
        last_render = now;
        ui_renderer_oled_draw();
    }
}
