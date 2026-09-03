#ifndef LED_REMAP_H
#define LED_REMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "App/Hall/hall_engine.h"
#include "buttons_ids.h"
#include "led_hw.h"
#include "led_ids.h"

#define LED_REMAP_PHYSICAL_UNMAPPED 0xFFU

static inline bool led_remap_is_param_led(led_id_t led)
{
    return ((uint32_t)led >= (uint32_t)LED_PARAM_1) && ((uint32_t)led <= (uint32_t)LED_PARAM_8);
}

static inline bool led_remap_is_hall_led(led_id_t led)
{
    return ((uint32_t)led >= (uint32_t)LED_STEP_1) && ((uint32_t)led <= (uint32_t)LED_STEP_16);
}

static inline bool led_remap_is_seq_led(led_id_t led)
{
    return ((uint32_t)led >= (uint32_t)LED_SEQ_1) && ((uint32_t)led <= (uint32_t)LED_SEQ_4);
}

static inline led_id_t led_remap_param_led_for_button(button_id_t button)
{
    if (((uint32_t)button < (uint32_t)BTN_PARAM_1) || ((uint32_t)button > (uint32_t)BTN_TRACK))
    {
        return LED_COUNT_TOTAL;
    }

    (void)button;
    return LED_COUNT_TOTAL;
}

static inline led_id_t led_remap_led_for_hall(uint8_t hall)
{
    if (hall >= HALL_KEY_COUNT)
    {
        return LED_COUNT_TOTAL;
    }

    return (led_id_t)((uint32_t)LED_STEP_1 + (uint32_t)hall);
}

static inline led_id_t led_remap_seq_led(uint8_t index)
{
    if (index >= 4U)
    {
        return LED_COUNT_TOTAL;
    }

    return (led_id_t)((uint32_t)LED_SEQ_1 + (uint32_t)index);
}

static inline uint8_t led_remap_physical_index(led_id_t led, uint8_t *physical_index)
{
    if (physical_index == 0)
    {
        return 0U;
    }

    *physical_index = LED_REMAP_PHYSICAL_UNMAPPED;

    if (led == LED_REC)
    {
        *physical_index = 0U;
    }
    else if (led_remap_is_hall_led(led))
    {
        *physical_index = (uint8_t)(1U + ((uint32_t)led - (uint32_t)LED_STEP_1));
    }
    else if (led_remap_is_seq_led(led))
    {
        *physical_index = (uint8_t)(17U + ((uint32_t)led - (uint32_t)LED_SEQ_1));
    }
    else if ((uint32_t)led < (uint32_t)LED_SEQ_1)
    {
        *physical_index = (uint8_t)led;
    }

    if (*physical_index >= LED_HW_COUNT)
    {
        *physical_index = LED_REMAP_PHYSICAL_UNMAPPED;
        return 0U;
    }

    return (*physical_index != LED_REMAP_PHYSICAL_UNMAPPED) ? 1U : 0U;
}

#endif
