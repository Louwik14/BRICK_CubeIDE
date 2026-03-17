#include "pages/ui_page_calibration.h"

#include <stdio.h>
#include "stm32h7xx_hal.h"

#include "App/Hall/hall_calibration.h"
#include "drv_display.h"
#include "ui_page_manager.h"
#include "UI/font.h"

#define CAL_GRID_COLS             8U
#define CAL_CELL_W                16U
#define CAL_CELL_H                22U
#define CAL_GRID_X                0U
#define CAL_GRID_Y                8U
#define CAL_OK_DISPLAY_TIME_MS    1000U

static uint8_t g_save_done = 0U;
static uint32_t g_cal_done_tick = 0U;

static void ui_page_calibration_enter(void)
{
    hall_calibration_start();
    g_save_done = 0U;
    g_cal_done_tick = 0U;
}

static void ui_page_calibration_leave(void)
{
}

static void ui_page_calibration_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

static void ui_page_calibration_tick(void)
{
    hall_calibration_process();

    if (hall_calibration_is_done() == 0U)
    {
        return;
    }

    if (g_save_done == 0U)
    {
        hall_calibration_save();
        g_cal_done_tick = HAL_GetTick();
        g_save_done = 1U;
        return;
    }

    if ((HAL_GetTick() - g_cal_done_tick) >= CAL_OK_DISPLAY_TIME_MS)
    {
        ui_page_set(UI_PAGE_MAIN);
    }
}

static void ui_page_calibration_render(void)
{
    drv_display_draw_text(0U, 0U, "CALIBRATION");

    for (uint8_t i = 0U; i < 16U; i++)
    {
        const uint8_t col = i % CAL_GRID_COLS;
        const uint8_t row = i / CAL_GRID_COLS;
        const uint8_t x = CAL_GRID_X + (col * CAL_CELL_W);
        const uint8_t y = CAL_GRID_Y + (row * CAL_CELL_H);

        const uint8_t hits = hall_calibration_get_count(i);

        drv_display_draw_rect(x, y, CAL_CELL_W - 1U, CAL_CELL_H - 1U);

        if (hits > 0U)
        {
            uint8_t fill_h = ((CAL_CELL_H - 2U) * hits) / 3U;

            drv_display_fill_rect(
                (uint8_t)(x + 1U),
                (uint8_t)(y + (CAL_CELL_H - 1U - fill_h)),
                (uint8_t)(CAL_CELL_W - 2U),
                fill_h
            );
        }
    }

    if (hall_calibration_is_done() != 0U)
    {
        drv_display_draw_text(48U, 58U, "CAL OK");
    }
}
const ui_page_t g_ui_page_calibration = {
    .enter = ui_page_calibration_enter,
    .leave = ui_page_calibration_leave,
    .handle_event = ui_page_calibration_handle_event,
    .tick = ui_page_calibration_tick,
    .render = ui_page_calibration_render,
};
