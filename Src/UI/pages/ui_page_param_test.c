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
#include "ui_renderer_template.h"

static const ui_param_bank_t g_tone_param_banks[] = {
    { .params = { PARAM_SAMPLER_SAMPLE, PARAM_SAMPLER_MODE, PARAM_SAMPLER_START, PARAM_SAMPLER_END } },
    { .params = { PARAM_SAMPLER_GAIN, PARAM_SAMPLER_TUNE, PARAM_SAMPLER_LOOP_START, PARAM_SAMPLER_SLICE_COUNT } },
    { .params = { PARAM_MIX_LEVEL, PARAM_MIX_PAN, PARAM_VCA_ATTACK, PARAM_COUNT } },
    { .params = { PARAM_MIDI_PROGRAM, PARAM_MIDI_CC1_1, PARAM_MIDI_CC1_2, PARAM_MIDI_CC1_3 } },
};

static const char *const g_tone_page_names[] = {
    "PLAY",
    "MOTION",
    "CTRL",
    "COLOR",
};

static uint8_t g_tone_page_index = 0U;

static void ui_page_param_test_select_page(uint8_t page_index)
{
    const uint8_t page_count = (uint8_t)(sizeof(g_tone_param_banks) / sizeof(g_tone_param_banks[0]));
    g_tone_page_index = (page_count == 0U) ? 0U : (uint8_t)(page_index % page_count);
    ui_param_set_bank(&g_tone_param_banks[g_tone_page_index]);
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
    const float display_value = param_value_policy_canonical_to_display(id, 0U, value);

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
            (void)snprintf(out, out_len, "%.2f %s", (double)display_value, desc->unit);
            break;

        case PARAM_DISPLAY_DB:
            (void)snprintf(out, out_len, "%.2f %s", (double)display_value, desc->unit);
            break;

        case PARAM_DISPLAY_TIME_MS:
            (void)snprintf(out, out_len, "%.2f ms", (double)display_value);
            break;

        case PARAM_DISPLAY_RATIO:
            (void)snprintf(out, out_len, "%.2f", (double)display_value);
            break;

        case PARAM_DISPLAY_INT:
            (void)snprintf(out, out_len, "%ld", (long)(value + 0.5f));
            break;

        default:
            if ((desc->unit != 0) && (desc->unit[0] != '\0'))
            {
                (void)snprintf(out, out_len, "%.2f %s", (double)display_value, desc->unit);
            }
            else
            {
                (void)snprintf(out, out_len, "%.2f", (double)display_value);
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
        ui_page_param_test_select_page((uint8_t)(g_tone_page_index + 1U));
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
    const ui_param_bank_t *bank = &g_tone_param_banks[g_tone_page_index];

    for (uint8_t i = 0U; i < 4U; i++)
    {
        const param_id_t id = bank->params[i];
        const param_desc_t *desc = &param_registry[id];
        char value_txt[20];
        char line_txt[32];

        ui_page_param_test_format_value(id, value_txt, (uint32_t)sizeof(value_txt));

        if (i == 0U)
        {
            (void)snprintf(line_txt, sizeof(line_txt), "%s %s %s", g_tone_page_names[g_tone_page_index], desc->name, value_txt);
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
