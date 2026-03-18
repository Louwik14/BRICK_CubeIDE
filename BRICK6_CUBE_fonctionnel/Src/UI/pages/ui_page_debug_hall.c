#include "pages/ui_page_debug_hall.h"

#include <stdio.h>

#include "drv_display.h"
#include "App/Hall/hall_engine.h"

static void ui_page_debug_hall_enter(void) {}

static void ui_page_debug_hall_leave(void) {}

static void ui_page_debug_hall_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

static void ui_page_debug_hall_tick(void) {}

static void ui_page_debug_hall_render(void)
{
    char line1[24];
    char line2[24];
    char line3[24];
    char line4[24];
    char line5[24];

    const uint16_t raw = hall_engine_get_debug_latched_raw(0U);
    const uint16_t prev_raw = hall_engine_get_debug_latched_prev_raw(0U);
    const uint16_t dv_peak = hall_engine_get_debug_latched_dv_peak(0U);
    const uint16_t sum_dv = hall_engine_get_debug_latched_sum_dv(0U);
    const uint16_t time_count = hall_engine_get_debug_latched_time_count(0U);
    const uint8_t velocity = hall_engine_get_velocity_latched(0U);
    const uint16_t trig_lo = hall_engine_get_debug_latched_trig_lo(0U);
    const uint16_t trig_hi = hall_engine_get_debug_latched_trig_hi(0U);
    const uint16_t vel_start = hall_engine_get_debug_latched_vel_start_th(0U);
    const uint32_t sample_count = hall_engine_get_debug_latched_sample_count(0U);

    snprintf(line1, sizeof(line1), "LR%u PR%u", (unsigned)raw, (unsigned)prev_raw);
    snprintf(line2, sizeof(line2), "DV%u SU%u", (unsigned)dv_peak, (unsigned)sum_dv);
    snprintf(line3, sizeof(line3), "TC%u VL%u", (unsigned)time_count, (unsigned)velocity);
    snprintf(line4, sizeof(line4), "TL%u TH%u", (unsigned)trig_lo, (unsigned)trig_hi);
    snprintf(line5, sizeof(line5), "VS%u NS%lu", (unsigned)vel_start, (unsigned long)sample_count);

    drv_display_draw_text(0U, 0U, "DEBUG HALL K0");
    drv_display_draw_text(0U, 12U, line1);
    drv_display_draw_text(0U, 24U, line2);
    drv_display_draw_text(0U, 36U, line3);
    drv_display_draw_text(0U, 48U, line4);
    drv_display_draw_text(0U, 58U, line5);
}

const ui_page_t g_ui_page_debug_hall = {
    .enter = ui_page_debug_hall_enter,
    .leave = ui_page_debug_hall_leave,
    .handle_event = ui_page_debug_hall_handle_event,
    .tick = ui_page_debug_hall_tick,
    .render = ui_page_debug_hall_render,
};
