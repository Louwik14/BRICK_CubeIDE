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
volatile ui_tasklet_metrics_t g_ui_tasklet_metrics;

void ui_tasklet_poll(void)
{
    const uint32_t start_cycles = DWT->CYCCNT;

    if (g_ui_tasklet_init == 0U)
    {
        g_ui_tasklet_init = 1U;
        g_ui_tasklet_metrics.lazy_init_count++;
        drv_display_init();
    }

    ui_core_tick();

    {
        const uint32_t elapsed_cycles = DWT->CYCCNT - start_cycles;

        g_ui_tasklet_metrics.calls++;
        g_ui_tasklet_metrics.last_cycles = elapsed_cycles;
        if (elapsed_cycles > g_ui_tasklet_metrics.max_cycles)
        {
            g_ui_tasklet_metrics.max_cycles = elapsed_cycles;
        }
    }
}

uint8_t ui_tasklet_is_initialized(void)
{
    return g_ui_tasklet_init;
}
