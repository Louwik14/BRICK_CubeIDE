#include "UI/pages/ui_page_stream_calibration.h"

#include <stdio.h>

#include "Core/stream_calibration.h"
#include "drv_display.h"

static void stream_calibration_render(void)
{
#if BRICK6_STREAM_CALIBRATION
    char line[28];
    drv_display_draw_text(0U, 0U, "STREAM TEST");
    if (brick6_stream_calibration_error() != 0U)
    {
        drv_display_draw_text(0U, 20U, "WAV / SD ERROR");
        drv_display_draw_text(0U, 42U, "CHECK VOIX1..8");
        return;
    }
    if (brick6_stream_calibration_complete() != 0U)
    {
        drv_display_draw_text(0U, 16U, "COMPLETE");
        (void)snprintf(line, sizeof(line), "PASS %u  FAIL %u",
                       brick6_stream_calibration_pass_count(),
                       brick6_stream_calibration_fail_count());
        drv_display_draw_text(0U, 40U, line);
        return;
    }
    const uint16_t current = brick6_stream_calibration_case_index();
    const uint16_t total = brick6_stream_calibration_case_count();
    const uint8_t progress = (uint8_t)(((uint32_t)current * 100U) / total);
    drv_display_draw_rect(0U, 14U, 120U, 9U);
    if (progress != 0U)
    {
        drv_display_fill_rect(2U, 16U, (uint8_t)((116U * progress) / 100U), 5U);
    }
    (void)snprintf(line, sizeof(line), "CASE %u / %u", current + 1U, total);
    drv_display_draw_text(0U, 32U, line);
    (void)snprintf(line, sizeof(line), "%uK / N%u / A%u",
                   (unsigned)BRICK6_STREAM_CALIBRATION_PAGE_KIB,
                   brick6_stream_calibration_current_passes(),
                   brick6_stream_calibration_current_advance());
    drv_display_draw_text(0U, 50U, line);
#else
    drv_display_draw_text(0U, 0U, "DIAGNOSTIC OFF");
#endif
}

const ui_page_t g_ui_page_stream_calibration = {
    .render = stream_calibration_render,
};
