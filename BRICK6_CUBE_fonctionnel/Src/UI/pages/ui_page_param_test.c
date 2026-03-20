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

#include "buttons.h"
#include "drv_display.h"
#include "param_registry.h"
#include "ui_param.h"

static const ui_param_bank_t g_juno_param_banks[] = {
    { .params = { PARAM_JUNO_SAW, PARAM_JUNO_PULSE, PARAM_JUNO_SUB, PARAM_JUNO_PWM } },
    { .params = { PARAM_JUNO_VCF_FREQ, PARAM_JUNO_VCF_RES, PARAM_JUNO_VCF_ENV, PARAM_JUNO_VCF_LFO } },
    { .params = { PARAM_JUNO_ATTACK, PARAM_JUNO_DECAY, PARAM_JUNO_SUSTAIN, PARAM_JUNO_RELEASE } },
    { .params = { PARAM_JUNO_LFO_RATE, PARAM_JUNO_HPF, PARAM_JUNO_PORTA, PARAM_JUNO_MODE } },
};

static const char *const g_juno_page_names[] = {
    "OSC",
    "FILTER",
    "ENV",
    "PERF",
};

static uint8_t g_juno_page_index = 0U;

static void ui_page_param_test_select_page(uint8_t page_index)
{
    const uint8_t page_count = (uint8_t)(sizeof(g_juno_param_banks) / sizeof(g_juno_param_banks[0]));
    g_juno_page_index = (page_count == 0U) ? 0U : (uint8_t)(page_index % page_count);
    ui_param_set_bank(&g_juno_param_banks[g_juno_page_index]);
}

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
    ui_page_param_test_select_page(0U);
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

static void ui_page_param_test_format_value(param_id_t id, char *out, uint32_t out_len)
{
    const param_desc_t *desc = &param_registry[id];
    const float value = param_get(id);

    switch (desc->display_type)
    {
        case PARAM_DISPLAY_BOOL:
        {
            const uint32_t index = (value >= 0.5f) ? 1U : 0U;
            const char *label = ((desc->labels != NULL) && (desc->labels[index] != NULL)) ? desc->labels[index] : ((index != 0U) ? "On" : "Off");
            (void)snprintf(out, out_len, "%s", label);
            break;
        }

        case PARAM_DISPLAY_ENUM:
        {
            const int32_t index = (int32_t)(value + 0.5f);
            const char *label = NULL;
            if ((desc->labels != NULL) && (index >= 0))
            {
                label = desc->labels[index];
            }
            if (label != NULL)
            {
                (void)snprintf(out, out_len, "%s", label);
            }
            else
            {
                (void)snprintf(out, out_len, "%ld", (long)index);
            }
            break;
        }

        case PARAM_DISPLAY_PERCENT:
            (void)snprintf(out, out_len, "%3lu%%", (unsigned long)(value * 100.0f + 0.5f));
            break;

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
    if ((ev == NULL) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        return;
    }

    if (ev->id == (uint8_t)BTN_PARAM_1)
    {
        ui_page_param_test_select_page((uint8_t)(g_juno_page_index + 1U));
    }
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
    const ui_param_bank_t *bank = &g_juno_param_banks[g_juno_page_index];

    for (uint8_t i = 0U; i < 4U; i++)
    {
        const param_id_t id = bank->params[i];
        const param_desc_t *desc = &param_registry[id];
        char value_txt[20];
        char line_txt[32];

        ui_page_param_test_format_value(id, value_txt, (uint32_t)sizeof(value_txt));

        if (i == 0U)
        {
            (void)snprintf(line_txt, sizeof(line_txt), "%s %s %s", g_juno_page_names[g_juno_page_index], desc->name, value_txt);
        }
        else
        {
            (void)snprintf(line_txt, sizeof(line_txt), "%s %s", desc->name, value_txt);
        }

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
