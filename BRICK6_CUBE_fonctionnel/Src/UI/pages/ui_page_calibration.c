#include "pages/ui_page_calibration.h"

#include <stdio.h>
#include "stm32h7xx_hal.h"

#include "App/Hall/hall_calibration.h"
#include "drv_display.h"
#include "ui_page_manager.h"
#include "UI/font.h"

#define CAL_GRID_COLS                     8U
#define CAL_CELL_W                        16U
#define CAL_CELL_H                        22U
#define CAL_GRID_X                        0U
#define CAL_GRID_Y                        8U
#define CAL_OK_DISPLAY_TIME_MS            1000U
#define USER_CAL_MESSAGE_TIME_MS          1200U

static uint8_t g_save_done = 0U;
static uint32_t g_cal_done_tick = 0U;
static uint8_t g_user_save_done = 0U;
static uint32_t g_user_message_tick = 0U;

static const char *ui_page_user_calibration_stage_label(hall_user_calibration_stage_t stage)
{
    switch (stage)
    {
        case HALL_USER_CAL_STAGE_FORT:
            return "FORT";

        case HALL_USER_CAL_STAGE_MID:
            return "MID";

        case HALL_USER_CAL_STAGE_SOFT:
            return "SOFT";

        case HALL_USER_CAL_STAGE_DONE:
        case HALL_USER_CAL_STAGE_IDLE:
        default:
            return "WAIT";
    }
}

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

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        const uint8_t col = i % CAL_GRID_COLS;
        const uint8_t row = i / CAL_GRID_COLS;
        const uint8_t x = CAL_GRID_X + (col * CAL_CELL_W);
        const uint8_t y = CAL_GRID_Y + (row * CAL_CELL_H);

        const uint8_t progress = hall_calibration_get_count(i);
        const uint8_t done = hall_calibration_is_key_done(i);

        drv_display_draw_rect(x, y, CAL_CELL_W - 1U, CAL_CELL_H - 1U);

        if (done != 0U)
        {
            drv_display_draw_text((uint8_t)(x + 2U), (uint8_t)(y + 7U), "OK");
        }
        else if (progress > 0U)
        {
            const uint8_t fill_h =
                (uint8_t)(((uint16_t)(CAL_CELL_H - 2U) * progress) / 100U);

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
    else
    {
        drv_display_draw_text(30U, 58U, "HOLD KEYS");
    }
}

static void ui_page_user_calibration_enter(void)
{
    hall_user_calibration_start();
    g_user_save_done = 0U;
    g_user_message_tick = 0U;
}

static void ui_page_user_calibration_leave(void)
{
}

static void ui_page_user_calibration_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

static void ui_page_user_calibration_tick(void)
{
    hall_user_calibration_process();

    if (hall_user_calibration_is_done() == 0U)
    {
        return;
    }

    if (g_user_message_tick == 0U)
    {
        g_user_message_tick = HAL_GetTick();

        if (hall_user_calibration_was_successful() != 0U)
        {
            hall_calibration_save();
            g_user_save_done = 1U;
        }
    }

    if ((HAL_GetTick() - g_user_message_tick) < USER_CAL_MESSAGE_TIME_MS)
    {
        return;
    }

    if (g_user_save_done != 0U)
    {
        ui_page_set(UI_PAGE_MAIN);
    }
    else
    {
        hall_user_calibration_start();
        g_user_message_tick = 0U;
    }
}

static void ui_page_user_calibration_render(void)
{
    char line1[24];
    char line2[24];
    char line3[24];

    drv_display_draw_text(0U, 0U, "CALIB USER");

    if (hall_user_calibration_is_done() != 0U)
    {
        if (hall_user_calibration_was_successful() != 0U)
        {
            drv_display_draw_text(0U, 20U, "PROFILE READY");
            drv_display_draw_text(0U, 40U, "SAVED");
        }
        else
        {
            drv_display_draw_text(0U, 20U, "PROFILE INVALID");
            drv_display_draw_text(0U, 40U, "RETRY");
        }

        return;
    }

    (void)snprintf(line1, sizeof(line1), "%s %u/%u",
                   ui_page_user_calibration_stage_label(hall_user_calibration_get_stage()),
                   (unsigned)hall_user_calibration_get_stage_count(),
                   (unsigned)hall_user_calibration_get_target_count());

    drv_display_draw_text(0U, 16U, line1);
    drv_display_draw_text(0U, 32U, "3 NOTES / 10X");

    switch (hall_user_calibration_get_stage())
    {
        case HALL_USER_CAL_STAGE_FORT:
            (void)snprintf(line2, sizeof(line2), "PLAY VERY HARD");
            (void)snprintf(line3, sizeof(line3), "STRONG TRIADS");
            break;

        case HALL_USER_CAL_STAGE_MID:
            (void)snprintf(line2, sizeof(line2), "PLAY MEDIUM");
            (void)snprintf(line3, sizeof(line3), "EVEN TRIADS");
            break;

        case HALL_USER_CAL_STAGE_SOFT:
        default:
            (void)snprintf(line2, sizeof(line2), "PLAY VERY SOFT");
            (void)snprintf(line3, sizeof(line3), "LIGHT TRIADS");
            break;
    }

    drv_display_draw_text(0U, 46U, line2);
    drv_display_draw_text(0U, 58U, line3);
}

const ui_page_t g_ui_page_calibration = {
    .enter = ui_page_calibration_enter,
    .leave = ui_page_calibration_leave,
    .handle_event = ui_page_calibration_handle_event,
    .tick = ui_page_calibration_tick,
    .render = ui_page_calibration_render,
};

const ui_page_t g_ui_page_user_calibration = {
    .enter = ui_page_user_calibration_enter,
    .leave = ui_page_user_calibration_leave,
    .handle_event = ui_page_user_calibration_handle_event,
    .tick = ui_page_user_calibration_tick,
    .render = ui_page_user_calibration_render,
};
