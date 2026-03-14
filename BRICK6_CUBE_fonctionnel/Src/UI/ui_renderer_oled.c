#include "ui_renderer_oled.h"

#include "drv_display.h"
#include "ui_page_manager.h"

void ui_renderer_oled_draw(void)
{
    const ui_page_t *page = ui_page_get();

    drv_display_clear();

    if ((page != 0) && (page->render != 0))
    {
        page->render();
    }

    drv_display_update();
}
