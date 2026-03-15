#include "pages/ui_page_hall_debug.h"

#include <stdio.h>

#include "App/hall_kbd.h"
#include "drv_display.h"

static uint8_t s_hall_values[HALL_KBD_KEY_COUNT];

static void ui_page_hall_debug_enter(void)
{
    for (uint8_t key = 0U; key < HALL_KBD_KEY_COUNT; key++)
    {
        s_hall_values[key] = hall_kbd_get_value(key);
    }
}

static void ui_page_hall_debug_leave(void) {}

static void ui_page_hall_debug_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

static void ui_page_hall_debug_tick(void)
{
    for (uint8_t key = 0U; key < HALL_KBD_KEY_COUNT; key++)
    {
        s_hall_values[key] = hall_kbd_get_value(key);
    }
}

static void ui_page_hall_debug_render(void)
{
    drv_display_draw_text(0U, 0U, "HALL DEBUG");

    for (uint8_t row = 0U; row < (HALL_KBD_KEY_COUNT / 2U); row++)
    {
        const uint8_t key_left = (uint8_t)(row * 2U);
        const uint8_t key_right = (uint8_t)(key_left + 1U);
        char line[28];

        (void)snprintf(line,
                       sizeof(line),
                       "%02u: %3u   %02u: %3u",
                       (unsigned int)key_left,
                       (unsigned int)s_hall_values[key_left],
                       (unsigned int)key_right,
                       (unsigned int)s_hall_values[key_right]);

        drv_display_draw_text(0U, (uint8_t)(8U + (row * 7U)), line);
    }
}

const ui_page_t g_ui_page_hall_debug = {
    .enter = ui_page_hall_debug_enter,
    .leave = ui_page_hall_debug_leave,
    .handle_event = ui_page_hall_debug_handle_event,
    .tick = ui_page_hall_debug_tick,
    .render = ui_page_hall_debug_render,
};
