#include "ui_widgets.h"

#include <string.h>

#include "drv_display.h"
#include "font.h"

#define UIW_KNOB_OUTER_RADIUS     10
#define UIW_KNOB_INDICATOR_RADIUS 10
#define UIW_KNOB_INDICATOR_SUBSTEPS 4
#define UIW_KNOB_VECTOR_SCALE     9
#define UIW_KNOB_MARGIN           0

#define UIW_SWITCH_W              18
#define UIW_SWITCH_H              8
#define UIW_ENUM_BOX_H            12
#define UIW_JACK_RING_OFFSET_Y    4
#define UIW_KEYBOARD_H            12
#define UIW_SHAPE_H               16

static int uiw_min_int(int a, int b)
{
    return (a < b) ? a : b;
}

static void uiw_draw_point(int x, int y)
{
    drv_display_draw_line(x, y, x, y);
}

static int uiw_scale_ratio(int value, int numerator, int denominator)
{
    const int half = denominator / 2;

    if (value < 0)
    {
        return -(((-value) * numerator + half) / denominator);
    }

    return (value * numerator + half) / denominator;
}

static void uiw_draw_circle_points(int cx, int cy, int x, int y)
{
    uiw_draw_point(cx + x, cy + y);
    uiw_draw_point(cx - x, cy + y);
    uiw_draw_point(cx + x, cy - y);
    uiw_draw_point(cx - x, cy - y);
    uiw_draw_point(cx + y, cy + x);
    uiw_draw_point(cx - y, cy + x);
    uiw_draw_point(cx + y, cy - x);
    uiw_draw_point(cx - y, cy - x);
}

static void uiw_draw_circle_outline(int cx, int cy, int radius)
{
    int x = 0;
    int y = radius;
    int d = 1 - radius;

    while (x <= y)
    {
        uiw_draw_circle_points(cx, cy, x, y);
        x++;
        if (d < 0)
        {
            d += (2 * x) + 1;
        }
        else
        {
            y--;
            d += (2 * (x - y)) + 1;
        }
    }
}

static int uiw_center_x(int x, int w, const char *txt)
{
    const int text_w = (int)drv_display_text_width(txt);
    int out = x + ((w - text_w) / 2);
    if (out < (x + 1))
    {
        out = x + 1;
    }
    return out;
}

static char uiw_ascii_tolower(char c)
{
    if ((c >= 'A') && (c <= 'Z'))
    {
        return (char)(c - 'A' + 'a');
    }

    return c;
}

static uint8_t uiw_label_starts_with(const char *label, const char *prefix)
{
    if ((label == NULL) || (prefix == NULL))
    {
        return 0U;
    }

    while (*prefix != '\0')
    {
        if (uiw_ascii_tolower(*label) != uiw_ascii_tolower(*prefix))
        {
            return 0U;
        }

        if (*label == '\0')
        {
            return 0U;
        }

        label++;
        prefix++;
    }

    return 1U;
}

void uiw_draw_knob(int x, int y, int w, int h, float value, float vmin, float vmax)
{
    static const int8_t indicator_x[33] = {-5, -7, -7, -8, -8, -9, -9, -9, -8, -8, -7, -7, -5, -4, -3, -1, 0,
                                            1, 3, 4, 5, 7, 7, 8, 8, 9, 9, 9, 8, 8, 7, 7, 5};
    static const int8_t indicator_y[33] = {5, 5, 4, 3, 1, 0, -1, -1, -3, -4, -5, -5, -7, -7, -8, -9, -9,
                                           -9, -8, -7, -7, -5, -5, -4, -3, -1, -1, 0, 1, 3, 4, 5, 5};

    const int cx = x + (w / 2);
    const int cy = y + (h / 2);
    int outer_radius = UIW_KNOB_OUTER_RADIUS;
    int indicator_radius = UIW_KNOB_INDICATOR_RADIUS;
    int indicator_end_x = 0;
    int indicator_end_y = 0;
    int indicator_step = 16 * UIW_KNOB_INDICATOR_SUBSTEPS;
    int indicator_vec_x = 0;
    int indicator_vec_y = -9 * UIW_KNOB_INDICATOR_SUBSTEPS;
    const int max_radius = uiw_min_int(w, h) / 2 - UIW_KNOB_MARGIN;

    if (outer_radius > max_radius)
    {
        outer_radius = max_radius;
    }
    if (outer_radius < 2)
    {
        outer_radius = 2;
    }
    if (indicator_radius > outer_radius)
    {
        indicator_radius = outer_radius;
    }
    if (indicator_radius < 1)
    {
        indicator_radius = 1;
    }

    uiw_draw_circle_outline(cx, cy, outer_radius);

    if (vmax <= vmin)
    {
        indicator_step = 16 * UIW_KNOB_INDICATOR_SUBSTEPS;
    }
    else
    {
        float norm = (value - vmin) / (vmax - vmin);
        if (norm < 0.0f)
        {
            norm = 0.0f;
        }
        if (norm > 1.0f)
        {
            norm = 1.0f;
        }
        indicator_step = (int)(norm * (float)(32 * UIW_KNOB_INDICATOR_SUBSTEPS) + 0.5f);
    }

    if (indicator_step >= (32 * UIW_KNOB_INDICATOR_SUBSTEPS))
    {
        indicator_vec_x = indicator_x[32] * UIW_KNOB_INDICATOR_SUBSTEPS;
        indicator_vec_y = indicator_y[32] * UIW_KNOB_INDICATOR_SUBSTEPS;
    }
    else
    {
        const int base = indicator_step / UIW_KNOB_INDICATOR_SUBSTEPS;
        const int frac = indicator_step - (base * UIW_KNOB_INDICATOR_SUBSTEPS);
        indicator_vec_x = (indicator_x[base] * (UIW_KNOB_INDICATOR_SUBSTEPS - frac))
                          + (indicator_x[base + 1] * frac);
        indicator_vec_y = (indicator_y[base] * (UIW_KNOB_INDICATOR_SUBSTEPS - frac))
                          + (indicator_y[base + 1] * frac);
    }

    indicator_end_x = uiw_scale_ratio(indicator_vec_x,
                                      indicator_radius,
                                      UIW_KNOB_VECTOR_SCALE * UIW_KNOB_INDICATOR_SUBSTEPS);
    indicator_end_y = uiw_scale_ratio(indicator_vec_y,
                                      indicator_radius,
                                      UIW_KNOB_VECTOR_SCALE * UIW_KNOB_INDICATOR_SUBSTEPS);
    drv_display_draw_line(cx, cy, cx + indicator_end_x, cy + indicator_end_y);
    drv_display_draw_rect(cx - 1, cy - 1, 3, 3);
}

