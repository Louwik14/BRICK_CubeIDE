#include "ui_renderer_oled.h"

#include "drv_display.h"

void ui_renderer_oled_draw(const ui_page_t *page)
{
    drv_display_clear();

    if ((page != 0) && (page->render != 0))
    {
        page->render();
    }

    drv_display_update();
}
