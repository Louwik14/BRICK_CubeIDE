/**
 * @file ui_page_manager.c
 * @brief Module applicatif ui_page_manager.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à ui_page_manager.
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

#include "ui_page_manager.h"
#include "ui_core_mute.h"
#include "ui_param.h"

#define UI_PAGE_MANAGER_MAX_PAGES UI_PAGE_COUNT

/*
 * Page manager responsibilities:
 * - maintain a static page registry (no dynamic allocation)
 * - track the active page ID
 * - execute leave/enter hooks when switching pages
 */
static const ui_page_t *g_ui_pages[UI_PAGE_MANAGER_MAX_PAGES];
static uint8_t g_ui_page_count = 0U;
static uint8_t g_ui_current_page_id = 0U;

/**
 * @brief Point d'entrée ui_page_manager_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_manager_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_page_manager_init(void)
{
    for (uint8_t i = 0U; i < UI_PAGE_MANAGER_MAX_PAGES; i++)
    {
        g_ui_pages[i] = 0;
    }

    g_ui_page_count = 0U;
    g_ui_current_page_id = 0U;
}

/**
 * @brief Point d'entrée ui_page_manager_register.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_manager_register.
 *
 * @param page Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_page_manager_register(const ui_page_t *page)
{
    if ((page == 0) || (g_ui_page_count >= UI_PAGE_MANAGER_MAX_PAGES))
    {
        return;
    }

    g_ui_pages[g_ui_page_count] = page;
    g_ui_page_count++;
}

/**
 * @brief Point d'entrée ui_page_set.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_set.
 *
 * @param page_id Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_page_set(uint8_t page_id)
{
    if ((page_id >= g_ui_page_count) || (g_ui_pages[page_id] == 0))
    {
        return;
    }

    const ui_page_t *current_page = g_ui_pages[g_ui_current_page_id];
    const ui_page_t *next_page = g_ui_pages[page_id];

    ui_param_set_bank(0);

    if ((current_page != 0) && (current_page->leave != 0))
    {
        current_page->leave();
    }

    g_ui_current_page_id = page_id;
    ui_core_mute_handle_page_step_led_ownership(page_id);

    if ((next_page != 0) && (next_page->enter != 0))
    {
        next_page->enter();
    }
}
const ui_page_t *ui_page_get(void)
{
    if ((g_ui_current_page_id >= g_ui_page_count) || (g_ui_pages[g_ui_current_page_id] == 0))
    {
        return 0;
    }

    return g_ui_pages[g_ui_current_page_id];
}

/**
 * @brief Point d'entrée ui_page_get_id.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_get_id.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint8_t ui_page_get_id(void)
{
    return g_ui_current_page_id;
}

void ui_page_sync_active_context(void)
{
    const ui_page_t *const active_page = ui_page_get();
    if (active_page == 0)
    {
        return;
    }

    if (active_page->sync_active_context != 0)
    {
        active_page->sync_active_context();
    }
}