void uiw_draw_switch(int x, int y, int w, int h, uint8_t on)
{
    const int sx = x + ((w - UIW_SWITCH_W) / 2);
    const int sy = y + ((h - UIW_SWITCH_H) / 2);

    drv_display_draw_rect(sx, sy, UIW_SWITCH_W, UIW_SWITCH_H);
    if (on != 0U)
    {
        drv_display_fill_rect(sx + UIW_SWITCH_W - 7, sy + 1, 5, UIW_SWITCH_H - 2);
    }
    else
    {
        drv_display_fill_rect(sx + 2, sy + 1, 5, UIW_SWITCH_H - 2);
    }
}

void uiw_draw_wave_icon(int x, int y, int w, int h, const char *label)
{
    const int shape_h = (h > UIW_SHAPE_H) ? UIW_SHAPE_H : h;
    const int lx = x + 4;
    const int rx = x + w - 5;
    const int mid = x + (w / 2);
    const int top = y + ((h - shape_h) / 2);
    const int bot = top + shape_h - 1;

    if ((label != NULL) && (uiw_label_starts_with(label, "Tri") != 0U))
    {
        drv_display_draw_line(lx, bot, mid, top);
        drv_display_draw_line(mid, top, rx, bot);
    }
    else if ((label != NULL) && (uiw_label_starts_with(label, "Saw") != 0U))
    {
        drv_display_draw_line(lx, bot, rx - 4, top);
        drv_display_draw_line(rx - 4, top, rx - 4, bot);
        drv_display_draw_line(rx - 4, bot, rx, bot);
    }
    else if ((label != NULL) && ((uiw_label_starts_with(label, "Sin") != 0U) || (uiw_label_starts_with(label, "Sine") != 0U)))
    {
        drv_display_draw_line(lx, bot, lx + 4, top + 4);
        drv_display_draw_line(lx + 4, top + 4, mid, top);
        drv_display_draw_line(mid, top, rx - 4, bot - 4);
        drv_display_draw_line(rx - 4, bot - 4, rx, bot);
    }
    else
    {
        drv_display_draw_line(lx, bot, lx, top);
        drv_display_draw_line(lx, top, mid, top);
        drv_display_draw_line(mid, top, mid, bot);
        drv_display_draw_line(mid, bot, rx, bot);
    }
}

void uiw_draw_filter_icon(int x, int y, int w, int h, const char *label)
{
    const int shape_h = (h > UIW_SHAPE_H) ? UIW_SHAPE_H : h;
    const int lx = x + 4;
    const int rx = x + w - 5;
    const int top = y + ((h - shape_h) / 2);
    const int bot = top + shape_h - 1;

    if ((label != NULL) && ((uiw_label_starts_with(label, "HP") != 0U) || (uiw_label_starts_with(label, "High") != 0U)))
    {
        drv_display_draw_line(lx, bot, lx + 5, bot);
        drv_display_draw_line(lx + 5, bot, rx, top);
    }
    else if ((label != NULL) && ((uiw_label_starts_with(label, "BP") != 0U) || (uiw_label_starts_with(label, "Band") != 0U)))
    {
        drv_display_draw_line(lx, bot, x + (w / 2), top + 3);
        drv_display_draw_line(x + (w / 2), top + 3, rx, bot);
    }
    else
    {
        drv_display_draw_line(lx, top, rx - 5, bot);
        drv_display_draw_line(rx - 5, bot, rx, bot);
    }
}

