#ifndef LED_REMAP_H
#define LED_REMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "App/Hall/hall_engine.h"
#include "buttons_ids.h"
#include "led_ids.h"

#define LED_REMAP_PARAM_COUNT 8U

/*
 * Logical param button -> physical param LED.
 *
 * The param button scan chain is wired in a different physical order than the
 * BTN_PARAM_* enum. Keep that mapping explicit here so UI scenes never rely on
 * enum arithmetic.
 *
 * Physical order observed on hardware:
 *   BTN_PARAM_7, BTN_PARAM_8, BTN_PARAM_4, BTN_PARAM_3,
 *   BTN_PARAM_2, BTN_PARAM_1, BTN_PARAM_5, BTN_PARAM_6
 *
 * LED_PARAM_1..LED_PARAM_8 follow that physical LED order.
 */
static const led_id_t g_led_remap_param_led_for_button[LED_REMAP_PARAM_COUNT] = {
    [0] = LED_PARAM_2, /* BTN_PARAM_1 */
    [1] = LED_PARAM_3, /* BTN_PARAM_2 */
    [2] = LED_PARAM_6, /* BTN_PARAM_3 */
    [3] = LED_PARAM_7, /* BTN_PARAM_4 */
    [4] = LED_PARAM_1, /* BTN_PARAM_5 */
    [5] = LED_PARAM_4, /* BTN_PARAM_6 */
    [6] = LED_PARAM_5, /* BTN_PARAM_7 */
    [7] = LED_PARAM_8, /* BTN_PARAM_8 */
};

/*
 * Physical hall LED -> logical hall index.
 *
 * The hall sensors are already explicitly remapped in hall_adc.c through the
 * mux-to-logical table. Reuse the same physical order here so keyboard LED
 * scenes track the correct logical halls on the real panel.
 */
static const uint8_t g_led_remap_hall_for_led[HALL_KEY_COUNT] = {
    5U,  6U,  7U,  4U,
    0U,  3U,  1U,  2U,
    13U, 14U, 15U, 12U,
    8U,  11U, 9U,  10U
};

/*
 * Logical hall index -> physical hall LED.
 *
 * Inverse mapping of g_led_remap_hall_for_led, kept explicit so future scenes
 * can map a logical hall back to its real LED without recomputing anything.
 */
static const led_id_t g_led_remap_led_for_hall[HALL_KEY_COUNT] = {
    [0]  = LED_STEP_5,
    [1]  = LED_STEP_7,
    [2]  = LED_STEP_8,
    [3]  = LED_STEP_6,
    [4]  = LED_STEP_4,
    [5]  = LED_STEP_1,
    [6]  = LED_STEP_2,
    [7]  = LED_STEP_3,
    [8]  = LED_STEP_13,
    [9]  = LED_STEP_15,
    [10] = LED_STEP_16,
    [11] = LED_STEP_14,
    [12] = LED_STEP_12,
    [13] = LED_STEP_9,
    [14] = LED_STEP_10,
    [15] = LED_STEP_11,
};

static inline bool led_remap_is_param_led(led_id_t led)
{
    return ((uint32_t)led >= (uint32_t)LED_PARAM_1) && ((uint32_t)led <= (uint32_t)LED_PARAM_8);
}

static inline bool led_remap_is_hall_led(led_id_t led)
{
    return ((uint32_t)led >= (uint32_t)LED_STEP_1) && ((uint32_t)led <= (uint32_t)LED_STEP_16);
}

static inline led_id_t led_remap_param_led_for_button(button_id_t button)
{
    if (((uint32_t)button < (uint32_t)BTN_PARAM_1) || ((uint32_t)button > (uint32_t)BTN_PARAM_8))
    {
        return LED_COUNT_TOTAL;
    }

    return g_led_remap_param_led_for_button[(uint32_t)button - (uint32_t)BTN_PARAM_1];
}

static inline uint8_t led_remap_hall_for_led(led_id_t led)
{
    return g_led_remap_hall_for_led[(uint32_t)led - (uint32_t)LED_STEP_1];
}

static inline led_id_t led_remap_led_for_hall(uint8_t hall)
{
    if (hall >= HALL_KEY_COUNT)
    {
        return LED_COUNT_TOTAL;
    }

    return g_led_remap_led_for_hall[hall];
}

#endif
