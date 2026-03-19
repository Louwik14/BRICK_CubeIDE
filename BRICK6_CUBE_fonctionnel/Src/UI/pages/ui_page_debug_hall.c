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
    char raw_txt[24];
    char pos_txt[24];
    char thr1_txt[24];
    char thr2_txt[24];
    char vel1_txt[24];
    char vel2_txt[24];

    hall_velocity_debug_t debug = {0};

    hall_engine_get_velocity_debug(0U, &debug);

    snprintf(raw_txt, sizeof(raw_txt), "%u %u/%u",
             (unsigned)debug.raw_current,
             (unsigned)debug.min_current,
             (unsigned)debug.max_current);
    snprintf(pos_txt, sizeof(pos_txt), "%u%% S%u %luus",
             (unsigned)debug.position_percent,
             (unsigned)debug.state,
             (unsigned long)((debug.velocity1_elapsed_samples != 0U) ?
                 (debug.velocity1_elapsed_samples * debug.sample_period_us) :
                 debug.sample_period_us));
    snprintf(thr1_txt, sizeof(thr1_txt), "V1 %u>%u %c%c",
             (unsigned)debug.velocity1_arm_threshold,
             (unsigned)debug.trigger1_threshold,
             (debug.velocity1_armed != 0U) ? 'A' : '-',
             (debug.velocity1_fallback != 0U) ? 'F' : '-');
    snprintf(thr2_txt, sizeof(thr2_txt), "V2 %u>%u %c%c",
             (unsigned)debug.velocity2_arm_threshold,
             (unsigned)debug.trigger2_threshold,
             (debug.velocity2_armed != 0U) ? 'A' : '-',
             (debug.velocity2_fallback != 0U) ? 'F' : '-');
    snprintf(vel1_txt, sizeof(vel1_txt), "%u %lu %u",
             (unsigned)debug.velocity_latched,
             (unsigned long)debug.velocity1_elapsed_samples,
             (unsigned)debug.velocity1_raw_latched);
    snprintf(vel2_txt, sizeof(vel2_txt), "%u %lu %u",
             (unsigned)debug.velocity2_latched,
             (unsigned long)debug.velocity2_elapsed_samples,
             (unsigned)debug.velocity2_raw_latched);

    drv_display_draw_text(0U, 0U, "DEBUG HALL VEL");
    drv_display_draw_text(0U, 10U, raw_txt);
    drv_display_draw_text(0U, 20U, pos_txt);
    drv_display_draw_text(0U, 30U, thr1_txt);
    drv_display_draw_text(0U, 40U, thr2_txt);
    drv_display_draw_text(0U, 50U, vel1_txt);
    drv_display_draw_text(0U, 60U, vel2_txt);
}

const ui_page_t g_ui_page_debug_hall = {
    .enter = ui_page_debug_hall_enter,
    .leave = ui_page_debug_hall_leave,
    .handle_event = ui_page_debug_hall_handle_event,
    .tick = ui_page_debug_hall_tick,
    .render = ui_page_debug_hall_render,
};
