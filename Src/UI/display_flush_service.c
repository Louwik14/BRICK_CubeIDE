#include "display_flush_service.h"

#include "drv_display.h"
#include "ui_renderer_oled.h"

static volatile uint8_t g_display_frame_pending;

void display_flush_service_frame_ready(void)
{
    g_display_frame_pending = 1U;
}

uint8_t display_flush_service_frame_pending(void)
{
    return g_display_frame_pending;
}

void display_flush_service_poll(void)
{
    if (ui_renderer_oled_is_rendering() != 0U)
    {
        return;
    }

    /* A completion/error wake owns the continuation of the current flush.
     * Do not consume the newer frame while advancing the snapshot DMA. */
    if (drv_display_flush_continuation_pending() != 0U)
    {
        drv_display_update();
    }

    if ((drv_display_flush_in_progress() == 0U)
        && (g_display_frame_pending != 0U)
        && (drv_display_get_state() == DRV_DISPLAY_STATE_READY))
    {
        drv_display_update();
        if (drv_display_flush_in_progress() != 0U)
            g_display_frame_pending = 0U;
    }
}
