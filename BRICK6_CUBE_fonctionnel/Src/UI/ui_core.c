#include "ui_core.h"

#include "encoders.h"
#include "ui_event.h"
#include "ui_page.h"
#include "pages/ui_page_param_test.h"
#include "ui_param.h"
#include "ui_renderer_oled.h"

static const ui_page_t *g_ui_active_page = 0;

void ui_core_init(void)
{
    g_ui_active_page = &g_ui_page_param_test;

    if (g_ui_active_page->enter != 0)
    {
        g_ui_active_page->enter();
    }
}

void ui_core_tick(void)
{
    ui_event_t ev;

    for (uint8_t encoder = 0U; encoder < (uint8_t)ENC_COUNT; encoder++)
    {
        const int16_t delta = encoder_consume_delta(encoder);
        ui_param_handle_encoder(encoder, delta);
    }

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
