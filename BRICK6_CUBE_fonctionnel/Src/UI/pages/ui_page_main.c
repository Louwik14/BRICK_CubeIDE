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

    drv_display_draw_text(0U, 0U, "BRICK6 MAIN");

    if (cpu_load_is_valid() != 0U)
    {
        const uint32_t cpu_pm = cpu_load_get_permille();
        const uint32_t cpu_max_pm = cpu_load_get_max();

        (void)snprintf(
            line0,
            sizeof(line0),
            "CPU %lu.%01lu%%",
            (unsigned long)(cpu_pm / 10U),
            (unsigned long)(cpu_pm % 10U));

        (void)snprintf(
            line1,
            sizeof(line1),
            "MAX %lu.%01lu%%",
            (unsigned long)(cpu_max_pm / 10U),
            (unsigned long)(cpu_max_pm % 10U));
    }
    else
    {
        (void)snprintf(line0, sizeof(line0), "CPU N/A");
        (void)snprintf(line1, sizeof(line1), "MAX N/A");
    }

    drv_display_draw_text(0U, 16U, line0);
    drv_display_draw_text(0U, 28U, line1);
    drv_display_draw_text(0U, 44U, "BTN1: PARAM TEST");
    drv_display_draw_text(0U, 54U, "BTN3: HALL DEBUG");
}

const ui_page_t g_ui_page_main = {
    .enter = ui_page_main_enter,
    .leave = ui_page_main_leave,
    .handle_event = ui_page_main_handle_event,
    .tick = ui_page_main_tick,
    .render = ui_page_main_render,
};
