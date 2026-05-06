#include "ui_widgets.h"

#include <string.h>

#include "drv_display.h"
#include "font.h"

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
    static const int8_t outline_x[16] = {0, 3, 5, 6, 7, 6, 5, 3, 0, -3, -5, -6, -7, -6, -5, -3};
    static const int8_t outline_y[16] = {-7, -6, -5, -3, 0, 3, 5, 6, 7, 6, 5, 3, 0, -3, -5, -6};
    static const int8_t indicator_x[33] = {-4, -5, -5, -6, -6, -6, -6, -6, -6, -5, -5, -4, -3, -3, -2, -1, 0,
                                            1, 2, 3, 3, 4, 5, 5, 6, 6, 6, 6, 6, 6, 5, 5, 4};
    static const int8_t indicator_y[33] = {4, 4, 3, 2, 1, 0, -1, -1, -2, -3, -4, -4, -5, -5, -6, -6, -6,
                                           -6, -6, -5, -5, -4, -4, -3, -2, -1, -1, 0, 1, 2, 3, 4, 4};

    const int cx = x + (w / 2);
    const int cy = y + 18;
    int idx = 16;

    (void)h;

    for (uint8_t i = 0U; i < 16U; i++)
    {
        const uint8_t next = (uint8_t)((i + 1U) & 15U);
        drv_display_draw_line(cx + outline_x[i],
                              cy + outline_y[i],
                              cx + outline_x[next],
                              cy + outline_y[next]);
    }

    if (vmax <= vmin)
    {
        idx = 16;
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
        idx = (int)(norm * 32.0f + 0.5f);
    }

    drv_display_draw_line(cx, cy, cx + indicator_x[idx], cy + indicator_y[idx]);
    drv_display_draw_rect(cx - 1, cy - 1, 3, 3);
}

void uiw_draw_switch(int x, int y, int w, int h, uint8_t on)
{
    const int sw_w = 18;
    const int sw_h = 8;
    const int sx = x + ((w - sw_w) / 2);
    const int sy = y + 13;

    (void)h;

    drv_display_draw_rect(sx, sy, sw_w, sw_h);
    if (on != 0U)
    {
        drv_display_fill_rect(sx + sw_w - 7, sy + 1, 5, sw_h - 2);
    }
    else
    {
        drv_display_fill_rect(sx + 2, sy + 1, 5, sw_h - 2);
    }
}

void uiw_draw_wave_icon(int x, int y, int w, int h, const char *label)
{
    const int lx = x + 5;
    const int rx = x + w - 6;
    const int mid = x + (w / 2);
    const int top = y + 12;
    const int bot = y + h - 14;

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
    const int lx = x + 5;
    const int rx = x + w - 6;
    const int top = y + 12;
    const int bot = y + h - 14;

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
    const int box_y = y + 12;
    const int box_h = 12;
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

    drv_display_draw_rect(box_x, box_y, box_w, box_h);
    drv_display_fill_rect(box_x + 1, box_y + 1, box_w - 2, box_h - 2);

    drv_display_set_font(font);
    drv_display_draw_text_inverted((uint8_t)uiw_center_x(box_x, box_w, display_label),
                                   (uint8_t)(box_y + ((box_h - drv_display_font_height()) / 2)),
                                   display_label);

    drv_display_draw_line(box_x + 2, box_y + box_h + 2, box_x + box_w - 3, box_y + box_h + 2);
}

void uiw_draw_jack_icon(int x, int y, int w, int h)
{
    const int cx = x + (w / 2);
    const int top = y + 10;
    const int ring_y = y + 22;

    (void)h;

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
    const int key_y = y + 12;
    const int key_w = w - 8;
    const int key_h = 12;
    const int white_w = key_w / 5;

    (void)h;

    drv_display_draw_rect(key_x, key_y, key_w, key_h);

    for (int i = 1; i < 5; i++)
    {
        drv_display_draw_line(key_x + (i * white_w), key_y + 5, key_x + (i * white_w), key_y + key_h - 1);
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
