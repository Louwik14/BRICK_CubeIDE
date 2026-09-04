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
#include "encoders.h"
#include "IPC/live_clock_control.h"
#include "ui_core.h"
#include "UI/ui_service_wakeup.h"
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"

#define UI_EVENT_Q_LEN 256U

_Static_assert((UI_EVENT_Q_LEN & (UI_EVENT_Q_LEN - 1U)) == 0U,
               "UI event queue length must be a power of two");

static ui_event_t g_ui_evt_q[UI_EVENT_Q_LEN];
static volatile uint16_t g_ui_evt_w = 0U;
static volatile uint16_t g_ui_evt_r = 0U;
static volatile uint32_t g_ui_evt_drop_count = 0U;

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

    const uint16_t next = (uint16_t)((g_ui_evt_w + 1U)
        & (UI_EVENT_Q_LEN - 1U));
    if (next == g_ui_evt_r)
    {
        g_ui_evt_drop_count++;
        __set_PRIMASK(primask);
        return false;
    }

    g_ui_evt_q[g_ui_evt_w] = *ev;
    __DMB();
    g_ui_evt_w = next;
    __DMB();
    __set_PRIMASK(primask);
    ui_service_wakeup(UI_SERVICE_WAKE_INPUT);
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
    for (;;)
    {
        button_id_t selected_button = BTN_COUNT;
        uint8_t selected_pressed = 0U;
        uint8_t selected_valid = 0U;
        uint32_t selected_serial = 0U;
        button_edge_context_t context = {0};
        for (uint8_t i = 0U; i < (uint8_t)BTN_COUNT; ++i)
        {
            button_edge_context_t candidate;
            if ((button_peek_pressed_event((button_id_t)i, &candidate) != 0U)
                    && ((selected_valid == 0U)
                        || (candidate.ingress_serial < selected_serial)))
            {
                selected_button = (button_id_t)i;
                selected_pressed = 1U;
                selected_serial = candidate.ingress_serial;
                selected_valid = 1U;
                context = candidate;
            }
            if ((button_peek_released_event((button_id_t)i, &candidate) != 0U)
                    && ((selected_valid == 0U)
                        || (candidate.ingress_serial < selected_serial)))
            {
                selected_button = (button_id_t)i;
                selected_pressed = 0U;
                selected_serial = candidate.ingress_serial;
                selected_valid = 1U;
                context = candidate;
            }
        }
        if (selected_button == BTN_COUNT)
        {
            break;
        }

        if (selected_pressed != 0U)
        {
            (void)button_take_pressed_event(selected_button, &context);
        }
        else
        {
            (void)button_take_released_event(selected_button, &context);
        }
        ev.type = (selected_pressed != 0U)
            ? UI_EVENT_BUTTON_PRESS : UI_EVENT_BUTTON_RELEASE;
        ev.id = (uint8_t)selected_button;
        ev.value = (selected_pressed != 0U) ? 1 : 0;
        ev.capture_tick = context.capture_tick;
        ev.capture_ms = context.capture_ms;
        ev.ingress_serial = context.ingress_serial;
        ev.shift_down = context.shift_down;
        ev.track_select_armed = context.track_select_armed;
        ev.hall_mode = (uint8_t)ui_get_hall_mode();
        ev.context_track = ui_get_active_lane();
        (void)ui_event_push(&ev);
    }
}

bool ui_event_push_hall(uint8_t hall, uint8_t pressed)
{
    return ui_event_push_hall_context(hall, pressed,
                                       live_clock_capture_tick(), HAL_GetTick(),
                                       0U, button_down(BTN_SHIFT),
                                       button_down(BTN_TRACK),
                                       (uint8_t)ui_get_hall_mode(),
                                       ui_get_active_lane());
}

bool ui_event_push_hall_context(uint8_t hall, uint8_t pressed,
                                uint32_t capture_tick, uint32_t capture_ms,
                                uint32_t ingress_serial, uint8_t shift_down,
                                uint8_t track_select_armed, uint8_t hall_mode,
                                uint8_t context_track)
{
    if (hall >= HALL_UI_LANE_COUNT)
    {
        return false;
    }

    const ui_event_t ev = {
        .type = (pressed != 0U) ? UI_EVENT_HALL_PRESS : UI_EVENT_HALL_RELEASE,
        .id = hall,
        .value = (pressed != 0U) ? 1 : 0,
        .capture_tick = capture_tick,
        .capture_ms = capture_ms,
        .ingress_serial = ingress_serial,
        .shift_down = (shift_down != 0U) ? 1U : 0U,
        .track_select_armed = (track_select_armed != 0U) ? 1U : 0U,
        .hall_mode = hall_mode,
        .context_track = context_track,
    };
    return ui_event_push(&ev);
}

bool ui_event_push_encoder(uint8_t encoder, int8_t direction,
                           uint32_t capture_tick, uint32_t ingress_serial,
                           uint8_t shift_down, uint8_t context_track)
{
    if ((encoder >= (uint8_t)ENC_COUNT) || (direction == 0))
    {
        return false;
    }

    const ui_event_t ev = {
        .type = UI_EVENT_ENCODER,
        .id = encoder,
        .value = direction,
        .capture_tick = capture_tick,
        .capture_ms = HAL_GetTick(),
        .ingress_serial = ingress_serial,
        .shift_down = (shift_down != 0U) ? 1U : 0U,
        .track_select_armed = button_down(BTN_TRACK),
        .hall_mode = (uint8_t)ui_get_hall_mode(),
        .context_track = context_track,
    };
    return ui_event_push(&ev);
}

uint32_t ui_event_drop_count(void)
{
    return g_ui_evt_drop_count;
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
    g_ui_evt_r = (uint16_t)((g_ui_evt_r + 1U) & (UI_EVENT_Q_LEN - 1U));
    return true;
}

uint32_t ui_event_pending_count(void)
{
    return (uint32_t)((uint16_t)(g_ui_evt_w - g_ui_evt_r));
}
