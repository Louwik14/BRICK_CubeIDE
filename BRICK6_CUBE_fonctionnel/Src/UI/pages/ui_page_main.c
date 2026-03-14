#include "ui_page.h"

#include "drv_display.h"

void ui_page_main_enter(void) {}
void ui_page_main_leave(void) {}
void ui_page_main_handle_event(const ui_event_t *ev)
{
    (void)ev;
}
void ui_page_main_tick(void) {}
void ui_page_main_render(void)
{
    drv_display_draw_text(0, 0, "BRICK6");
}

const ui_page_t g_ui_page_main = {
    .enter = ui_page_main_enter,
    .leave = ui_page_main_leave,
    .handle_event = ui_page_main_handle_event,
    .tick = ui_page_main_tick,
    .render = ui_page_main_render,
};
