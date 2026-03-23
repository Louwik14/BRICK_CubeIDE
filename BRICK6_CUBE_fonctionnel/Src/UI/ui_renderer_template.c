#include "ui_renderer_template.h"

#include <stdio.h>
#include <string.h>

#include "drv_display.h"
#include "font.h"
#include "param_registry.h"

#define UI_TEMPLATE_FRAME_W          31
#define UI_TEMPLATE_FRAME_H          37
#define UI_TEMPLATE_FRAME_Y          16
#define UI_TEMPLATE_FOOTER_Y         54
#define UI_TEMPLATE_FOOTER_H         10
#define UI_TEMPLATE_NOTE_X           101
#define UI_TEMPLATE_NOTE_Y           1

static const uint8_t g_ui_template_frame_x[4] = {0U, 32U, 65U, 97U};
static const uint8_t g_ui_template_footer_x[4] = {0U, 32U, 65U, 97U};
static const uint8_t g_ui_template_footer_w[4] = {31U, 31U, 31U, 31U};

typedef enum
{
    UI_TEMPLATE_WIDGET_KNOB = 0,
    UI_TEMPLATE_WIDGET_SWITCH,
    UI_TEMPLATE_WIDGET_WAVE,
    UI_TEMPLATE_WIDGET_FILTER,
} ui_template_widget_t;

static void ui_renderer_template_format_value(param_id_t id, char *out, uint32_t out_len)
{
    const param_desc_t *desc = &param_registry[id];
    const float value = param_get(id);

    switch (desc->display_type)
    {
        case PARAM_DISPLAY_BOOL:
        {
            const uint32_t index = (value >= 0.5f) ? 1U : 0U;
            const char *label = ((desc->labels != NULL) && (desc->labels[index] != NULL)) ? desc->labels[index] : ((index != 0U) ? "On" : "Off");
            (void)snprintf(out, out_len, "%s", label);
            break;
        }

        case PARAM_DISPLAY_ENUM:
        {
            const int32_t index = (int32_t)(value + 0.5f);
            const char *label = NULL;
            if ((desc->labels != NULL) && (index >= 0))
            {
                label = desc->labels[index];
            }
            if (label != NULL)
            {
                (void)snprintf(out, out_len, "%s", label);
            }
            else
            {
                (void)snprintf(out, out_len, "%ld", (long)index);
            }
            break;
        }

        case PARAM_DISPLAY_PERCENT:
            (void)snprintf(out, out_len, "%3lu%%", (unsigned long)(value * 100.0f + 0.5f));
            break;

        case PARAM_DISPLAY_DB:
            (void)snprintf(out, out_len, "%.1f%s", (double)value, (desc->unit != NULL) ? desc->unit : "");
            break;

        case PARAM_DISPLAY_TIME_MS:
            (void)snprintf(out, out_len, "%.1fms", (double)(value * 1000.0f));
            break;

        case PARAM_DISPLAY_RATIO:
            (void)snprintf(out, out_len, "%.2f", (double)value);
            break;

        case PARAM_DISPLAY_INT:
            (void)snprintf(out, out_len, "%ld", (long)(value + 0.5f));
            break;

        default:
            if ((desc->unit != NULL) && (desc->unit[0] != '\0'))
            {
                (void)snprintf(out, out_len, "%.2f%s", (double)value, desc->unit);
            }
            else
            {
                (void)snprintf(out, out_len, "%.2f", (double)value);
            }
            break;
    }
}

static int ui_renderer_template_center_x(int x, int w, const char *txt)
{
    const int text_w = (int)drv_display_text_width(txt);
    int out = x + ((w - text_w) / 2);
    if (out < (x + 1))
    {
        out = x + 1;
    }
    return out;
}

