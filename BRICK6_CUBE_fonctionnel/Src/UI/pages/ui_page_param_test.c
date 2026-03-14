/**
 * @file ui_page_param_test.c
 * @brief Module applicatif ui_page_param_test.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à ui_page_param_test.
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

#include "pages/ui_page_param_test.h"

#include <stdio.h>

#include "drv_display.h"
#include "param_registry.h"
#include "ui_param.h"

static const ui_param_bank_t g_param_test_bank = {
    .params = {
        PARAM_DAISY_COMP_THRESHOLD_DB,
        PARAM_DAISY_COMP_RATIO,
        PARAM_DAISY_COMP_ATTACK_S,
        PARAM_DAISY_COMP_RELEASE_S,
    },
};

/**
 * @brief Point d'entrée ui_page_param_test_enter.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_param_test_enter.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void ui_page_param_test_enter(void)
{
    ui_param_set_bank(&g_param_test_bank);
}

/**
 * @brief Point d'entrée ui_page_param_test_leave.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_param_test_leave.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void ui_page_param_test_leave(void) {}

/**
 * @brief Point d'entrée ui_page_param_test_handle_event.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_param_test_handle_event.
 *
 * @param ev Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void ui_page_param_test_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

/**
 * @brief Point d'entrée ui_page_param_test_tick.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_param_test_tick.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void ui_page_param_test_tick(void) {}

/**
 * @brief Point d'entrée ui_page_param_test_format_value.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_param_test_format_value.
 *
 * @param id Paramètre d'entrée de l'API.
 * @param out Paramètre d'entrée de l'API.
 * @param out_len Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void ui_page_param_test_format_value(param_id_t id, char *out, uint32_t out_len)
{
    const param_desc_t *desc = &param_registry[id];
    const float value = param_get(id);

    switch (desc->display_type)
    {
        case PARAM_DISPLAY_DB:
            (void)snprintf(out, out_len, "%.1f %s", (double)value, desc->unit);
            break;

        case PARAM_DISPLAY_TIME_MS:
            (void)snprintf(out, out_len, "%.1f ms", (double)(value * 1000.0f));
            break;

        case PARAM_DISPLAY_RATIO:
            (void)snprintf(out, out_len, "%.2f", (double)value);
            break;

        default:
            if ((desc->unit != 0) && (desc->unit[0] != '\0'))
            {
                (void)snprintf(out, out_len, "%.2f %s", (double)value, desc->unit);
            }
            else
            {
                (void)snprintf(out, out_len, "%.2f", (double)value);
            }
            break;
    }
}

/**
 * @brief Point d'entrée ui_page_param_test_render.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_page_param_test_render.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void ui_page_param_test_render(void)
{
    for (uint8_t i = 0U; i < 4U; i++)
    {
        const param_id_t id = g_param_test_bank.params[i];
        char value_txt[20];
        char line_txt[32];

        ui_page_param_test_format_value(id, value_txt, (uint32_t)sizeof(value_txt));
        (void)snprintf(line_txt, sizeof(line_txt), "%s %s", param_registry[id].name, value_txt);

        drv_display_draw_text(0U, (uint8_t)(i * 16U), line_txt);
    }

}

const ui_page_t g_ui_page_param_test = {
    .enter = ui_page_param_test_enter,
    .leave = ui_page_param_test_leave,
    .handle_event = ui_page_param_test_handle_event,
    .tick = ui_page_param_test_tick,
    .render = ui_page_param_test_render,
};
