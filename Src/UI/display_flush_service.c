#include "display_flush_service.h"

#include "main.h"
#include "drv_display.h"
#include "ui_renderer_oled.h"

#define DISPLAY_FLUSH_PERIOD_MS 16U

void display_flush_service_poll(void)
{
    static uint32_t last_flush = 0U;
    uint32_t now = HAL_GetTick();

    if (drv_display_flush_in_progress() != 0U)
    {
        drv_display_update();
        return;
    }

    if ((now - last_flush) < DISPLAY_FLUSH_PERIOD_MS)
    {
        return;
    }

    if (ui_renderer_oled_is_rendering() != 0U)
    {
        return;
    }

    drv_display_update();
    last_flush = now;
}
