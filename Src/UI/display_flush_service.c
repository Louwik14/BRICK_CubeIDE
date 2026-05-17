#include "display_flush_service.h"

#include "main.h"
#include "drv_display.h"
#include "Storage/sample_capture.h"
#include "ui_renderer_oled.h"

#define DISPLAY_FLUSH_PERIOD_MS 16U

void display_flush_service_poll(void)
{
    static uint32_t last_flush = 0U;
    uint32_t now = HAL_GetTick();

    if (drv_display_flush_in_progress() != 0U)
    {
#if SAMPLE_CAPTURE_DEBUG_UART
        const uint32_t start = HAL_GetTick();
#endif
        drv_display_update();
#if SAMPLE_CAPTURE_DEBUG_UART
        sample_capture_model_debug_note_flush_cost(HAL_GetTick() - start, 1U);
#endif
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

    {
#if SAMPLE_CAPTURE_DEBUG_UART
        const uint32_t start = HAL_GetTick();
#endif
        drv_display_update();
#if SAMPLE_CAPTURE_DEBUG_UART
        sample_capture_model_debug_note_flush_cost(HAL_GetTick() - start, 0U);
#endif
    }
    last_flush = now;
}
