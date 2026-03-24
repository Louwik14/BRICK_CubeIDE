#include "pages/ui_page_debug_hall.h"

#include <stdio.h>

#include "App/mux_pots.h"
#include "drv_display.h"

static void ui_page_debug_hall_enter(void) {}
static void ui_page_debug_hall_leave(void) {}

static void ui_page_debug_hall_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

static void ui_page_debug_hall_tick(void) {}

static void ui_page_debug_hall_render(void)
{
    char line0[24];
    char line1[24];
    char line2[24];
    char line3[24];
    char line4[24];
    const uint8_t pot_count = 4U;

    drv_display_draw_text(0U, 0U, "POTS DEBUG (ADC)");

    for (uint8_t pot = 0U; pot < pot_count; pot++)
    {
        char *line = line1;

        if (pot == 1U)
        {
            line = line2;
        }
        else if (pot == 2U)
        {
            line = line3;
        }
        else if (pot == 3U)
        {
            line = line4;
        }

        if (mux_pots_is_valid(pot) != 0U)
        {
            (void)snprintf(line, sizeof(line0), "P%u: %4u",
                           (unsigned)(pot + 1U),
                           (unsigned)mux_pots_get(pot));
        }
        else
        {
            (void)snprintf(line, sizeof(line0), "P%u: ----", (unsigned)(pot + 1U));
        }
    }

    drv_display_draw_text(0U, 14U, line1);
    drv_display_draw_text(0U, 24U, line2);
    drv_display_draw_text(0U, 34U, line3);
    drv_display_draw_text(0U, 44U, line4);
}

const ui_page_t g_ui_page_debug_hall = {
    .enter = ui_page_debug_hall_enter,
    .leave = ui_page_debug_hall_leave,
    .handle_event = ui_page_debug_hall_handle_event,
    .tick = ui_page_debug_hall_tick,
    .render = ui_page_debug_hall_render,
};
