#include "pages/ui_page_debug_hall.h"

#include <stdio.h>

#include "drv_display.h"
#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_velocity.h"

static uint16_t raw_min;
static uint16_t raw_max;

static void ui_page_debug_hall_enter(void)
{
    const uint16_t raw = hall_adc_get_raw(0U);

    raw_min = raw;
    raw_max = raw;
}

static void ui_page_debug_hall_leave(void) {}

static void ui_page_debug_hall_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

static void ui_page_debug_hall_tick(void) {}

static void ui_page_debug_hall_render(void)
{
    char raw_txt[16];
    char raw_min_txt[16];
    char raw_max_txt[16];

    char val_txt[16];
    char press_txt[16];
    char vel_txt[16];

    char eng_min_txt[16];
    char eng_max_txt[16];

    const uint16_t raw = hall_adc_get_raw(0U);

    const uint16_t val = hall_engine_get_value(0U);
    const uint8_t pressed = hall_engine_is_pressed(0U);
    const uint8_t velocity = hall_velocity_get(0U);

    const uint16_t eng_min = hall_engine_get_min(0U);
    const uint16_t eng_max = hall_engine_get_max(0U);

    if(raw < raw_min)
        raw_min = raw;

    if(raw > raw_max)
        raw_max = raw;

    snprintf(raw_txt, sizeof(raw_txt), "%u", (unsigned)raw);
    snprintf(raw_min_txt, sizeof(raw_min_txt), "%u", (unsigned)raw_min);
    snprintf(raw_max_txt, sizeof(raw_max_txt), "%u", (unsigned)raw_max);

    snprintf(val_txt, sizeof(val_txt), "%u%%", (unsigned)val);
    snprintf(press_txt, sizeof(press_txt), "%u", (unsigned)pressed);
    snprintf(vel_txt, sizeof(vel_txt), "%u", (unsigned)velocity);

    snprintf(eng_min_txt, sizeof(eng_min_txt), "%u", (unsigned)eng_min);
    snprintf(eng_max_txt, sizeof(eng_max_txt), "%u", (unsigned)eng_max);

    drv_display_draw_text(0U, 0U, "DEBUG - HALL");

    drv_display_draw_text(0U, 12U, "RAW");
    drv_display_draw_text(0U, 22U, raw_txt);
    drv_display_draw_text(40U, 22U, raw_min_txt);
    drv_display_draw_text(80U, 22U, raw_max_txt);

    drv_display_draw_text(0U, 34U, "VAL");
    drv_display_draw_text(30U, 34U, val_txt);

    drv_display_draw_text(60U, 34U, "P");
    drv_display_draw_text(75U, 34U, press_txt);

    drv_display_draw_text(0U, 46U, "VEL");
    drv_display_draw_text(40U, 46U, vel_txt);

    drv_display_draw_text(0U, 58U, "ENG");
    drv_display_draw_text(40U, 58U, eng_min_txt);
    drv_display_draw_text(80U, 58U, eng_max_txt);
}

const ui_page_t g_ui_page_debug_hall = {
    .enter = ui_page_debug_hall_enter,
    .leave = ui_page_debug_hall_leave,
    .handle_event = ui_page_debug_hall_handle_event,
    .tick = ui_page_debug_hall_tick,
    .render = ui_page_debug_hall_render,
};
