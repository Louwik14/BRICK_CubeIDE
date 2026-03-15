#include "pages/ui_page_hall_debug.h"

#include <stdio.h>

#include "App/hall_kbd.h"
#include "drv_display.h"
#include "App/hall_mux_test.h"

enum
{
    HALL_DEBUG_FONT_HEIGHT_PX = 8U,
    HALL_DEBUG_MAX_ROWS = 8U,
    HALL_DEBUG_KEYS_PER_ROW = 2U,
};

static void ui_page_hall_debug_enter(void) {}

static void ui_page_hall_debug_leave(void) {}

static void ui_page_hall_debug_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

static void ui_page_hall_debug_tick(void) {}

static void ui_page_hall_debug_render(void)
{
    const uint8_t available_rows =
        (uint8_t)(OLED_HEIGHT / HALL_DEBUG_FONT_HEIGHT_PX);

    const uint8_t rows_to_draw =
        (available_rows < HALL_DEBUG_MAX_ROWS)
            ? available_rows
            : HALL_DEBUG_MAX_ROWS;

    for (uint8_t row = 0U; row < rows_to_draw; row++)
    {
        const uint8_t key_left  = (uint8_t)(row * HALL_DEBUG_KEYS_PER_ROW);
        const uint8_t key_right = (uint8_t)(key_left + 1U);

        if (key_right >= HALL_KBD_KEY_COUNT)
        {
            break;
        }

        /* efface la ligne avant redraw */
        drv_display_clear_rect(
            0,
            (uint8_t)(row * HALL_DEBUG_FONT_HEIGHT_PX),
            OLED_WIDTH,
            HALL_DEBUG_FONT_HEIGHT_PX
        );

        char line[32];

        (void)snprintf(line,
                       sizeof(line),
                       "%02u:%5u   %02u:%5u",
                       (unsigned int)key_left,
                       (unsigned int)hall_mux_test_get_raw(key_left),
                       (unsigned int)key_right,
                       (unsigned int)hall_mux_test_get_raw(key_right));

        drv_display_draw_text(
            0U,
            (uint8_t)(row * HALL_DEBUG_FONT_HEIGHT_PX),
            line);
    }
}

const ui_page_t g_ui_page_hall_debug = {
    .enter = ui_page_hall_debug_enter,
    .leave = ui_page_hall_debug_leave,
    .handle_event = ui_page_hall_debug_handle_event,
    .tick = ui_page_hall_debug_tick,
    .render = ui_page_hall_debug_render,
};
