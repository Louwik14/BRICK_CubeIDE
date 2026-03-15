#include "pages/ui_page_hall_key_debug.h"

#include <stdio.h>

#include "App/hall_kbd.h"
#include "drv_display.h"

enum
{
    HALL_KEY_DEBUG_FONT_HEIGHT_PX = 8U,
    HALL_KEY_DEBUG_KEY_INDEX = 0U,
};

static void ui_page_hall_key_debug_enter(void) {}

static void ui_page_hall_key_debug_leave(void) {}

static void ui_page_hall_key_debug_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

static void ui_page_hall_key_debug_tick(void) {}

static void ui_page_hall_key_debug_render(void)
{
    char line[24];
    const uint8_t key = HALL_KEY_DEBUG_KEY_INDEX;

    (void)snprintf(line, sizeof(line), "HALL KEY %u", (unsigned int)key);
    drv_display_draw_text(0U, 0U * HALL_KEY_DEBUG_FONT_HEIGHT_PX, line);

    (void)snprintf(line, sizeof(line), "RAW:  %4u", (unsigned int)hall_kbd_get_raw(key));
    drv_display_draw_text(0U, 1U * HALL_KEY_DEBUG_FONT_HEIGHT_PX, line);

    (void)snprintf(line, sizeof(line), "FILT: %4u", (unsigned int)hall_kbd_get_filtered(key));
    drv_display_draw_text(0U, 2U * HALL_KEY_DEBUG_FONT_HEIGHT_PX, line);

    (void)snprintf(line, sizeof(line), "MIN:  %4u", (unsigned int)hall_kbd_get_min(key));
    drv_display_draw_text(0U, 3U * HALL_KEY_DEBUG_FONT_HEIGHT_PX, line);

    (void)snprintf(line, sizeof(line), "MAX:  %4u", (unsigned int)hall_kbd_get_max(key));
    drv_display_draw_text(0U, 4U * HALL_KEY_DEBUG_FONT_HEIGHT_PX, line);

    (void)snprintf(line, sizeof(line), "THR:  %4u", (unsigned int)hall_kbd_get_threshold(key));
    drv_display_draw_text(0U, 5U * HALL_KEY_DEBUG_FONT_HEIGHT_PX, line);

    (void)snprintf(line, sizeof(line), "REL:%4u V:%3u",
                   (unsigned int)hall_kbd_get_hysteresis(key),
                   (unsigned int)hall_kbd_get_value(key));
    drv_display_draw_text(0U, 6U * HALL_KEY_DEBUG_FONT_HEIGHT_PX, line);

    (void)snprintf(line, sizeof(line), "VEL:%3u P:%u",
                   (unsigned int)hall_kbd_get_velocity(key),
                   (unsigned int)hall_kbd_is_pressed(key));
    drv_display_draw_text(0U, 7U * HALL_KEY_DEBUG_FONT_HEIGHT_PX, line);
}

const ui_page_t g_ui_page_hall_key_debug = {
    .enter = ui_page_hall_key_debug_enter,
    .leave = ui_page_hall_key_debug_leave,
    .handle_event = ui_page_hall_key_debug_handle_event,
    .tick = ui_page_hall_key_debug_tick,
    .render = ui_page_hall_key_debug_render,
};
