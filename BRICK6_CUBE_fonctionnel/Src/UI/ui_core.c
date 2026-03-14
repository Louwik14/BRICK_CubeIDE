#include "ui_core.h"

#include "ui_event.h"
#include "ui_page.h"
#include "ui_renderer_oled.h"

extern const ui_page_t g_ui_page_main;

static const ui_page_t *g_ui_active_page = 0;

void ui_core_init(void)
{
    g_ui_active_page = &g_ui_page_main;

    if (g_ui_active_page->enter != 0)
    {
        g_ui_active_page->enter();
    }
}

void ui_core_tick(void)
{
    ui_event_t ev;

    ui_event_from_inputs();

    while (ui_event_pop(&ev))
    {
        if ((g_ui_active_page != 0) && (g_ui_active_page->handle_event != 0))
        {
            g_ui_active_page->handle_event(&ev);
        }
    }

    if ((g_ui_active_page != 0) && (g_ui_active_page->tick != 0))
    {
        g_ui_active_page->tick();
    }

    ui_renderer_oled_draw(g_ui_active_page);
}
