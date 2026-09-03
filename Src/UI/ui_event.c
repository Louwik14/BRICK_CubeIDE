/**
 * @file ui_event.c
 * @brief Module applicatif ui_event.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à ui_event.
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

#include "ui_event.h"

#include "App/Hall/hall_engine.h"
#include "buttons.h"
#include "ui_core.h"
#include "stm32h7xx.h"

#define UI_EVENT_Q_LEN 32U

static ui_event_t g_ui_evt_q[UI_EVENT_Q_LEN];
static volatile uint8_t g_ui_evt_w = 0U;
static volatile uint8_t g_ui_evt_r = 0U;

/**
 * @brief Point d'entrée ui_event_push.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_event_push.
 *
 * @param ev Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static bool ui_event_push(const ui_event_t *ev)
{
    if (ev == 0)
    {
        return false;
    }

    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    const uint8_t next = (uint8_t)((g_ui_evt_w + 1U) & (UI_EVENT_Q_LEN - 1U));
    if (next == g_ui_evt_r)
    {
        __set_PRIMASK(primask);
        return false;
    }

    g_ui_evt_q[g_ui_evt_w] = *ev;
    __DMB();
    g_ui_evt_w = next;
    __DMB();
    __set_PRIMASK(primask);
    return true;
}

/**
 * @brief Point d'entrée ui_event_from_inputs.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_event_from_inputs.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_event_from_inputs(void)
{
    ui_event_t ev;
    for (uint8_t i = 0U; i < (uint8_t)BTN_COUNT; i++)
    {
        if (button_pressed((button_id_t)i) != 0U)
        {
            ev.type = UI_EVENT_BUTTON_PRESS;
            ev.id = i;
            ev.value = 1;
            ui_event_push(&ev);
        }

        if (button_released((button_id_t)i) != 0U)
        {
            ev.type = UI_EVENT_BUTTON_RELEASE;
            ev.id = i;
            ev.value = 0;
            ui_event_push(&ev);
        }
    }

}

bool ui_event_push_hall(uint8_t hall, uint8_t pressed)
{
    if (hall >= HALL_UI_LANE_COUNT)
    {
        return false;
    }

    const ui_event_t ev = {
        .type = (pressed != 0U) ? UI_EVENT_HALL_PRESS : UI_EVENT_HALL_RELEASE,
        .id = hall,
        .value = (pressed != 0U) ? 1 : 0,
    };
    return ui_event_push(&ev);
}

/**
 * @brief Point d'entrée ui_event_pop.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_event_pop.
 *
 * @param ev Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
bool ui_event_pop(ui_event_t *ev)
{
    if (ev == 0)
    {
        return false;
    }

    if (g_ui_evt_r == g_ui_evt_w)
    {
        return false;
    }

    __DMB();
    *ev = g_ui_evt_q[g_ui_evt_r];
    __DMB();
    g_ui_evt_r = (uint8_t)((g_ui_evt_r + 1U) & (UI_EVENT_Q_LEN - 1U));
    return true;
}
