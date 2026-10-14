#include "ui_widgets.h"

#include <limits.h>
#include <string.h>

#include "Audio/Publications/audio_waveform_reader.h"
#include "drv_display.h"
#include "font.h"
#include "Platform/memory_layout.h"
#include "UI/ui_algo_icons.h"

#define UIW_SWITCH_W              18
#define UIW_SWITCH_H              8
#define UIW_ENUM_BOX_H            12
#define UIW_BAR_H                 7
#define UIW_BAR_OFFSET_Y          4
#define UIW_JACK_RING_OFFSET_Y    4
#define UIW_KEYBOARD_H            12
#define UIW_SHAPE_H               16

static const uint32_t *const g_ui_algo_icons[32] = {
    UI_ICON_ALGO1, UI_ICON_ALGO2, UI_ICON_ALGO3, UI_ICON_ALGO4,
    UI_ICON_ALGO5, UI_ICON_ALGO6, UI_ICON_ALGO7, UI_ICON_ALGO8,
    UI_ICON_ALGO9, UI_ICON_ALGO10, UI_ICON_ALGO11, UI_ICON_ALGO12,
    UI_ICON_ALGO13, UI_ICON_ALGO14, UI_ICON_ALGO15, UI_ICON_ALGO16,
    UI_ICON_ALGO17, UI_ICON_ALGO18, UI_ICON_ALGO19, UI_ICON_ALGO20,
    UI_ICON_ALGO21, UI_ICON_ALGO22, UI_ICON_ALGO23, UI_ICON_ALGO24,
    UI_ICON_ALGO25, UI_ICON_ALGO26, UI_ICON_ALGO27, UI_ICON_ALGO28,
    UI_ICON_ALGO29, UI_ICON_ALGO30, UI_ICON_ALGO31, UI_ICON_ALGO32
};

_Static_assert((sizeof(g_ui_algo_icons) / sizeof(g_ui_algo_icons[0])) == 32U,
               "FM algorithm icon mapping must contain 32 entries");

void uiw_audio_scope_reset(uiw_audio_scope_cache_t *cache)
{
    if (cache == NULL) return;
    memset(cache, 0, sizeof(*cache));
    cache->entity_id = BRICK_ENTITY_INVALID_ID;
    cache->display_peak = 32U;
}

