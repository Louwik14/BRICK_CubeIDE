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
#include <stdio.h>
#include "ui_renderer_oled.h"

#include "ui_display.h"
#include "ui_page_manager.h"

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
    static uint8_t drawing = 0;
    static uint32_t last_refresh = 0;
    static uint8_t last_page = 255;

    uint32_t now = HAL_GetTick();

    if(now - last_refresh < 16)
        return;

    last_refresh = now;

    if (drawing) return;
    drawing = 1;

    uint8_t page_id = ui_page_get_id();

    if(page_id != last_page)
    {
        printf("PAGE SWITCH %u -> %u\n", last_page, page_id);
        last_page = page_id;
    }

    const ui_page_t *page = ui_page_get();

    display_clear();

    if ((page != 0) && (page->render != 0))
    {
        page->render();
    }


    drawing = 0;
}
