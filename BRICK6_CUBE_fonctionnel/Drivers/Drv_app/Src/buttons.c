#include "buttons.h"

#include <string.h>

#include "buttons_hw.h"

/*
 * Temporary debug switch for physical->logical button mapping.
 * Set to 1 to print only debounced press events as: "BTN <index>".
 * Set back to 0 (default) to compile all debug logging code out.
 */
#ifndef BUTTON_DEBUG_LOG
#define BUTTON_DEBUG_LOG 1
#endif

#define BUTTONS_DEBOUNCE_MS 10U

static button_state_t button_states[BTN_COUNT];

#if BUTTON_DEBUG_LOG
#include "usart.h"

/*
 * Best-effort UART logging for mapping validation.
 * - Called only from tasklet context (never IRQ).
 * - Called only on debounced press edges.
 * - Drops messages if UART is busy to avoid contention/blocking.
 */
static void buttons_debug_log_press(button_id_t btn)
{
    if (huart1.gState != HAL_UART_STATE_READY)
    {
        return;
    }

    char msg[12];
    uint32_t idx = (uint32_t)btn;
    uint32_t pos = 0U;

    msg[pos++] = 'B';
    msg[pos++] = 'T';
    msg[pos++] = 'N';
    msg[pos++] = ' ';

    if (idx >= 10U)
    {
        msg[pos++] = (char)('0' + (idx / 10U));
    }
    msg[pos++] = (char)('0' + (idx % 10U));
    msg[pos++] = '\r';
    msg[pos++] = '\n';

    (void)HAL_UART_Transmit(&huart1, (uint8_t *)msg, (uint16_t)pos, HAL_MAX_DELAY);
}
#endif

void buttons_init(void)
{
    memset(button_states, 0, sizeof(button_states));

    buttons_hw_init();
    buttons_hw_read();

    for (uint32_t i = 0U; i < (uint32_t)BTN_COUNT; i++)
    {
        const uint8_t raw = buttons_hw_get((button_id_t)i);
        button_states[i].state = raw;
        button_states[i].prev_state = raw;
    }
}

void buttons_update(uint32_t dt_ms)
{
    buttons_hw_read();

    for (uint32_t i = 0U; i < (uint32_t)BTN_COUNT; i++)
    {
        button_state_t *s = &button_states[i];
        const uint8_t raw = buttons_hw_get((button_id_t)i);

        s->pressed = 0U;
        s->released = 0U;

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
#if BUTTON_DEBUG_LOG
                    buttons_debug_log_press((button_id_t)i);
#endif
                }
                else if ((s->prev_state != 0U) && (s->state == 0U))
                {
                    s->released = 1U;
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
}

uint8_t button_pressed(button_id_t btn)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }

    return button_states[(uint32_t)btn].pressed;
}

uint8_t button_released(button_id_t btn)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }

    return button_states[(uint32_t)btn].released;
}

uint8_t button_down(button_id_t btn)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }

    return button_states[(uint32_t)btn].state;
}
