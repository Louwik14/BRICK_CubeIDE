#include "pages/ui_page_debug_hall.h"

#include <stdio.h>

#include "App/Hall/hall_engine.h"
#include "drv_display.h"

#define UI_HALL_DEBUG_KEY 0U

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

    const uint8_t key = UI_HALL_DEBUG_KEY;
    const uint16_t raw = hall_engine_get_raw(key);
    const uint16_t filtered = hall_engine_get_filtered(key);
    const uint16_t cal_min = hall_engine_get_cal_min(key);
    const uint16_t cal_max = hall_engine_get_cal_max(key);
    const uint16_t obs_min = hall_engine_get_observed_min(key);
    const uint16_t obs_max = hall_engine_get_observed_max(key);
    const uint16_t vel_start = hall_engine_get_velocity_start(key);
    const uint16_t trig_release = hall_engine_get_trigger_release(key);
    const uint16_t trig_press = hall_engine_get_trigger_press(key);
    const int16_t derivative = hall_engine_get_derivative(key);
    const uint16_t attack_samples = hall_engine_get_attack_samples(key);
    const uint8_t velocity = hall_engine_get_velocity(key);
    const uint8_t pressed = hall_engine_get_pressed(key);
    const uint8_t valid = hall_engine_get_range_valid(key);

    snprintf(line1, sizeof(line1), "R%u F%u", (unsigned)raw, (unsigned)filtered);
    snprintf(line2, sizeof(line2), "C%u-%u", (unsigned)cal_min, (unsigned)cal_max);
    snprintf(line3, sizeof(line3), "O%u-%u %s", (unsigned)obs_min, (unsigned)obs_max,
             (valid != 0U) ? "OK" : "NO");
    snprintf(line4, sizeof(line4), "V%u RL%u PH%u", (unsigned)vel_start,
             (unsigned)trig_release, (unsigned)trig_press);
    snprintf(line5, sizeof(line5), "P%u D%d A%u V%u", (unsigned)pressed, (int)derivative,
             (unsigned)attack_samples, (unsigned)velocity);

    drv_display_draw_text(0U, 0U, "HALL DEBUG K0");
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
