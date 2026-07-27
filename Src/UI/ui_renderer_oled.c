/**
 * @file ui_renderer_oled.c
 * @brief Module applicatif ui_renderer_oled.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à ui_renderer_oled.
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

#include "ui_renderer_oled.h"

#include "main.h"
#include "drv_display.h"
#include "ui_boot_loading.h"
#include "ui_page_manager.h"
#include "ui_roll_popup.h"

#define UI_RENDER_PERIOD_MS 16U

static volatile uint8_t g_ui_rendering = 0U;

/**
 * @brief Point d'entrée ui_renderer_oled_draw.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_renderer_oled_draw.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_renderer_oled_draw(void)
{
    const ui_page_t *page = ui_page_get();

    g_ui_rendering = 1U;

    drv_display_clear();

    if (ui_boot_loading_is_active() != 0U)
    {
        ui_boot_loading_render();
        ui_boot_loading_note_frame_rendered();
    }
    else if ((page != 0) && (page->render != 0))
    {
        page->render();
        ui_roll_popup_render(HAL_GetTick());
    }

    g_ui_rendering = 0U;
}

/**
 * @brief Cadence le rendu UI à une fréquence adaptée à l'OLED.
 */
void ui_renderer_oled_service_poll(void)
{
    static uint32_t last_render = 0U;
    const uint32_t now = HAL_GetTick();

    if ((now - last_render) < UI_RENDER_PERIOD_MS)
    {
        return;
    }

    ui_renderer_oled_draw();
    last_render = now;
}

uint8_t ui_renderer_oled_is_rendering(void)
{
    return g_ui_rendering;
}
