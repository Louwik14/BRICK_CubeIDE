#include "pages/ui_page_debug_hall.h"

#include <stdio.h>

#include "ui_display.h"
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

    draw_text(0U, 1U, "DEBUG - HALL");

    draw_text(0U, 11U, "RAW");
    draw_text(24U, 11U, raw_txt);
    draw_text(64U, 11U, raw_min_txt);

    draw_text(0U, 23U, "FILT");
    draw_text(24U, 23U, filt_txt);
    draw_text(64U, 23U, filt_min_txt);

    draw_text(0U, 35U, "VAL");
    draw_text(24U, 35U, val_txt);

    draw_text(64U, 35U, "P");
    draw_text(76U, 35U, press_txt);

    draw_text(0U, 47U, "ENG");
    draw_text(24U, 47U, eng_min_txt);
    draw_text(64U, 47U, eng_max_txt);

    draw_rect(0U, 56U, 127U, 8U);
    fill_rect(1U, 57U, (uint8_t)((val * 125U) / 100U), 6U);
}

const ui_page_t g_ui_page_debug_hall = {
    .enter = ui_page_debug_hall_enter,
    .leave = ui_page_debug_hall_leave,
    .handle_event = ui_page_debug_hall_handle_event,
    .tick = ui_page_debug_hall_tick,
    .render = ui_page_debug_hall_render,
};
