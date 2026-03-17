#include "pages/ui_page_hall_velocity.h"

#include <stdio.h>

#include "drv_display.h"
#include "App/Hall/hall_engine.h"

static const char *ui_hall_velocity_mode_to_text(uint8_t mode)
{
    switch ((hall_vel_mode_t)mode)
    {
        case HALL_VEL_MODE_DV_PEAK:
            return "DV_PEAK";
        case HALL_VEL_MODE_TIME:
            return "TIME";
        case HALL_VEL_MODE_ENERGY:
            return "ENERGY";
        default:
            return "?";
    }
}

static const char *ui_hall_velocity_curve_to_text(uint8_t curve)
{
    switch ((hall_vel_curve_t)curve)
    {
        case HALL_VEL_CURVE_LINEAR:
            return "LINEAR";
        case HALL_VEL_CURVE_SOFT:
            return "SOFT";
        case HALL_VEL_CURVE_HARD:
            return "HARD";
        case HALL_VEL_CURVE_LOG:
            return "LOG";
        case HALL_VEL_CURVE_EXP:
            return "EXP";
        default:
            return "?";
    }
}

static void ui_page_hall_velocity_enter(void) {}

static void ui_page_hall_velocity_leave(void) {}

static void ui_page_hall_velocity_handle_event(const ui_event_t *ev)
{
    if ((ev == 0) || (ev->type != UI_EVENT_ENCODER) || (ev->value == 0))
    {
        return;
    }

    if (ev->id == 0U)
    {
        int16_t mode = (int16_t)hall_engine_get_velocity_mode();

        mode += ev->value;

        if (mode < 0)
        {
            mode = 0;
        }
        if (mode >= (int16_t)HALL_VEL_MODE_COUNT)
        {
            mode = (int16_t)HALL_VEL_MODE_COUNT - 1;
        }

        hall_engine_set_velocity_mode((uint8_t)mode);
    }
    else if (ev->id == 1U)
    {
        int16_t curve = (int16_t)hall_engine_get_velocity_curve();

        curve += ev->value;

        if (curve < 0)
        {
            curve = 0;
        }
        if (curve >= (int16_t)HALL_VEL_CURVE_COUNT)
        {
            curve = (int16_t)HALL_VEL_CURVE_COUNT - 1;
        }

        hall_engine_set_velocity_curve((uint8_t)curve);
    }
}

static void ui_page_hall_velocity_tick(void) {}

static void ui_page_hall_velocity_render(void)
{
    char mode_txt[24];
    char curve_txt[24];
    char vel_txt[24];

    const uint8_t mode = hall_engine_get_velocity_mode();
    const uint8_t curve = hall_engine_get_velocity_curve();
    const uint8_t vel = hall_engine_get_velocity(0U);

    (void)snprintf(mode_txt, sizeof(mode_txt), "MODE   %s", ui_hall_velocity_mode_to_text(mode));
    (void)snprintf(curve_txt, sizeof(curve_txt), "CURVE  %s", ui_hall_velocity_curve_to_text(curve));
    (void)snprintf(vel_txt, sizeof(vel_txt), "VEL    %u", (unsigned)vel);

    drv_display_draw_text(0U, 0U, "HALL VEL");
    drv_display_draw_text(0U, 14U, mode_txt);
    drv_display_draw_text(0U, 28U, curve_txt);
    drv_display_draw_text(0U, 42U, vel_txt);
}

const ui_page_t g_ui_page_hall_velocity = {
    .enter = ui_page_hall_velocity_enter,
    .leave = ui_page_hall_velocity_leave,
    .handle_event = ui_page_hall_velocity_handle_event,
    .tick = ui_page_hall_velocity_tick,
    .render = ui_page_hall_velocity_render,
};
