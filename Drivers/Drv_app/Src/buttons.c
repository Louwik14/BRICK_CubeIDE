/**
 * @file buttons.c
 * @brief Module applicatif buttons.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à buttons.
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

#include "buttons.h"

#include <string.h>

#include "buttons_hw.h"
#include "IPC/live_clock_control.h"
#include "stm32h7xx_hal.h"

#define BUTTONS_DEBOUNCE_MS 10U

static button_state_t button_states[BTN_COUNT];
static uint32_t button_ingress_serial;

static void button_capture_edge(button_edge_context_t *out_context,
                                uint32_t capture_tick,
                                uint32_t capture_ms,
                                uint32_t ingress_serial)
{
    if (out_context == 0)
    {
        return;
    }
    out_context->capture_tick = capture_tick;
    out_context->capture_ms = capture_ms;
    out_context->ingress_serial = ingress_serial;
    out_context->shift_down = 0U;
    out_context->track_select_armed = 0U;
}

static uint32_t button_next_ingress_serial(void)
{
    uint32_t serial = button_ingress_serial + 1U;
    if (serial == 0U)
    {
        serial = 1U;
    }
    button_ingress_serial = serial;
    return serial;
}

/**
 * @brief Point d'entrée buttons_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à buttons_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void buttons_init(void)
{
    memset(button_states, 0, sizeof(button_states));
    button_ingress_serial = 0U;
    buttons_hw_init();
    buttons_hw_read();

    for (uint32_t i = 0U; i < (uint32_t)BTN_COUNT; i++)
    {
        uint8_t raw = buttons_hw_get((button_id_t)i);
        button_states[i].state = raw;
        button_states[i].prev_state = raw;
    }
}

/**
 * @brief Point d'entrée buttons_update.
 *
 * Rôle:
 * - Exécuter le traitement associé à buttons_update.
 *
 * @param dt_ms Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void buttons_update(uint32_t dt_ms)
{
    buttons_hw_read();

    for (uint32_t i = 0U; i < (uint32_t)BTN_COUNT; i++)
    {
        button_state_t *s = &button_states[i];
        uint8_t raw = buttons_hw_get((button_id_t)i);



        if (raw != s->state)
        {
            uint32_t acc = (uint32_t)s->debounce + dt_ms;
            if (acc >= BUTTONS_DEBOUNCE_MS)
            {
                s->prev_state = s->state;
                s->state = raw;
                s->debounce = 0U;

                if ((s->prev_state == 0U) && (s->state != 0U))
                {
                    s->pressed = 1U;
                    button_capture_edge(&s->pressed_context,
                                        live_clock_capture_tick(), HAL_GetTick(),
                                        button_next_ingress_serial());
                }
                else if ((s->prev_state != 0U) && (s->state == 0U))
                {
                    s->released = 1U;
                    button_capture_edge(&s->released_context,
                                        live_clock_capture_tick(), HAL_GetTick(),
                                        button_next_ingress_serial());
                }
            }
            else
            {
                s->debounce = (uint16_t)acc;
            }
        }
        else
        {
            s->debounce = 0U;
        }
    }

    /* Resolve modifier context in captured edge order, after the complete
     * debounced scan has produced its edge set. */
    uint8_t shift_state = button_states[(uint32_t)BTN_SHIFT].state;
    uint8_t track_state = button_states[(uint32_t)BTN_TRACK].state;
    if (button_states[(uint32_t)BTN_SHIFT].pressed != 0U)
    {
        shift_state = 0U;
    }
    else if (button_states[(uint32_t)BTN_SHIFT].released != 0U)
    {
        shift_state = 1U;
    }
    if (button_states[(uint32_t)BTN_TRACK].pressed != 0U)
    {
        track_state = 0U;
    }
    else if (button_states[(uint32_t)BTN_TRACK].released != 0U)
    {
        track_state = 1U;
    }

    uint64_t assigned_pressed = 0U;
    uint64_t assigned_released = 0U;
    for (;;)
    {
        button_id_t selected_button = BTN_COUNT;
        uint8_t selected_pressed = 0U;
        uint8_t selected_valid = 0U;
        uint32_t selected_serial = 0U;
        for (uint32_t i = 0U; i < (uint32_t)BTN_COUNT; ++i)
        {
            button_state_t *const s = &button_states[i];
            if ((s->pressed != 0U)
                    && ((assigned_pressed & (UINT64_C(1) << i)) == 0U)
                    && ((selected_valid == 0U)
                        || (s->pressed_context.ingress_serial < selected_serial)))
            {
                selected_button = (button_id_t)i;
                selected_pressed = 1U;
                selected_serial = s->pressed_context.ingress_serial;
                selected_valid = 1U;
            }
            if ((s->released != 0U)
                    && ((assigned_released & (UINT64_C(1) << i)) == 0U)
                    && ((selected_valid == 0U)
                        || (s->released_context.ingress_serial < selected_serial)))
            {
                selected_button = (button_id_t)i;
                selected_pressed = 0U;
                selected_serial = s->released_context.ingress_serial;
                selected_valid = 1U;
            }
        }
        if (selected_button == BTN_COUNT)
        {
            break;
        }

        button_state_t *const selected = &button_states[(uint32_t)selected_button];
        button_edge_context_t *const context = (selected_pressed != 0U)
            ? &selected->pressed_context : &selected->released_context;
        if (selected_button == BTN_SHIFT)
        {
            shift_state = selected_pressed;
        }
        else if (selected_button == BTN_TRACK)
        {
            track_state = selected_pressed;
        }
        context->shift_down = shift_state;
        context->track_select_armed = track_state;

        if (selected_pressed != 0U)
        {
            assigned_pressed |= (UINT64_C(1) << (uint32_t)selected_button);
        }
        else
        {
            assigned_released |= (UINT64_C(1) << (uint32_t)selected_button);
        }
    }
}

