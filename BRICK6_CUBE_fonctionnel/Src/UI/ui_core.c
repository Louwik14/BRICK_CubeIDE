#include "ui_core.h"

#include "encoders.h"
#include "pages/ui_page_main.h"
#include "pages/ui_page_param_test.h"
#include "ui_event.h"
#include "ui_navigation.h"
#include "ui_page_manager.h"
#include "ui_param.h"
#include "ui_renderer_oled.h"

void ui_core_init(void)
{
    ui_page_manager_init();

    /*
     * Register pages once at boot. Registration order defines stable page IDs
     * used by the navigation rule table.
     */
    ui_page_manager_register(&g_ui_page_main);
    ui_page_manager_register(&g_ui_page_param_test);

    ui_page_set(UI_PAGE_MAIN);
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
        ui_navigation_handle_event(&ev);

        const ui_page_t *active_page = ui_page_get();
        if ((active_page != 0) && (active_page->handle_event != 0))
        {
            active_page->handle_event(&ev);
        }
    }

    const ui_page_t *active_page = ui_page_get();
    if ((active_page != 0) && (active_page->tick != 0))
    {
        active_page->tick();
    }

    ui_renderer_oled_draw();
}
