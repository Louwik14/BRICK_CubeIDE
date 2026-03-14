#include "buttons.h"

#include <string.h>

#include "buttons_hw.h"

#define BUTTONS_DEBOUNCE_MS 10U

static button_state_t button_states[BTN_COUNT];

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
