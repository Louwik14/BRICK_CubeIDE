#include "pages/ui_page_calibration.h"

#include <stdio.h>
#include "stm32h7xx_hal.h"

#include "App/Hall/hall_calibration.h"
#include "App/control_domain.h"
#include "drv_display.h"
#include "ui_navigation.h"
#include "ui_page_manager.h"
#include "UI/font.h"
#include "UI/ui_service_wakeup.h"

#define CAL_GRID_COLS                     8U
#define CAL_CELL_W                        16U
#define CAL_CELL_H                        22U
#define CAL_GRID_X                        0U
#define CAL_GRID_Y                        8U
#define CAL_OK_DISPLAY_TIME_MS            1000U
#define USER_CAL_MESSAGE_TIME_MS          1200U
#define CAL_STAGE_KEY_COUNT        12U
#define CAL_KEYBOARD_X             1U
#define CAL_KEYBOARD_Y             16U
#define CAL_WHITE_KEY_W            18U
#define CAL_WHITE_KEY_H            39U
#define CAL_BLACK_KEY_W            10U
#define CAL_BLACK_KEY_H            22U
#define CAL_WHITE_FILL_Y           (CAL_KEYBOARD_Y + CAL_BLACK_KEY_H + 3U)
#define CAL_VALID_FLASH_MS         360U
#define CAL_VALID_FLASH_PHASE_MS   90U
#define CAL_NO_FLASH_KEY           0xFFU

static uint32_t g_cal_done_tick = 0U;
static uint32_t g_user_message_tick = 0U;
static uint8_t g_calibration_return_page = UI_PAGE_TEMPLATE_CFG;
static uint8_t g_user_calibration_return_page = UI_PAGE_TEMPLATE_ENV;
static uint8_t g_cal_prev_done[HALL_KEY_COUNT];
static uint8_t g_cal_flash_key = CAL_NO_FLASH_KEY;
static uint32_t g_cal_flash_start_tick = 0U;

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
    (void)control_domain_request_calibration(CONTROL_CALIBRATION_START_HALL);
    g_cal_done_tick = 0U;
    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        g_cal_prev_done[i] = 0U;
    }
    g_cal_flash_key = CAL_NO_FLASH_KEY;
    g_cal_flash_start_tick = 0U;
}

static void ui_page_calibration_leave(void)
{
}

static void ui_page_calibration_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

void ui_page_calibration_process_presentation(uint8_t deadline_due)
{
    const uint32_t now_ms = HAL_GetTick();

    if (ui_page_get_id() == UI_PAGE_CALIBRATION)
    {
        if (ui_service_dirty_is_set() != 0U)
        {
            for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
            {
                const uint8_t done = hall_calibration_is_key_done(i);
                if ((done != 0U) && (g_cal_prev_done[i] == 0U))
                {
                    g_cal_flash_key = i;
                    g_cal_flash_start_tick = now_ms;
                }
                g_cal_prev_done[i] = done;
            }
        }

        if ((deadline_due != 0U) && (g_cal_flash_key != CAL_NO_FLASH_KEY)
            && ((now_ms - g_cal_flash_start_tick) >= CAL_VALID_FLASH_MS))
            g_cal_flash_key = CAL_NO_FLASH_KEY;

        if (hall_calibration_is_done() != 0U)
        {
            if (g_cal_done_tick == 0U)
                g_cal_done_tick = now_ms;
            else if ((deadline_due != 0U)
                     && ((now_ms - g_cal_done_tick) >= CAL_OK_DISPLAY_TIME_MS))
                ui_page_set(g_calibration_return_page);
        }
        return;
    }

    if (ui_page_get_id() == UI_PAGE_USER_CALIBRATION)
    {
        if (hall_user_calibration_is_done() == 0U)
        {
            g_user_message_tick = 0U;
            return;
        }
        if (g_user_message_tick == 0U)
        {
            g_user_message_tick = now_ms;
            return;
        }
        if ((deadline_due != 0U)
            && ((now_ms - g_user_message_tick) >= USER_CAL_MESSAGE_TIME_MS)
            && (hall_user_calibration_was_successful() != 0U))
            ui_page_set(g_user_calibration_return_page);
    }
}