/**
 * @brief Point d'entrée button_pressed.
 *
 * Rôle:
 * - Exécuter le traitement associé à button_pressed.
 *
 * @param btn Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
/* Legacy destructive edge accessors were removed: edges carry their context. */
#if 0
 uint8_t removed_button_pressed(button_id_t btn)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }

    uint8_t v = button_states[(uint32_t)btn].pressed;
    button_states[(uint32_t)btn].pressed = 0U;
    return v;
}

/**
 * @brief Point d'entrée button_released.
 *
 * Rôle:
 * - Exécuter le traitement associé à button_released.
 *
 * @param btn Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
 uint8_t removed_button_released(button_id_t btn)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }

    uint8_t v = button_states[(uint32_t)btn].released;
    button_states[(uint32_t)btn].released = 0U;
    return v;
}
#endif
/**
 * @brief Point d'entrée button_down.
 *
 * Rôle:
 * - Exécuter le traitement associé à button_down.
 *
 * @param btn Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
uint8_t button_down(button_id_t btn)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }

    return button_states[(uint32_t)btn].state;
}

uint8_t button_peek_pressed_event(button_id_t btn, button_edge_context_t *out_context)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }
    if (button_states[(uint32_t)btn].pressed == 0U)
    {
        return 0U;
    }
    button_capture_edge(out_context,
                        button_states[(uint32_t)btn].pressed_context.capture_tick,
                        button_states[(uint32_t)btn].pressed_context.capture_ms,
                        button_states[(uint32_t)btn].pressed_context.ingress_serial);
    if (out_context != 0)
    {
        out_context->shift_down = button_states[(uint32_t)btn].pressed_context.shift_down;
        out_context->track_select_armed =
            button_states[(uint32_t)btn].pressed_context.track_select_armed;
    }
    return 1U;
}

uint8_t button_peek_released_event(button_id_t btn, button_edge_context_t *out_context)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }
    if (button_states[(uint32_t)btn].released == 0U)
    {
        return 0U;
    }
    button_capture_edge(out_context,
                        button_states[(uint32_t)btn].released_context.capture_tick,
                        button_states[(uint32_t)btn].released_context.capture_ms,
                        button_states[(uint32_t)btn].released_context.ingress_serial);
    if (out_context != 0)
    {
        out_context->shift_down = button_states[(uint32_t)btn].released_context.shift_down;
        out_context->track_select_armed =
            button_states[(uint32_t)btn].released_context.track_select_armed;
    }
    return 1U;
}

uint8_t button_take_pressed_event(button_id_t btn, button_edge_context_t *out_context)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }

    if (button_states[(uint32_t)btn].pressed == 0U)
    {
        return 0U;
    }

    if (out_context != 0)
    {
        *out_context = button_states[(uint32_t)btn].pressed_context;
    }
    button_states[(uint32_t)btn].pressed = 0U;
    return 1U;
}

uint8_t button_take_released_event(button_id_t btn, button_edge_context_t *out_context)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }

    if (button_states[(uint32_t)btn].released == 0U)
    {
        return 0U;
    }

    if (out_context != 0)
    {
        *out_context = button_states[(uint32_t)btn].released_context;
    }
    button_states[(uint32_t)btn].released = 0U;
    return 1U;
}
