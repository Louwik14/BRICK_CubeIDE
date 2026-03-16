#include "pages/ui_page_debug_hall.h"

#include <stdio.h>

#include "drv_display.h"
#include "hall_adc.h"

static void ui_page_debug_hall_enter(void) {}

static void ui_page_debug_hall_leave(void) {}

static void ui_page_debug_hall_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

static void ui_page_debug_hall_tick(void) {}

static void ui_page_debug_hall_render(void)
{
    char raw_txt[20];
    const uint16_t raw = hall_adc_get_raw(0U);

    (void)snprintf(raw_txt, sizeof(raw_txt), "%u", (unsigned)raw);

    drv_display_draw_text(0U, 0U, "DEBUG - HALL");
    drv_display_draw_text(0U, 16U, "Key 0 RAW");
    drv_display_draw_text(0U, 32U, raw_txt);
}

const ui_page_t g_ui_page_debug_hall = {
    .enter = ui_page_debug_hall_enter,
    .leave = ui_page_debug_hall_leave,
    .handle_event = ui_page_debug_hall_handle_event,
    .tick = ui_page_debug_hall_tick,
    .render = ui_page_debug_hall_render,
};