uint8_t ui_page_calibration_next_deadline(uint32_t now_ms, uint32_t *out_deadline_ms)
{
    uint32_t deadline = 0U;
    if (ui_page_get_id() == UI_PAGE_CALIBRATION)
    {
        if (g_cal_flash_key != CAL_NO_FLASH_KEY)
            deadline = now_ms + CAL_VALID_FLASH_PHASE_MS;
        if ((hall_calibration_is_done() != 0U) && (g_cal_done_tick != 0U))
        {
            const uint32_t done_deadline = g_cal_done_tick + CAL_OK_DISPLAY_TIME_MS;
            if ((deadline == 0U) || ((int32_t)(done_deadline - deadline) < 0))
                deadline = done_deadline;
        }
    }
    else if ((ui_page_get_id() == UI_PAGE_USER_CALIBRATION)
             && (hall_user_calibration_is_done() != 0U)
             && (g_user_message_tick != 0U))
    {
        deadline = g_user_message_tick + USER_CAL_MESSAGE_TIME_MS;
    }
    if (deadline == 0U)
        return 0U;
    if (out_deadline_ms != 0)
        *out_deadline_ms = deadline;
    return 1U;
}

static uint8_t ui_page_calibration_flash_fill_visible(uint8_t key)
{
    uint32_t elapsed;

    if (key != g_cal_flash_key)
    {
        return 1U;
    }

    elapsed = HAL_GetTick() - g_cal_flash_start_tick;
    if (elapsed >= CAL_VALID_FLASH_MS)
    {
        return 1U;
    }

    return (((elapsed / CAL_VALID_FLASH_PHASE_MS) & 1UL) == 0UL) ? 1U : 0U;
}

static uint8_t ui_page_calibration_key_fill_height(uint8_t h,
                                                   uint8_t progress,
                                                   uint8_t done,
                                                   uint8_t flash_visible)
{
    uint8_t fill_h = 0U;

    if (done != 0U)
    {
        fill_h = (flash_visible != 0U) ? (uint8_t)(h - 2U) : 0U;
    }
    else if (progress != 0U)
    {
        fill_h = (uint8_t)(((uint16_t)(h - 2U) * progress) / 100U);
    }

    return fill_h;
}

static void ui_page_calibration_draw_white_fill(uint8_t x,
                                                uint8_t progress,
                                                uint8_t done,
                                                uint8_t flash_visible)
{
    const uint8_t fill_y = CAL_WHITE_FILL_Y;
    const uint8_t fill_h_max = (uint8_t)(CAL_KEYBOARD_Y
                               + CAL_WHITE_KEY_H
                               - fill_y
                               - 1U);
    const uint8_t fill_h = ui_page_calibration_key_fill_height(fill_h_max,
                                                               progress,
                                                               done,
                                                               flash_visible);

    if (fill_h == 0U)
    {
        return;
    }

    drv_display_fill_rect((uint8_t)(x + 3U),
                          (uint8_t)(fill_y + fill_h_max - fill_h),
                          (uint8_t)(CAL_WHITE_KEY_W - 6U),
                          fill_h);
}

static void ui_page_calibration_draw_black_key(uint8_t x,
                                               uint8_t key,
                                               uint8_t progress,
                                               uint8_t done)
{
    const uint8_t flash_visible = ui_page_calibration_flash_fill_visible(key);
    const uint8_t fill_h = ui_page_calibration_key_fill_height((uint8_t)(CAL_BLACK_KEY_H - 1U),
                                                               progress,
                                                               done,
                                                               flash_visible);

    drv_display_clear_rect(x,
                           CAL_KEYBOARD_Y,
                           CAL_BLACK_KEY_W,
                           CAL_BLACK_KEY_H);
    drv_display_draw_rect(x,
                          CAL_KEYBOARD_Y,
                          CAL_BLACK_KEY_W,
                          CAL_BLACK_KEY_H);

    if (fill_h != 0U)
    {
        drv_display_fill_rect((uint8_t)(x + 2U),
                              (uint8_t)(CAL_KEYBOARD_Y
                              + CAL_BLACK_KEY_H
                              - 1U
                              - fill_h),
                              (uint8_t)(CAL_BLACK_KEY_W - 4U),
                              fill_h);
    }

    drv_display_draw_line(x,
                          (uint8_t)(CAL_KEYBOARD_Y + CAL_BLACK_KEY_H),
                          (uint8_t)(x + CAL_BLACK_KEY_W - 1U),
                          (uint8_t)(CAL_KEYBOARD_Y + CAL_BLACK_KEY_H));
}

