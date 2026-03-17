#include "display_flush_service.h"

#include "main.h"
#include "drv_display.h"

#define DISPLAY_FLUSH_PERIOD_MS 33U

void display_flush_service_poll(void)
{
    static uint32_t last_flush = 0U;
    uint32_t now = HAL_GetTick();

    if ((now - last_flush) >= DISPLAY_FLUSH_PERIOD_MS)
    {
        drv_display_update();
        last_flush = now;
    }
}
