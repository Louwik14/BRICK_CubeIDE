#include "pages/ui_page_debug_hall.h"

#include <stdio.h>

#include "ui_renderer_oled.h"
#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_filter.h"
#include "App/Hall/hall_engine.h"

static uint16_t raw_min;
static uint16_t raw_max;

static uint16_t filt_min;
static uint16_t filt_max;

static void ui_page_debug_hall_enter(void)
{
    const uint16_t raw = hall_adc_get_raw(0U);
    const uint16_t filt = hall_filter_get(0U);

    raw_min = raw;
    raw_max = raw;

    filt_min = filt;
    filt_max = filt;
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

    char filt_txt[16];
    char filt_min_txt[16];

    char val_txt[16];
    char press_txt[16];
    char eng_min_txt[16];
    char eng_max_txt[16];

    const uint16_t raw = hall_adc_get_raw(0U);
    const uint16_t filt = hall_filter_get(0U);

    const uint16_t val = hall_engine_get_value(0U);
    const uint8_t pressed = hall_engine_is_pressed(0U);

    const uint16_t eng_min = hall_engine_get_min(0U);
    const uint16_t eng_max = hall_engine_get_max(0U);

    /* update RAW min/max */
    if(raw < raw_min)
        raw_min = raw;

    if(raw > raw_max)
        raw_max = raw;

    /* update FILT min/max */
    if(filt < filt_min)
        filt_min = filt;

    if(filt > filt_max)
        filt_max = filt;

    snprintf(raw_txt, sizeof(raw_txt), "%u", (unsigned)raw);
    snprintf(raw_min_txt, sizeof(raw_min_txt), "%u", (unsigned)raw_min);

    snprintf(filt_txt, sizeof(filt_txt), "%u", (unsigned)filt);
    snprintf(filt_min_txt, sizeof(filt_min_txt), "%u", (unsigned)filt_min);

    snprintf(val_txt, sizeof(val_txt), "%u%%", (unsigned)val);
    snprintf(press_txt, sizeof(press_txt), "%u", (unsigned)pressed);

    snprintf(eng_min_txt, sizeof(eng_min_txt), "%u", (unsigned)eng_min);
    snprintf(eng_max_txt, sizeof(eng_max_txt), "%u", (unsigned)eng_max);

    u8g2_t *u8g2 = &g_u8g2;

    u8g2_DrawStr(u8g2, 0U, 8U, "DEBUG - HALL");

    u8g2_DrawStr(u8g2, 0U, 18U, "RAW");
    u8g2_DrawStr(u8g2, 24U, 18U, raw_txt);
    u8g2_DrawStr(u8g2, 64U, 18U, raw_min_txt);

    u8g2_DrawStr(u8g2, 0U, 30U, "FILT");
    u8g2_DrawStr(u8g2, 24U, 30U, filt_txt);
    u8g2_DrawStr(u8g2, 64U, 30U, filt_min_txt);

    u8g2_DrawStr(u8g2, 0U, 42U, "VAL");
    u8g2_DrawStr(u8g2, 24U, 42U, val_txt);

    u8g2_DrawStr(u8g2, 64U, 42U, "P");
    u8g2_DrawStr(u8g2, 76U, 42U, press_txt);

    u8g2_DrawStr(u8g2, 0U, 54U, "ENG");
    u8g2_DrawStr(u8g2, 24U, 54U, eng_min_txt);
    u8g2_DrawStr(u8g2, 64U, 54U, eng_max_txt);

    u8g2_DrawFrame(u8g2, 0U, 56U, 127U, 8U);
    u8g2_DrawBox(u8g2, 1U, 57U, (uint8_t)((val * 125U) / 100U), 6U);
}

const ui_page_t g_ui_page_debug_hall = {
    .enter = ui_page_debug_hall_enter,
    .leave = ui_page_debug_hall_leave,
    .handle_event = ui_page_debug_hall_handle_event,
    .tick = ui_page_debug_hall_tick,
    .render = ui_page_debug_hall_render,
};