void uiw_draw_enum_text(int x, int y, int w, int h, const char *label)
{
    const char *display_label = ((label != NULL) && (label[0] != '\0')) ? label : "-";
    const int cy = y + (h / 2);
    const int arrow_y = cy - 1;
    const int box_x = x + 3;
    const int box_w = w - 6;
    const int box_y = y + ((h - UIW_ENUM_BOX_H) / 2);
    const font_t *font = &FONT_5X7;

    drv_display_set_font(font);
    if (drv_display_text_width(display_label) > (uint8_t)(box_w - 4))
    {
        font = &FONT_4X6;
        drv_display_set_font(font);
    }

    drv_display_draw_line(x + 1, arrow_y, x + 3, arrow_y - 2);
    drv_display_draw_line(x + 1, arrow_y, x + 3, arrow_y + 2);
    drv_display_draw_line(x + w - 2, arrow_y, x + w - 4, arrow_y - 2);
    drv_display_draw_line(x + w - 2, arrow_y, x + w - 4, arrow_y + 2);

    drv_display_draw_rect(box_x, box_y, box_w, UIW_ENUM_BOX_H);
    drv_display_fill_rect(box_x + 1, box_y + 1, box_w - 2, UIW_ENUM_BOX_H - 2);

    drv_display_set_font(font);
    drv_display_draw_text_inverted((uint8_t)uiw_center_x(box_x, box_w, display_label),
                                   (uint8_t)(box_y + ((UIW_ENUM_BOX_H - drv_display_font_height()) / 2)),
                                   display_label);

    drv_display_draw_line(box_x + 2, box_y + UIW_ENUM_BOX_H + 2, box_x + box_w - 3, box_y + UIW_ENUM_BOX_H + 2);
}

void uiw_draw_jack_icon(int x, int y, int w, int h)
{
    const int cx = x + (w / 2);
    const int cy = y + (h / 2);
    const int ring_y = cy + UIW_JACK_RING_OFFSET_Y;
    const int top = ring_y - 12;

    drv_display_draw_line(cx, top, cx, ring_y - 4);
    drv_display_draw_line(cx - 2, top + 3, cx + 2, top + 3);
    drv_display_draw_line(cx - 3, ring_y - 4, cx + 3, ring_y - 4);
    drv_display_draw_rect(cx - 4, ring_y - 4, 8, 8);
    drv_display_draw_rect(cx - 2, ring_y - 2, 4, 4);
    drv_display_draw_line(cx - 6, ring_y + 6, cx + 6, ring_y + 6);
}

void uiw_draw_keyboard_icon(int x, int y, int w, int h)
{
    const int key_x = x + 4;
    const int key_y = y + ((h - UIW_KEYBOARD_H) / 2);
    const int key_w = w - 8;
    const int white_w = key_w / 5;

    drv_display_draw_rect(key_x, key_y, key_w, UIW_KEYBOARD_H);

    for (int i = 1; i < 5; i++)
    {
        drv_display_draw_line(key_x + (i * white_w), key_y + 5, key_x + (i * white_w), key_y + UIW_KEYBOARD_H - 1);
    }

    drv_display_fill_rect(key_x + 2, key_y, 3, 6);
    drv_display_fill_rect(key_x + white_w + 1, key_y, 3, 6);
    drv_display_fill_rect(key_x + (3 * white_w) - 1, key_y, 3, 6);
}

uiw_widget_type_t uiw_pick_widget_type(const param_desc_t *desc, const char *enum_label)
{
    if (desc == NULL)
    {
        return UIW_WIDGET_NONE;
    }

    if (desc->display_type == PARAM_DISPLAY_BOOL)
    {
        return UIW_WIDGET_SWITCH;
    }

    if (desc->display_type == PARAM_DISPLAY_ENUM)
    {
        if ((enum_label != NULL)
                && ((uiw_label_starts_with(enum_label, "Saw") != 0U)
                    || (uiw_label_starts_with(enum_label, "Tri") != 0U)
                    || (uiw_label_starts_with(enum_label, "Sin") != 0U)
                    || (uiw_label_starts_with(enum_label, "Sine") != 0U)
                    || (uiw_label_starts_with(enum_label, "Pulse") != 0U)
                    || (uiw_label_starts_with(enum_label, "Square") != 0U)))
        {
            return UIW_WIDGET_WAVE_ICON;
        }

        if ((enum_label != NULL)
                && ((uiw_label_starts_with(enum_label, "LP") != 0U)
                    || (uiw_label_starts_with(enum_label, "HP") != 0U)
                    || (uiw_label_starts_with(enum_label, "BP") != 0U)
                    || (uiw_label_starts_with(enum_label, "Low") != 0U)
                    || (uiw_label_starts_with(enum_label, "High") != 0U)
                    || (uiw_label_starts_with(enum_label, "Band") != 0U)))
        {
            return UIW_WIDGET_FILTER_ICON;
        }
    }

    return UIW_WIDGET_KNOB;
}
