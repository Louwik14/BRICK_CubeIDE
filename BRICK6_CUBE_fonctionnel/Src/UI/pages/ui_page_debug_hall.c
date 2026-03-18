#include "pages/ui_page_debug_hall.h"

#include <stdio.h>

#include "drv_display.h"
#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_engine.h"

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

    char pos_txt[16];
    char press_txt[16];
    char vel_txt[16];
    char arm_trg_txt[24];
    char latched_txt[24];
    char elapsed_txt[24];

    hall_velocity_debug_t debug = {0};

    const uint16_t raw = hall_adc_get_raw(0U);
    const uint8_t pressed = hall_engine_is_pressed(0U);
    const uint16_t eng_min = hall_engine_get_min(0U);
    const uint16_t eng_max = hall_engine_get_max(0U);

    hall_engine_get_velocity_debug(0U, &debug);

    if(raw < raw_min)
        raw_min = raw;

    if(raw > raw_max)
        raw_max = raw;

    snprintf(raw_txt, sizeof(raw_txt), "%u", (unsigned)raw);
    snprintf(raw_min_txt, sizeof(raw_min_txt), "%u", (unsigned)raw_min);
    snprintf(raw_max_txt, sizeof(raw_max_txt), "%u", (unsigned)raw_max);

    snprintf(pos_txt, sizeof(pos_txt), "%u%%", (unsigned)debug.position_percent);
    snprintf(press_txt, sizeof(press_txt), "%u/%u", (unsigned)pressed, (unsigned)debug.state);
    snprintf(vel_txt, sizeof(vel_txt), "%u %c%c",
             (unsigned)debug.velocity_latched,
             (debug.velocity_ready != 0U) ? 'V' : '-',
             (debug.velocity_armed != 0U) ? 'A' : '-');
    snprintf(arm_trg_txt, sizeof(arm_trg_txt), "%u>%u",
             (unsigned)debug.velocity_arm_threshold,
             (unsigned)debug.trigger_threshold);
    snprintf(latched_txt, sizeof(latched_txt), "%u %u/%u",
             (unsigned)debug.raw_latched,
             (unsigned)eng_min,
             (unsigned)eng_max);
    snprintf(elapsed_txt, sizeof(elapsed_txt), "%lu samp",
             (unsigned long)debug.elapsed_samples_latched);

    drv_display_draw_text(0U, 0U, "DEBUG HALL VEL");

    drv_display_draw_text(0U, 10U, "RAW");
    drv_display_draw_text(28U, 10U, raw_txt);
    drv_display_draw_text(68U, 10U, raw_min_txt);
    drv_display_draw_text(100U, 10U, raw_max_txt);

    drv_display_draw_text(0U, 22U, "POS");
    drv_display_draw_text(28U, 22U, pos_txt);
    drv_display_draw_text(70U, 22U, press_txt);

    drv_display_draw_text(0U, 34U, "THR");
    drv_display_draw_text(28U, 34U, arm_trg_txt);

    drv_display_draw_text(0U, 46U, "LAT");
    drv_display_draw_text(28U, 46U, latched_txt);

    drv_display_draw_text(0U, 58U, "VEL");
    drv_display_draw_text(28U, 58U, vel_txt);
    drv_display_draw_text(76U, 58U, elapsed_txt);
}

const ui_page_t g_ui_page_debug_hall = {
    .enter = ui_page_debug_hall_enter,
    .leave = ui_page_debug_hall_leave,
    .handle_event = ui_page_debug_hall_handle_event,
    .tick = ui_page_debug_hall_tick,
    .render = ui_page_debug_hall_render,
};