static void ui_renderer_template_draw_open_corner_frame(int x, int y, int w, int h)
{
    const int c = 2;

    drv_display_draw_line(x + c,         y,             x + w - 1 - c, y);
    drv_display_draw_line(x + c,         y + h - 1,     x + w - 1 - c, y + h - 1);

    drv_display_draw_line(x,             y + c,         x,             y + h - 1 - c);
    drv_display_draw_line(x + w - 1,     y + c,         x + w - 1,     y + h - 1 - c);

    drv_display_draw_line(x,             y + c,         x + c,         y);
    drv_display_draw_line(x + w - 1 - c, y,             x + w - 1,     y + c);

    drv_display_draw_line(x,             y + h - 1 - c, x + c,         y + h - 1);
    drv_display_draw_line(x + w - 1 - c, y + h - 1,     x + w - 1,     y + h - 1 - c);
}static void ui_renderer_template_draw_note_icon(int x, int y)
{
    drv_display_draw_line(x + 2, y + 1, x + 2, y + 10);
    drv_display_draw_line(x + 2, y + 1, x + 7, y + 3);
    drv_display_draw_line(x + 7, y + 3, x + 7, y + 11);
    drv_display_draw_line(x + 2, y + 6, x + 7, y + 8);
    drv_display_draw_rect(x, y + 8, 3, 3);
    drv_display_draw_rect(x + 5, y + 10, 3, 3);
}

static void ui_renderer_template_draw_inverted_label(uint8_t x, uint8_t y, const char *txt, const font_t *font)
{
    drv_display_set_font(font);
    const uint8_t w = (uint8_t)(drv_display_text_width(txt) + 2U);
    const uint8_t h = (uint8_t)(drv_display_font_height() + 2U);
    drv_display_fill_rect(x, y, w, h);
    drv_display_draw_text_inverted((uint8_t)(x + 1U), (uint8_t)(y + 1U), txt);
}

