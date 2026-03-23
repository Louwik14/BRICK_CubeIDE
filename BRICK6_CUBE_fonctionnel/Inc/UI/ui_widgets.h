#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include <stdint.h>

#include "param_registry.h"

typedef enum
{
    UIW_WIDGET_NONE = 0,
    UIW_WIDGET_EMPTY,
    UIW_WIDGET_KNOB,
    UIW_WIDGET_SWITCH,
    UIW_WIDGET_ENUM_TEXT,
    UIW_WIDGET_WAVE_ICON,
    UIW_WIDGET_FILTER_ICON,
    UIW_WIDGET_JACK,
    UIW_WIDGET_KEYBOARD,
} uiw_widget_type_t;

void uiw_draw_knob(int x, int y, int w, int h, int value, int vmin, int vmax);
void uiw_draw_switch(int x, int y, int w, int h, uint8_t on);
void uiw_draw_wave_icon(int x, int y, int w, int h, const char *label);
void uiw_draw_filter_icon(int x, int y, int w, int h, const char *label);
void uiw_draw_enum_text(int x, int y, int w, int h, const char *label);
void uiw_draw_jack_icon(int x, int y, int w, int h);
void uiw_draw_keyboard_icon(int x, int y, int w, int h);

uiw_widget_type_t uiw_pick_widget_type(const param_desc_t *desc, const char *enum_label);

#endif /* UI_WIDGETS_H */
