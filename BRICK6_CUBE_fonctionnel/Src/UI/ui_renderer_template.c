#include "ui_renderer_template.h"

#include <stdio.h>
#include <string.h>

#include "drv_display.h"
#include "font.h"
#include "param_registry.h"
#include "ui_core.h"
#include "ui_widgets.h"

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

static void ui_renderer_template_format_value(param_id_t id, char *out, uint32_t out_len)
{
    const param_desc_t *desc = &param_registry[id];
    const float value = param_get(id);

    if (id == PARAM_CFG_TRACK)
    {
        (void)snprintf(out, out_len, "%s", ui_get_track_family_display_name((ui_track_family_t)((uint8_t)(value + 0.5f))));
        return;
    }

    if (id == PARAM_CFG_TRACK_TYPE)
    {
        const ui_track_family_t active_family = ui_get_track_family(ui_get_active_track());
        const ui_track_type_t active_type = ui_get_track_type_from_family_index(active_family, (uint8_t)(value + 0.5f));
        (void)snprintf(out, out_len, "%s", ui_get_track_type_display_name(active_family, active_type));
        return;
    }

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
}

static void ui_renderer_template_draw_note_icon(int x, int y)
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

static uiw_widget_type_t ui_renderer_template_resolve_widget_type(const ui_template_page_state_t *state,
                                                                  uint8_t slot,
                                                                  param_id_t id,
                                                                  const param_desc_t *desc,
                                                                  const char *enum_label,
                                                                  const char *value_label)
{
    uiw_widget_type_t widget = uiw_pick_widget_type(desc, enum_label);

    if ((state != NULL) && (state->widget_picker != NULL))
    {
        widget = state->widget_picker(slot, id, value_label, widget);
    }

    return widget;
}

static void ui_renderer_template_draw_param_slot(const ui_template_page_state_t *state, uint8_t slot, param_id_t id)
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

    ui_renderer_template_format_value(id, value_txt, (uint32_t)sizeof(value_txt));

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, desc->name), (uint8_t)(y + 3), desc->name);

    const uiw_widget_type_t widget_type = ui_renderer_template_resolve_widget_type(state, slot, id, desc, enum_label, value_txt);

    switch (widget_type)
    {
        case UIW_WIDGET_EMPTY:
            break;

        case UIW_WIDGET_SWITCH:
            uiw_draw_switch(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, (value >= 0.5f) ? 1U : 0U);
            break;

        case UIW_WIDGET_ENUM_TEXT:
            uiw_draw_enum_text(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, (enum_label != NULL) ? enum_label : value_txt);
            break;

        case UIW_WIDGET_JACK:
            uiw_draw_jack_icon(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H);
            break;

        case UIW_WIDGET_KEYBOARD:
            uiw_draw_keyboard_icon(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H);
            break;

        case UIW_WIDGET_WAVE_ICON:
            uiw_draw_wave_icon(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, enum_label);
            break;

        case UIW_WIDGET_FILTER_ICON:
            uiw_draw_filter_icon(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, enum_label);
            break;

        case UIW_WIDGET_KNOB:
        default:
        {
            const int vmin = (int)(desc->min * 10.0f);
            const int vmax = (int)(desc->max * 10.0f);
            const int vint = (int)(value * 10.0f);
            uiw_draw_knob(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, vint, vmin, vmax);
            break;
        }
    }

    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, value_txt), (uint8_t)(y + UI_TEMPLATE_FRAME_H - 8), value_txt);
}

static void ui_renderer_template_draw_header(const ui_template_page_state_t *state)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);
    const char *family_title = ((family != NULL) && (family->family_title != NULL)) ? family->family_title : "TEMPLATE";
    const uint8_t active_track = ui_get_active_track();
    char track_label[4];
    char runtime_label[12];

    (void)snprintf(track_label, sizeof(track_label), "%u", (unsigned int)(active_track + 1U));
    ui_get_track_runtime_header_label(active_track, runtime_label, (uint32_t)sizeof(runtime_label));

    drv_display_set_font(&FONT_5X7);
    ui_renderer_template_draw_inverted_label(0U, 1U, track_label, &FONT_5X7);
    drv_display_draw_text(9U, 1U, runtime_label);

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text(9U, 9U, ui_get_hall_mode_short_label());

    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(0, OLED_WIDTH, family_title), 2U, family_title);

    drv_display_set_font(&FONT_4X6);
    ui_renderer_template_draw_note_icon(UI_TEMPLATE_NOTE_X, UI_TEMPLATE_NOTE_Y);
    drv_display_draw_text(109U, 1U, "120.0");
    drv_display_draw_text(113U, 9U, "A-12");
}

static void ui_renderer_template_draw_footer(const ui_template_page_state_t *state)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);

    drv_display_set_font(&FONT_4X6);

    for (uint8_t i = 0U; i < 4U; i++)
    {
        const int bx = g_ui_template_footer_x[i];
        const int bw = g_ui_template_footer_w[i];
        const char *label = family->nav_labels[i];

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
    const ui_template_family_t *family = ui_template_page_get_active_family(state);

    if ((state == NULL) || (family == NULL))
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
            ui_renderer_template_draw_param_slot(state, i, subpage->param_bank.params[i]);
        }
    }

    ui_renderer_template_draw_footer(state);

    drv_display_set_font(&FONT_5X7);
}