uint8_t uiw_draw_audio_scope(audio_waveform_target_t target,
                             uint8_t entity_id,
                             int x, int y, int w, int h,
                             uiw_audio_scope_cache_t *cache)
{
    if ((cache == NULL) || (w < 2) || (w > (int)UIW_AUDIO_SCOPE_MAX_COLUMNS)
            || (h < 3)) return 0U;
    const int center = y + (h / 2);
    const int positive_height = center - y;
    const int negative_height = y + h - 1 - center;
    UI_STATE_SDRAM static audio_waveform_capture_snapshot_t snapshot;
    if (audio_waveform_capture_read(&snapshot) != 0U)
    {
        if ((snapshot.target != (uint8_t)target)
                || (snapshot.entity_id != entity_id))
            uiw_audio_scope_reset(cache);
        else if ((snapshot.valid != 0U)
                && ((cache->valid == 0U)
                    || (cache->generation != snapshot.generation)))
        {
            uint8_t peak = 0U;
            for (int column = 0; column < w; ++column)
            {
                int8_t column_min = INT8_MAX;
                int8_t column_max = INT8_MIN;
                if (snapshot.triggered != 0U)
                {
                    const uint32_t position_q8 =
                        (((uint32_t)column
                          * (AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES - 1U)) << 8)
                        / (uint32_t)(w - 1) + snapshot.trigger_fraction_q8;
                    uint32_t source = position_q8 >> 8;
                    const uint16_t fraction = (uint16_t)(position_q8 & 0xffU);
                    if (source >= AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES)
                        source = AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES - 1U;
                    const uint32_t next = (source + 1U
                            < AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES)
                        ? source + 1U : source;
                    const int32_t a = snapshot.samples[source];
                    const int32_t b = snapshot.samples[next];
                    column_min = column_max = (int8_t)(a
                        + (((b - a) * fraction) >> 8));
                }
                else
                {
                    const uint32_t begin = ((uint32_t)column
                        * AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES) / (uint32_t)w;
                    uint32_t end = ((uint32_t)(column + 1)
                        * AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES) / (uint32_t)w;
                    if (end <= begin) end = begin + 1U;
                    for (uint32_t sample = begin; sample < end; ++sample)
                    {
                        const int8_t value = snapshot.samples[sample];
                        if (value < column_min) column_min = value;
                        if (value > column_max) column_max = value;
                    }
                }
                const uint8_t min_peak = (column_min == INT8_MIN) ? 128U
                    : (uint8_t)((column_min < 0) ? -column_min : column_min);
                const uint8_t max_peak = (column_max == INT8_MIN) ? 128U
                    : (uint8_t)((column_max < 0) ? -column_max : column_max);
                if (min_peak > peak) peak = min_peak;
                if (max_peak > peak) peak = max_peak;
                cache->top[column] = column_max;
                cache->bottom[column] = column_min;
            }
            const uint8_t target_peak = (peak > 32U) ? peak : 32U;
            if ((cache->target != (uint8_t)target)
                    || (cache->entity_id != entity_id)) cache->display_peak = 32U;
            if (target_peak > cache->display_peak) cache->display_peak = target_peak;
            else cache->display_peak = (uint8_t)(((uint16_t)cache->display_peak
                * 7U + target_peak) / 8U);
            if (cache->display_peak < 32U) cache->display_peak = 32U;
            for (int column = 0; column < w; ++column)
            {
                int32_t high = ((int32_t)cache->top[column]
                    * ((cache->top[column] >= 0) ? positive_height : negative_height))
                    / cache->display_peak;
                int32_t low = ((int32_t)cache->bottom[column]
                    * ((cache->bottom[column] >= 0) ? positive_height : negative_height))
                    / cache->display_peak;
                if (high > positive_height) high = positive_height;
                if (high < -negative_height) high = -negative_height;
                if (low > positive_height) low = positive_height;
                if (low < -negative_height) low = -negative_height;
                cache->top[column] = (int8_t)(center - high);
                cache->bottom[column] = (int8_t)(center - low);
            }
            cache->target = (uint8_t)target;
            cache->entity_id = entity_id;
            cache->generation = snapshot.generation;
            cache->triggered = snapshot.triggered;
            cache->valid = 1U;
        }
    }
    for (int px = x; px < x + w; px += 2)
        drv_display_draw_pixel(px, center, true);
    if ((cache->valid == 0U) || (cache->target != (uint8_t)target)
            || (cache->entity_id != entity_id)) return 1U;
    if (cache->triggered != 0U)
    {
        int previous = cache->top[0];
        for (int column = 1; column < w; ++column)
        {
            drv_display_draw_line(x + column - 1, previous,
                x + column, cache->top[column]);
            previous = cache->top[column];
        }
    }
    else
        for (int column = 0; column < w; ++column)
            drv_display_draw_line(x + column, cache->top[column],
                x + column, cache->bottom[column]);
    return 1U;
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

void uiw_draw_value_bar(int x, int y, int w, int h, float value, float vmin, float vmax)
{
    const int bx = x + 3;
    const int by = y + (h / 2) - 3 + UIW_BAR_OFFSET_Y;
    const int bw = (w > 8) ? (w - 6) : w;
    float norm = 0.0f;

    if (vmax > vmin)
    {
        norm = (value - vmin) / (vmax - vmin);
    }
    if (norm < 0.0f)
    {
        norm = 0.0f;
    }
    if (norm > 1.0f)
    {
        norm = 1.0f;
    }

    drv_display_draw_rect(bx, by, bw, UIW_BAR_H);
    const int fill_w = (int)((norm * (float)(bw - 2)) + 0.5f);
    if (fill_w > 0)
    {
        drv_display_fill_rect(bx + 1, by + 1, fill_w, UIW_BAR_H - 2);
    }
}

void uiw_draw_bipolar_bar(int x, int y, int w, int h, float value, float vmin, float vmax)
{
    const int cy = y + (h / 2) + UIW_BAR_OFFSET_Y;
    const int center = x + (w / 2);
    const int left_span = (center - x) - 2;
    const int right_span = (x + w - center) - 2;
    float center_value = 0.0f;
    float left_max = 1.0f;
    float right_max = 1.0f;

    if (!((vmin < 0.0f) && (vmax > 0.0f)))
    {
        center_value = vmin + ((vmax - vmin) * 0.5f);
    }
    left_max = center_value - vmin;
    right_max = vmax - center_value;
    if (left_max <= 0.0001f)
    {
        left_max = 1.0f;
    }
    if (right_max <= 0.0001f)
    {
        right_max = 1.0f;
    }

    drv_display_draw_line(x + 2, cy, x + w - 3, cy);
    drv_display_draw_line(center, y + 14, center, y + h - 7);

    if ((value < (center_value - 0.0001f)) && (left_span > 0))
    {
        int len = (int)(((center_value - value) * (float)left_span / left_max) + 0.5f);
        if (len > left_span)
        {
            len = left_span;
        }
        if (len > 0)
        {
            drv_display_fill_rect(center - len, cy - 1, len, 3);
        }
    }
    else if ((value > (center_value + 0.0001f)) && (right_span > 0))
    {
        int len = (int)(((value - center_value) * (float)right_span / right_max) + 0.5f);
        if (len > right_span)
        {
            len = right_span;
        }
        if (len > 0)
        {
            drv_display_fill_rect(center + 1, cy - 1, len, 3);
        }
    }
}

void uiw_draw_switch(int x, int y, int w, int h, uint8_t on)
{
    const int sx = x + ((w - UIW_SWITCH_W) / 2);
    const int sy = y + ((h - UIW_SWITCH_H) / 2) + UIW_BAR_OFFSET_Y;

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

void uiw_draw_algo_icon(int x, int y, int w, int h, uint8_t algorithm)
{
    if (algorithm >= 32U)
    {
        algorithm = 31U;
    }

    const int icon_x = x + ((w - (int)UI_ALGO_ICON_WIDTH) / 2);
    const int icon_y = y + ((h - (int)UI_ALGO_ICON_HEIGHT) / 2);
    const uint32_t *const bitmap = g_ui_algo_icons[algorithm];
    for (uint8_t row = 0U; row < UI_ALGO_ICON_HEIGHT; ++row)
    {
        for (uint8_t col = 0U; col < UI_ALGO_ICON_WIDTH; ++col)
        {
            if ((bitmap[row] & (1UL << (UI_ALGO_ICON_WIDTH - 1U - col))) != 0UL)
            {
                drv_display_draw_pixel((uint8_t)(icon_x + col),
                                        (uint8_t)(icon_y + row),
                                        true);
            }
        }
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

        return UIW_WIDGET_ENUM_TEXT;
    }

    if ((desc->type == PARAM_TYPE_BIPOLAR)
            || ((desc->min < 0.0f) && (desc->max > 0.0f)))
    {
        return UIW_WIDGET_BIPOLAR_BAR;
    }

    return UIW_WIDGET_BAR;
}
