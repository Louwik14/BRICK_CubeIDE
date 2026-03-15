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
    char line[32];

    for (uint8_t row = 0; row < 8; row++)
    {
        uint8_t k0 = row * 2;
        uint8_t k1 = k0 + 1;

        snprintf(line,
                 sizeof(line),
                 "%u %u",
                 (unsigned int)hall_mux_test_get_raw(k0),
                 (unsigned int)hall_mux_test_get_raw(k1));

        drv_display_draw_text(0, row * 8, line);
    }
}
const ui_page_t g_ui_page_hall_debug = {
    .enter = ui_page_hall_debug_enter,
    .leave = ui_page_hall_debug_leave,
    .handle_event = ui_page_hall_debug_handle_event,
    .tick = ui_page_hall_debug_tick,
    .render = ui_page_hall_debug_render,
};
