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

#include "ui_renderer_oled.h"
#include "ui_param.h"

static const ui_param_bank_t g_main_bank = {
    .params = {
        PARAM_DAISY_COMP_THRESHOLD_DB,
        PARAM_DAISY_COMP_RATIO,
        PARAM_DAISY_COMP_ATTACK_S,
        PARAM_DAISY_COMP_RELEASE_S,
    },
};

void ui_page_main_enter(void)
{
    ui_param_set_bank(&g_main_bank);
}

void ui_page_main_leave(void) {}

void ui_page_main_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

void ui_page_main_tick(void) {}

void ui_page_main_render(void)
{
    u8g2_t *u8g2 = &g_u8g2;

    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, u8g2_font_5x8_tf);

    u8g2_DrawStr(u8g2, 0U, 8U, "BRICK6 MAIN");
    u8g2_DrawStr(u8g2, 0U, 24U, "BTN1: PARAM TEST");
    u8g2_DrawStr(u8g2, 0U, 40U, "BTN2: MAIN PAGE");
    u8g2_DrawStr(u8g2, 0U, 56U, "BTN3: HALL DEBUG");

    u8g2_SendBuffer(u8g2);
}

const ui_page_t g_ui_page_main = {
    .enter = ui_page_main_enter,
    .leave = ui_page_main_leave,
    .handle_event = ui_page_main_handle_event,
    .tick = ui_page_main_tick,
    .render = ui_page_main_render,
};
