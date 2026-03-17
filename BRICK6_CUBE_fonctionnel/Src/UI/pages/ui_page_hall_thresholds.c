#include "pages/ui_page_hall_thresholds.h"

#include <stdio.h>

#include "drv_display.h"
#include "App/Hall/hall_engine.h"

static uint16_t ui_clamp_u16(int32_t value, uint16_t min_v, uint16_t max_v)
{
    if (value < (int32_t)min_v)
    {
        return min_v;
    }

    if (value > (int32_t)max_v)
    {
        return max_v;
    }

    return (uint16_t)value;
}

static void ui_page_hall_thresholds_enter(void) {}

static void ui_page_hall_thresholds_leave(void) {}

static void ui_page_hall_thresholds_handle_event(const ui_event_t *ev)
{
    if ((ev == 0) || (ev->type != UI_EVENT_ENCODER) || (ev->value == 0))
    {
        return;
    }

    if (ev->id == 0U)
    {
        const uint16_t current = hall_engine_get_threshold_ppm();
        const uint16_t next = ui_clamp_u16((int32_t)current + ((int32_t)ev->value * 5), 0U, 1000U);
        hall_engine_set_threshold_ppm(next);
    }
    else if (ev->id == 1U)
    {
        const uint16_t current = hall_engine_get_hyst_ppm();
        const uint16_t next = ui_clamp_u16((int32_t)current + ((int32_t)ev->value * 5), 0U, 1000U);
        hall_engine_set_hyst_ppm(next);
    }
    else if (ev->id == 2U)
    {
        const uint16_t current = hall_engine_get_time_fast_dt();
        const uint16_t next = ui_clamp_u16((int32_t)current + (int32_t)ev->value, 1U, 65534U);
        hall_engine_set_time_fast_dt(next);
    }
    else if (ev->id == 3U)
    {
        const uint16_t current = hall_engine_get_time_slow_dt();
        const uint16_t next = ui_clamp_u16((int32_t)current + (int32_t)ev->value, 2U, 65535U);
        hall_engine_set_time_slow_dt(next);
    }
}

static void ui_page_hall_thresholds_tick(void) {}

static void ui_page_hall_thresholds_render(void)
{
    char thr_txt[24];
    char hyst_txt[24];
    char fast_txt[24];
    char slow_txt[24];
    char trig_txt[28];

    const uint16_t thr = hall_engine_get_threshold_ppm();
    const uint16_t hyst = hall_engine_get_hyst_ppm();
    const uint16_t fast_dt = hall_engine_get_time_fast_dt();
    const uint16_t slow_dt = hall_engine_get_time_slow_dt();

    const uint16_t trig_lo = hall_engine_get_trig_lo(0U);
    const uint16_t trig_hi = hall_engine_get_trig_hi(0U);

    (void)snprintf(thr_txt, sizeof(thr_txt), "THR    %u", (unsigned)thr);
    (void)snprintf(hyst_txt, sizeof(hyst_txt), "HYST   %u", (unsigned)hyst);
    (void)snprintf(fast_txt, sizeof(fast_txt), "FAST   %u", (unsigned)fast_dt);
    (void)snprintf(slow_txt, sizeof(slow_txt), "SLOW   %u", (unsigned)slow_dt);
    (void)snprintf(trig_txt, sizeof(trig_txt), "TRIG   %u %u", (unsigned)trig_lo, (unsigned)trig_hi);

    drv_display_draw_text(0U, 0U, "HALL TRIG");
    drv_display_draw_text(0U, 12U, thr_txt);
    drv_display_draw_text(0U, 24U, hyst_txt);
    drv_display_draw_text(0U, 36U, fast_txt);
    drv_display_draw_text(0U, 48U, slow_txt);
    drv_display_draw_text(0U, 60U, trig_txt);
}

const ui_page_t g_ui_page_hall_thresholds = {
    .enter = ui_page_hall_thresholds_enter,
    .leave = ui_page_hall_thresholds_leave,
    .handle_event = ui_page_hall_thresholds_handle_event,
    .tick = ui_page_hall_thresholds_tick,
    .render = ui_page_hall_thresholds_render,
};
