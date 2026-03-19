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
    drv_display_draw_text(0U, 0U, "BRICK6 MAIN");
    drv_display_draw_text(0U, 16U, "BTN1: PARAM TEST");
    drv_display_draw_text(0U, 32U, "BTN2: MAIN PAGE");
    drv_display_draw_text(0U, 48U, "BTN3: HALL DEBUG");
}

const ui_page_t g_ui_page_main = {
    .enter = ui_page_main_enter,
    .leave = ui_page_main_leave,
    .handle_event = ui_page_main_handle_event,
    .tick = ui_page_main_tick,
    .render = ui_page_main_render,
};