static void ui_renderer_template_draw_switch(int x, int y, int w, int h, uint8_t on)
{
    const int sw_w = 18;
    const int sw_h = 8;
    const int sx = x + ((w - sw_w) / 2);
    const int sy = y + 13;

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

static void ui_renderer_template_draw_wave_icon(int x, int y, int w, int h, const char *label)
{
    const int lx = x + 5;
    const int rx = x + w - 6;
    const int mid = x + (w / 2);
    const int top = y + 12;
    const int bot = y + h - 14;

    if ((label != NULL) && ((strncmp(label, "Tri", 3) == 0) || (strncmp(label, "TRI", 3) == 0)))
    {
        drv_display_draw_line(lx, bot, mid, top);
        drv_display_draw_line(mid, top, rx, bot);
    }
    else if ((label != NULL) && ((strncmp(label, "Saw", 3) == 0) || (strncmp(label, "SAW", 3) == 0)))
    {
        drv_display_draw_line(lx, bot, rx - 4, top);
        drv_display_draw_line(rx - 4, top, rx - 4, bot);
        drv_display_draw_line(rx - 4, bot, rx, bot);
    }
    else if ((label != NULL) && ((strncmp(label, "Sin", 3) == 0) || (strncmp(label, "Sine", 4) == 0)))
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

static void ui_renderer_template_draw_filter_icon(int x, int y, int w, int h, const char *label)
{
    const int lx = x + 5;
    const int rx = x + w - 6;
    const int top = y + 12;
    const int bot = y + h - 14;

    if ((label != NULL) && ((strncmp(label, "HP", 2) == 0) || (strncmp(label, "High", 4) == 0)))
    {
        drv_display_draw_line(lx, bot, lx + 5, bot);
        drv_display_draw_line(lx + 5, bot, rx, top);
    }
    else if ((label != NULL) && ((strncmp(label, "BP", 2) == 0) || (strncmp(label, "Band", 4) == 0)))
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

static void ui_renderer_template_draw_knob(int x, int y, int w, int h, int value, int vmin, int vmax)
{
    static const int8_t indicator_x[7] = {-5, -4, -2, 0, 2, 4, 5};
    static const int8_t indicator_y[7] = {2, -2, -4, -5, -4, -2, 2};

    const int cx = x + (w / 2);
    const int cy = y + 18;
    const int r = 6;
    int idx;

    drv_display_draw_line(cx - r, cy, cx - 4, cy - 4);
    drv_display_draw_line(cx - 4, cy - 4, cx, cy - r);
    drv_display_draw_line(cx, cy - r, cx + 4, cy - 4);
    drv_display_draw_line(cx + 4, cy - 4, cx + r, cy);
    drv_display_draw_line(cx + r, cy, cx + 4, cy + 4);
    drv_display_draw_line(cx + 4, cy + 4, cx, cy + r);
    drv_display_draw_line(cx, cy + r, cx - 4, cy + 4);
    drv_display_draw_line(cx - 4, cy + 4, cx - r, cy);

    if (vmax <= vmin)
    {
        idx = 3;
    }
    else
    {
        int scaled = ((value - vmin) * 6) / (vmax - vmin);
        if (scaled < 0)
        {
            scaled = 0;
        }
        if (scaled > 6)
        {
            scaled = 6;
        }
        idx = scaled;
    }

    drv_display_draw_line(cx, cy, cx + indicator_x[idx], cy + indicator_y[idx]);
}

static ui_template_widget_t ui_renderer_template_get_widget_type(const param_desc_t *desc, const char *label)
{
    if (desc->display_type == PARAM_DISPLAY_BOOL)
    {
        return UI_TEMPLATE_WIDGET_SWITCH;
    }

    if (desc->display_type == PARAM_DISPLAY_ENUM)
    {
        if ((label != NULL) &&
            ((strncmp(label, "Saw", 3) == 0) || (strncmp(label, "SAW", 3) == 0) ||
             (strncmp(label, "Tri", 3) == 0) || (strncmp(label, "TRI", 3) == 0) ||
             (strncmp(label, "Sin", 3) == 0) || (strncmp(label, "Sine", 4) == 0) ||
             (strncmp(label, "Pulse", 5) == 0) || (strncmp(label, "Square", 6) == 0)))
        {
            return UI_TEMPLATE_WIDGET_WAVE;
        }

        if ((label != NULL) &&
            ((strncmp(label, "LP", 2) == 0) || (strncmp(label, "HP", 2) == 0) ||
             (strncmp(label, "BP", 2) == 0) || (strncmp(label, "Low", 3) == 0) ||
             (strncmp(label, "High", 4) == 0) || (strncmp(label, "Band", 4) == 0)))
        {
            return UI_TEMPLATE_WIDGET_FILTER;
        }
    }

    return UI_TEMPLATE_WIDGET_KNOB;
}

static void ui_renderer_template_draw_param_slot(uint8_t slot, param_id_t id)
{
    const int x = g_ui_template_frame_x[slot];
    const int y = UI_TEMPLATE_FRAME_Y;

    ui_renderer_template_draw_open_corner_frame(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H);

    if (id >= PARAM_COUNT)
    {
        drv_display_set_font(&FONT_4X6);
        drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, "-"), (uint8_t)(y + 14), "-");
        return;
    }

    const param_desc_t *desc = &param_registry[id];
    const float value = param_get(id);
    const char *enum_label = NULL;
    char value_txt[20];

    if ((desc->display_type == PARAM_DISPLAY_ENUM) && (desc->labels != NULL))
    {
        const int32_t index = (int32_t)(value + 0.5f);
        if (index >= 0)
        {
            enum_label = desc->labels[index];
        }
    }

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, desc->name), (uint8_t)(y + 3), desc->name);

    switch (ui_renderer_template_get_widget_type(desc, enum_label))
    {
        case UI_TEMPLATE_WIDGET_SWITCH:
            ui_renderer_template_draw_switch(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, (value >= 0.5f) ? 1U : 0U);
            break;

        case UI_TEMPLATE_WIDGET_WAVE:
            ui_renderer_template_draw_wave_icon(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, enum_label);
            break;

        case UI_TEMPLATE_WIDGET_FILTER:
            ui_renderer_template_draw_filter_icon(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, enum_label);
            break;

        case UI_TEMPLATE_WIDGET_KNOB:
        default:
        {
            const int vmin = (int)(desc->min * 10.0f);
            const int vmax = (int)(desc->max * 10.0f);
            const int vint = (int)(value * 10.0f);
            ui_renderer_template_draw_knob(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, vint, vmin, vmax);
            break;
        }
    }

    ui_renderer_template_format_value(id, value_txt, (uint32_t)sizeof(value_txt));
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, value_txt), (uint8_t)(y + UI_TEMPLATE_FRAME_H - 8), value_txt);
}

