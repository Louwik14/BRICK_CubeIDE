/**
 * @file ui_page_main.c
 * @brief Module applicatif ui_page_main.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à ui_page_main.
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

#include "pages/ui_page_main.h"

#include <stdio.h>

#include "cpu_load.h"
#include "drv_display.h"
#include "ui_param.h"

/**
 * @brief Point d'entrée ui_page_main_enter.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_main_enter.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_page_main_enter(void)
{
    ui_param_set_bank(0);
}

/**
 * @brief Point d'entrée ui_page_main_leave.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_main_leave.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_page_main_leave(void)
{
}

/**
 * @brief Point d'entrée ui_page_main_handle_event.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_main_handle_event.
 *
 * @param ev Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_page_main_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

/**
 * @brief Point d'entrée ui_page_main_tick.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_main_tick.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_page_main_tick(void)
{
}

/**
 * @brief Point d'entrée ui_page_main_render.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_main_render.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_page_main_render(void)
{
    char line0[24];
    char line1[24];
    char line2[24];
    cpu_load_metrics_t cpu_metrics;

    cpu_load_get_metrics(&cpu_metrics);

    drv_display_draw_text(0U, 0U, "BRICK6 MAIN");

    if (cpu_metrics.counter_valid != 0U)
    {
        (void)snprintf(
            line0,
            sizeof(line0),
            "LAST %lu.%01lu%%",
            (unsigned long)(cpu_metrics.last_permille / 10U),
            (unsigned long)(cpu_metrics.last_permille % 10U));

        (void)snprintf(
            line1,
            sizeof(line1),
            "AVG  %lu.%01lu%%",
            (unsigned long)(cpu_metrics.avg_permille / 10U),
            (unsigned long)(cpu_metrics.avg_permille % 10U));

        (void)snprintf(
            line2,
            sizeof(line2),
            "PEAK %lu.%01lu%%",
            (unsigned long)(cpu_metrics.peak_permille / 10U),
            (unsigned long)(cpu_metrics.peak_permille % 10U));
    }
    else
    {
        (void)snprintf(line0, sizeof(line0), "LAST N/A");
        (void)snprintf(line1, sizeof(line1), "AVG  N/A");
        (void)snprintf(line2, sizeof(line2), "PEAK N/A");
    }

    drv_display_draw_text(0U, 14U, line0);
    drv_display_draw_text(0U, 24U, line1);
    drv_display_draw_text(0U, 34U, line2);
    drv_display_draw_text(0U, 48U, "BTN1:DX7 BTN3:HALL");
    drv_display_draw_text(0U, 58U, "BTN5:CAL");
}

const ui_page_t g_ui_page_main = {
    .enter = ui_page_main_enter,
    .leave = ui_page_main_leave,
    .handle_event = ui_page_main_handle_event,
    .tick = ui_page_main_tick,
    .render = ui_page_main_render,
};
