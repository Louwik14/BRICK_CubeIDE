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
    char line0[24];
    char line1[24];
    char line2[24];
    char line3[24];
    char line4[24];
    char line5[24];
    char line6[24];
    hall_velocity_debug_t debug = {0};

    hall_engine_get_velocity_debug(0U, &debug);

    snprintf(line0, sizeof(line0), "HALL K0 C%u M%uC%u",
             (unsigned)debug.calibrated,
             (unsigned)debug.velocity_mode,
             (unsigned)debug.velocity_curve);
    snprintf(line1, sizeof(line1), "R%u %u/%u",
             (unsigned)debug.raw_current,
             (unsigned)debug.min_current,
             (unsigned)debug.max_current);
    snprintf(line2, sizeof(line2), "T%u %u RV%u O%u",
             (unsigned)debug.trig_lo,
             (unsigned)debug.trig_hi,
             (unsigned)debug.range_valid,
             (unsigned)debug.state);
    snprintf(line3, sizeof(line3), "P%u%% V%u/%u",
             (unsigned)debug.position_percent,
             (unsigned)debug.velocity,
             (unsigned)debug.velocity_valid);
    snprintf(line4, sizeof(line4), "DV%u SD%u",
             (unsigned)debug.dv_peak,
             (unsigned)debug.sum_dv);
    snprintf(line5, sizeof(line5), "TS%u TE%u",
             (unsigned)debug.vel_start_th,
             (unsigned)debug.vel_end_th);
    snprintf(line6, sizeof(line6), "TC%u A%u N%u/%u",
             (unsigned)debug.time_count,
             (unsigned)debug.time_active,
             (unsigned)debug.note_on_pending,
             (unsigned)debug.note_off_pending);

    drv_display_draw_text(0U, 0U, line0);
    drv_display_draw_text(0U, 10U, line1);
    drv_display_draw_text(0U, 20U, line2);
    drv_display_draw_text(0U, 30U, line3);
    drv_display_draw_text(0U, 40U, line4);
    drv_display_draw_text(0U, 50U, line5);
    drv_display_draw_text(0U, 60U, line6);
}

const ui_page_t g_ui_page_debug_hall = {
    .enter = ui_page_debug_hall_enter,
    .leave = ui_page_debug_hall_leave,
    .handle_event = ui_page_debug_hall_handle_event,
    .tick = ui_page_debug_hall_tick,
    .render = ui_page_debug_hall_render,
};