static void ui_page_calibration_render(void)
{
    static const uint8_t white_keys[7U] = {0U, 2U, 4U, 6U, 7U, 9U, 11U};
    static const uint8_t black_keys[5U] = {1U, 3U, 5U, 8U, 10U};
    static const uint8_t black_after_white[5U] = {1U, 2U, 3U, 5U, 6U};
    char title[20];
    const uint8_t stage = hall_calibration_get_stage();
    const uint8_t first_key = (uint8_t)(stage * CAL_STAGE_KEY_COUNT);
    const uint8_t keyboard_w = (uint8_t)(7U * CAL_WHITE_KEY_W);
    const uint8_t white_sep_y = (uint8_t)(CAL_WHITE_FILL_Y - 1U);
    const uint8_t white_bottom_y = (uint8_t)(CAL_KEYBOARD_Y + CAL_WHITE_KEY_H - 1U);

    (void)snprintf(title, sizeof(title), "CAL F-E %u/2", (unsigned)(stage + 1U));
    drv_display_draw_text(0U, 0U, title);

    drv_display_draw_rect(CAL_KEYBOARD_X,
                          CAL_KEYBOARD_Y,
                          keyboard_w,
                          CAL_WHITE_KEY_H);

    for (uint8_t white = 0U; white < 7U; white++)
    {
        const uint8_t key = (uint8_t)(first_key + white_keys[white]);
        const uint8_t x = (uint8_t)(CAL_KEYBOARD_X
                          + (white * CAL_WHITE_KEY_W));

        ui_page_calibration_draw_white_fill(x,
                                            hall_calibration_get_count(key),
                                            hall_calibration_is_key_done(key),
                                            ui_page_calibration_flash_fill_visible(key));
    }

    for (uint8_t white = 1U; white < 7U; white++)
    {
        const uint8_t x = (uint8_t)(CAL_KEYBOARD_X
                          + (white * CAL_WHITE_KEY_W));
        drv_display_draw_line(x, white_sep_y, x, white_bottom_y);
    }

    for (uint8_t black = 0U; black < 5U; black++)
    {
        const uint8_t key = (uint8_t)(first_key + black_keys[black]);
        const uint8_t boundary_x = (uint8_t)(CAL_KEYBOARD_X
                                   + (black_after_white[black] * CAL_WHITE_KEY_W));
        const uint8_t x = (uint8_t)(boundary_x - (CAL_BLACK_KEY_W / 2U));

        ui_page_calibration_draw_black_key(x,
                                           key,
                                           hall_calibration_get_count(key),
                                           hall_calibration_is_key_done(key));
    }

    if (hall_calibration_is_done() != 0U)
    {
        drv_display_draw_text(44U, 57U, "CAL OK");
    }
    else
    {
        drv_display_draw_text(36U, 57U, "HOLD KEYS");
    }
}

static void ui_page_user_calibration_enter(void)
{
    (void)control_domain_request_calibration(CONTROL_CALIBRATION_START_USER);
    g_user_message_tick = 0U;
}

static void ui_page_user_calibration_leave(void)
{
}

static void ui_page_user_calibration_handle_event(const ui_event_t *ev)
{
    (void)ev;
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
    .render = ui_page_calibration_render,
};

const ui_page_t g_ui_page_user_calibration = {
    .enter = ui_page_user_calibration_enter,
    .leave = ui_page_user_calibration_leave,
    .handle_event = ui_page_user_calibration_handle_event,
    .render = ui_page_user_calibration_render,
};

void ui_page_calibration_open(uint8_t return_page_id)
{
    g_calibration_return_page = (return_page_id < UI_PAGE_COUNT)
        ? return_page_id
        : UI_PAGE_TEMPLATE_CFG;
    ui_page_set(UI_PAGE_CALIBRATION);
}

void ui_page_user_calibration_open(uint8_t return_page_id)
{
    g_user_calibration_return_page = (return_page_id < UI_PAGE_COUNT)
        ? return_page_id
        : UI_PAGE_TEMPLATE_ENV;
    ui_page_set(UI_PAGE_USER_CALIBRATION);
}