static void ui_renderer_template_draw_header(const ui_template_page_state_t *state)
{
    const char *family_title = (state->family->family_title != NULL) ? state->family->family_title : "TEMPLATE";

    drv_display_set_font(&FONT_5X7);
    ui_renderer_template_draw_inverted_label(0U, 1U, "1", &FONT_5X7);
    drv_display_draw_text(9U, 1U, "Synth");

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text(9U, 9U, "SEQ");

    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(0, OLED_WIDTH, family_title), 2U, family_title);

    drv_display_set_font(&FONT_4X6);
    ui_renderer_template_draw_note_icon(UI_TEMPLATE_NOTE_X, UI_TEMPLATE_NOTE_Y);
    drv_display_draw_text(109U, 1U, "120.0");
    drv_display_draw_text(113U, 9U, "A-12");
}

static void ui_renderer_template_draw_footer(const ui_template_page_state_t *state)
{
    drv_display_set_font(&FONT_4X6);

    for (uint8_t i = 0U; i < 4U; i++)
    {
        const int bx = g_ui_template_footer_x[i];
        const int bw = g_ui_template_footer_w[i];
        const char *label = state->family->nav_labels[i];

        if ((label == NULL) || (label[0] == '\0'))
        {
            label = "-";
        }


        const int x_label = ui_renderer_template_center_x(bx, bw, label);

        if (i == state->active_subpage)
        {
            drv_display_fill_rect(bx, UI_TEMPLATE_FOOTER_Y, bw, UI_TEMPLATE_FOOTER_H);

            drv_display_draw_pixel(bx,                 UI_TEMPLATE_FOOTER_Y,                 false);
            drv_display_draw_pixel(bx + 1,             UI_TEMPLATE_FOOTER_Y,                 false);
            drv_display_draw_pixel(bx,                 UI_TEMPLATE_FOOTER_Y + 1,             false);

            drv_display_draw_pixel(bx + bw - 1,        UI_TEMPLATE_FOOTER_Y,                 false);
            drv_display_draw_pixel(bx + bw - 2,        UI_TEMPLATE_FOOTER_Y,                 false);
            drv_display_draw_pixel(bx + bw - 1,        UI_TEMPLATE_FOOTER_Y + 1,             false);

            drv_display_draw_pixel(bx,                 UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 1, false);
            drv_display_draw_pixel(bx + 1,             UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 1, false);
            drv_display_draw_pixel(bx,                 UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 2, false);

            drv_display_draw_pixel(bx + bw - 1,        UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 1, false);
            drv_display_draw_pixel(bx + bw - 2,        UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 1, false);
            drv_display_draw_pixel(bx + bw - 1,        UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 2, false);

            drv_display_draw_text_inverted((uint8_t)x_label, 56U, label);
        }
        else
        {
            ui_renderer_template_draw_open_corner_frame(bx, UI_TEMPLATE_FOOTER_Y, bw, UI_TEMPLATE_FOOTER_H);
            drv_display_draw_text((uint8_t)x_label, 56U, label);
        }
    }
}

void ui_renderer_template_draw(const ui_template_page_state_t *state)
{
    if ((state == NULL) || (state->family == NULL))
    {
        drv_display_draw_text(0U, 0U, "TEMPLATE N/A");
        return;
    }

    ui_renderer_template_draw_header(state);

    const ui_template_subpage_t *subpage = ui_template_page_get_active_subpage(state);
    if (subpage != NULL)
    {
        for (uint8_t i = 0U; i < 4U; i++)
        {
            ui_renderer_template_draw_param_slot(i, subpage->param_bank.params[i]);
        }
    }

    ui_renderer_template_draw_footer(state);

    drv_display_set_font(&FONT_5X7);
}
