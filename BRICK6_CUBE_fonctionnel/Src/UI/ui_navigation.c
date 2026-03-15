/**
 * @file ui_navigation.c
 * @brief Module applicatif ui_navigation.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à ui_navigation.
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

#include "ui_navigation.h"

#include "ui_page_manager.h"

/*
 * Data-driven navigation table.
 * To add a new workflow, add/edit rules here without changing the engine logic.
 */
static const ui_nav_rule_t g_ui_nav_rules[] = {
    { BTN_PARAM_1, UI_NAV_ANY_PAGE, UI_PAGE_PARAM_TEST },
    { BTN_PARAM_2, UI_NAV_ANY_PAGE, UI_PAGE_MAIN },
    { BTN_PARAM_3, UI_NAV_ANY_PAGE, UI_PAGE_HALL_DEBUG },
};

/**
 * @brief Point d'entrée ui_navigation_handle_event.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_navigation_handle_event.
 *
 * @param event Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_navigation_handle_event(const ui_event_t *event)
{
    if ((event == 0) || (event->type != UI_EVENT_BUTTON_PRESS))
    {
        return;
    }

    const uint8_t current_page = ui_page_get_id();

    for (uint8_t i = 0U; i < (uint8_t)(sizeof(g_ui_nav_rules) / sizeof(g_ui_nav_rules[0])); i++)
    {
        const ui_nav_rule_t *rule = &g_ui_nav_rules[i];

        if ((event->id == (uint8_t)rule->button)
                && ((rule->required_page == UI_NAV_ANY_PAGE) || (rule->required_page == current_page)))
        {
            ui_page_set(rule->target_page);
            break;
        }
    }
}
