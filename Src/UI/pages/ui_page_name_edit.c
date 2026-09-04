#include "pages/ui_page_name_edit.h"

#include <stdio.h>
#include <string.h>

#include "buttons.h"
#include "drv_display.h"
#include "font.h"
#include "ui_event.h"
#include "ui_page_manager.h"

#define NAME_EDIT_HEADER_LINE_Y 7U
#define NAME_EDIT_NAME_Y 15U
#define NAME_EDIT_FRIEZE_Y 38U
#define NAME_EDIT_FOOTER_Y 58U
#define NAME_EDIT_NAME_VISIBLE_CHARS 20U
#define NAME_EDIT_FRIEZE_CELLS 13U

typedef struct
{
    uint8_t return_page;
    uint8_t max_len;
    uint8_t pos;
    uint8_t char_index;
    char title[13];
    char context[21];
    char name[UI_PAGE_NAME_EDIT_TEXT_MAX];
    ui_page_name_edit_done_fn done;
    void *user;
} ui_page_name_edit_state_t;

static ui_page_name_edit_state_t g_name_edit = {
    .return_page = 0U,
    .max_len = UI_PAGE_NAME_EDIT_TEXT_MAX,
    .pos = 0U,
    .char_index = 0U,
    .title = { 0 },
    .context = { 0 },
    .name = { 0 },
    .done = 0,
    .user = 0,
};

static const char g_name_edit_chars[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";

static void ui_page_name_edit_fit_label(char *out,
                                        uint32_t out_size,
                                        const char *in,
                                        uint8_t max_px)
{
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    memset(out, 0, out_size);
    if (in == 0)
    {
        return;
    }

    (void)snprintf(out, out_size, "%s", in);
    drv_display_set_font(&FONT_4X6);
    if (drv_display_text_width(out) <= max_px)
    {
        return;
    }

    const uint32_t len = strlen(out);
    if (len <= 1U)
    {
        return;
    }

    for (uint32_t keep = len - 1U; keep > 0U; --keep)
    {
        if ((keep + 1U) >= out_size)
        {
            continue;
        }
        out[keep] = '~';
        out[keep + 1U] = '\0';
        if (drv_display_text_width(out) <= max_px)
        {
            return;
        }
        out[keep] = '\0';
    }
}

static void ui_page_name_edit_draw_centered_label(uint8_t x,
                                                  uint8_t w,
                                                  uint8_t y,
                                                  const char *label)
{
    if ((label == 0) || (w == 0U))
    {
        return;
    }

    drv_display_set_font(&FONT_4X6);
    const uint8_t text_w = drv_display_text_width(label);
    const uint8_t text_x = (text_w >= w) ? x : (uint8_t)(x + ((w - text_w) / 2U));
    drv_display_draw_text(text_x, y, label);
}

static char ui_page_name_edit_sanitize_char(char c)
{
    const uint8_t char_count = (uint8_t)(sizeof(g_name_edit_chars) - 1U);
    for (uint8_t i = 0U; i < char_count; ++i)
    {
        if (g_name_edit_chars[i] == c)
        {
            return c;
        }
    }
    return ' ';
}

static uint8_t ui_page_name_edit_char_index(char c)
{
    const uint8_t char_count = (uint8_t)(sizeof(g_name_edit_chars) - 1U);
    if (c == '\0')
    {
        c = ' ';
    }

    for (uint8_t i = 0U; i < char_count; ++i)
    {
        if (g_name_edit_chars[i] == c)
        {
            return i;
        }
    }
    return 0U;
}

static uint8_t ui_page_name_edit_name_len(void)
{
    uint8_t len = 0U;
    while ((len < (uint8_t)(g_name_edit.max_len - 1U))
            && (g_name_edit.name[len] != '\0'))
    {
        ++len;
    }
    return len;
}

static uint8_t ui_page_name_edit_max_pos(void)
{
    const uint8_t len = ui_page_name_edit_name_len();
    const uint8_t last = (uint8_t)(g_name_edit.max_len - 2U);
    return (len < last) ? len : last;
}

static void ui_page_name_edit_clamp_pos(void)
{
    const uint8_t max_pos = ui_page_name_edit_max_pos();
    if (g_name_edit.pos > max_pos)
    {
        g_name_edit.pos = max_pos;
    }
}

static void ui_page_name_edit_sync_char_to_pos(uint8_t keep_on_empty)
{
    const uint8_t len = ui_page_name_edit_name_len();
    if (g_name_edit.pos < len)
    {
        g_name_edit.char_index = ui_page_name_edit_char_index(g_name_edit.name[g_name_edit.pos]);
        return;
    }

    if (keep_on_empty == 0U)
    {
        g_name_edit.char_index = ui_page_name_edit_char_index(' ');
    }
}

static uint8_t ui_page_name_edit_glyph_w(void)
{
    drv_display_set_font(&FONT_5X7);
    uint8_t w = drv_display_text_width("W");
    const uint8_t space_w = drv_display_text_width(" ");
    if (space_w > w)
    {
        w = space_w;
    }
    return (w == 0U) ? 5U : w;
}

static void ui_page_name_edit_commit_char(char c)
{
    ui_page_name_edit_clamp_pos();

    const uint8_t len = ui_page_name_edit_name_len();
    const uint8_t last = (uint8_t)(g_name_edit.max_len - 2U);
    if (g_name_edit.pos > len)
    {
        return;
    }

    g_name_edit.name[g_name_edit.pos] = ui_page_name_edit_sanitize_char(c);
    if ((g_name_edit.pos == len) && (g_name_edit.pos < last))
    {
        g_name_edit.name[g_name_edit.pos + 1U] = '\0';
    }
    g_name_edit.name[g_name_edit.max_len - 1U] = '\0';

    if (g_name_edit.pos < last)
    {
        ++g_name_edit.pos;
    }
    ui_page_name_edit_clamp_pos();
    ui_page_name_edit_sync_char_to_pos(1U);
}

static void ui_page_name_edit_backspace(void)
{
    ui_page_name_edit_clamp_pos();

    if (g_name_edit.pos == 0U)
    {
        return;
    }

    --g_name_edit.pos;
    for (uint8_t i = g_name_edit.pos; i < (uint8_t)(g_name_edit.max_len - 1U); ++i)
    {
        g_name_edit.name[i] = g_name_edit.name[i + 1U];
        if (g_name_edit.name[i] == '\0')
        {
            break;
        }
    }
    g_name_edit.name[g_name_edit.max_len - 1U] = '\0';

    ui_page_name_edit_clamp_pos();
    ui_page_name_edit_sync_char_to_pos(1U);
}

static void ui_page_name_edit_trim_name(char *name)
{
    if (name == 0)
    {
        return;
    }

    for (int32_t i = (int32_t)UI_PAGE_NAME_EDIT_TEXT_MAX - 2; i >= 0; --i)
    {
        if (name[i] == '\0')
        {
            continue;
        }
        if (name[i] != ' ')
        {
            break;
        }
        name[i] = '\0';
    }

    if (name[0] == '\0')
    {
        (void)snprintf(name, UI_PAGE_NAME_EDIT_TEXT_MAX, "PATCH");
    }
}

static void ui_page_name_edit_finish(ui_page_name_edit_result_t result)
{
    ui_page_name_edit_done_fn done = g_name_edit.done;
    void *user = g_name_edit.user;
    char name[UI_PAGE_NAME_EDIT_TEXT_MAX];
    memset(name, 0, sizeof(name));
    memcpy(name, g_name_edit.name, sizeof(name));
    name[sizeof(name) - 1U] = '\0';
    ui_page_name_edit_trim_name(name);

    const uint8_t return_page = g_name_edit.return_page;
    g_name_edit.done = 0;
    g_name_edit.user = 0;
    ui_page_set(return_page);

    if (done != 0)
    {
        done(result, name, user);
    }
}

uint8_t ui_page_name_edit_open(uint8_t return_page,
                               const char *title,
                               const char *context,
                               const char *initial,
                               uint8_t max_len,
                               ui_page_name_edit_done_fn done,
                               void *user)
{
    if ((done == 0) || (max_len < 2U))
    {
        return 0U;
    }

    if (max_len > UI_PAGE_NAME_EDIT_TEXT_MAX)
    {
        max_len = UI_PAGE_NAME_EDIT_TEXT_MAX;
    }

    memset(&g_name_edit, 0, sizeof(g_name_edit));
    g_name_edit.return_page = return_page;
    g_name_edit.max_len = max_len;
    g_name_edit.done = done;
    g_name_edit.user = user;
    (void)snprintf(g_name_edit.title,
                   sizeof(g_name_edit.title),
                   "%s",
                   (title != 0) ? title : "NAME");
    (void)snprintf(g_name_edit.context,
                   sizeof(g_name_edit.context),
                   "%s",
                   (context != 0) ? context : "");

    if (initial != 0)
    {
        for (uint8_t i = 0U; i < (uint8_t)(max_len - 1U); ++i)
        {
            if (initial[i] == '\0')
            {
                break;
            }
            g_name_edit.name[i] = ui_page_name_edit_sanitize_char(initial[i]);
        }
    }
    g_name_edit.name[max_len - 1U] = '\0';
    ui_page_name_edit_clamp_pos();
    ui_page_name_edit_sync_char_to_pos(0U);
    ui_page_set(UI_PAGE_NAME_EDIT);
    return 1U;
}

uint8_t ui_page_name_edit_is_open(void)
{
    return (ui_page_get_id() == UI_PAGE_NAME_EDIT) ? 1U : 0U;
}

uint8_t ui_page_name_edit_handle_encoder(uint8_t encoder, int16_t delta)
{
    if (delta == 0)
    {
        return 1U;
    }

    if (encoder == 0U)
    {
        const uint8_t char_count = (uint8_t)(sizeof(g_name_edit_chars) - 1U);
        int32_t next = (int32_t)g_name_edit.char_index + (int32_t)delta;
        if (next < 0)
        {
            next = 0;
        }
        if (next >= (int32_t)char_count)
        {
            next = (int32_t)char_count - 1;
        }
        g_name_edit.char_index = (uint8_t)next;
        return 1U;
    }

    return 1U;
}

static void ui_page_name_edit_handle_event(const ui_event_t *ev)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        return;
    }

    switch ((button_id_t)ev->id)
    {
        case BTN_PAGE_1:
            if (ev->shift_down == 0U)
            {
                ui_page_name_edit_finish(UI_PAGE_NAME_EDIT_RESULT_CANCEL);
            }
            break;

        case BTN_PAGE_2:
            if (ev->shift_down != 0U)
            {
                ui_page_name_edit_commit_char(' ');
            }
            else
            {
                ui_page_name_edit_finish(UI_PAGE_NAME_EDIT_RESULT_OK);
            }
            break;

        case BTN_PAGE_3:
            if (ev->shift_down == 0U)
            {
                ui_page_name_edit_commit_char(g_name_edit_chars[g_name_edit.char_index]);
            }
            break;

        case BTN_PAGE_4:
            if (ev->shift_down == 0U)
            {
                ui_page_name_edit_backspace();
            }
            break;

        default:
            break;
    }
}

static void ui_page_name_edit_draw_name(void)
{
    char text[NAME_EDIT_NAME_VISIBLE_CHARS + 1U];
    const uint8_t visible = NAME_EDIT_NAME_VISIBLE_CHARS;
    const uint8_t glyph_w = ui_page_name_edit_glyph_w();
    const uint8_t font_h = drv_display_font_height();
    uint8_t first = 0U;
    if (g_name_edit.pos >= (visible - 2U))
    {
        first = (uint8_t)(g_name_edit.pos - (visible / 2U));
    }
    if ((first + visible) > (uint8_t)(g_name_edit.max_len - 1U))
    {
        first = (g_name_edit.max_len > (visible + 1U))
            ? (uint8_t)(g_name_edit.max_len - 1U - visible)
            : 0U;
    }

    memset(text, 0, sizeof(text));
    for (uint8_t i = 0U; i < visible; ++i)
    {
        const uint8_t src = (uint8_t)(first + i);
        if (src >= (uint8_t)(g_name_edit.max_len - 1U))
        {
            break;
        }
        const char c = g_name_edit.name[src];
        text[i] = (c == '\0') ? ' ' : c;
    }

    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text(2U, NAME_EDIT_NAME_Y, text);

    const uint8_t cursor_col = (uint8_t)(g_name_edit.pos - first);
    if (cursor_col < visible)
    {
        char prefix[NAME_EDIT_NAME_VISIBLE_CHARS + 1U];
        memset(prefix, 0, sizeof(prefix));
        memcpy(prefix, text, cursor_col);
        prefix[cursor_col] = '\0';
        const uint8_t x = (uint8_t)(2U + drv_display_text_width(prefix));
        const char c = (g_name_edit.name[g_name_edit.pos] == '\0')
            ? ' '
            : g_name_edit.name[g_name_edit.pos];
        char one[2] = { c, '\0' };
        drv_display_fill_rect(x,
                              (uint8_t)(NAME_EDIT_NAME_Y - 1U),
                              (uint8_t)(glyph_w + 2U),
                              (uint8_t)(font_h + 2U));
        drv_display_draw_text_inverted(x, NAME_EDIT_NAME_Y, one);
    }
}

static void ui_page_name_edit_draw_frieze(void)
{
    const uint8_t char_count = (uint8_t)(sizeof(g_name_edit_chars) - 1U);
    const uint8_t current = (g_name_edit.char_index < char_count) ? g_name_edit.char_index : 0U;
    const int16_t center = (int16_t)(NAME_EDIT_FRIEZE_CELLS / 2U);

    drv_display_set_font(&FONT_5X7);
    const uint8_t glyph_w = ui_page_name_edit_glyph_w();
    const uint8_t font_h = drv_display_font_height();
    const uint8_t cell_w = (uint8_t)(glyph_w + 4U);
    const uint8_t total_w = (uint8_t)(NAME_EDIT_FRIEZE_CELLS * cell_w);
    const uint8_t x0 = (total_w >= 128U) ? 0U : (uint8_t)((128U - total_w) / 2U);
    for (uint8_t cell = 0U; cell < NAME_EDIT_FRIEZE_CELLS; ++cell)
    {
        int16_t index = (int16_t)current + (int16_t)cell - center;
        if (index < 0)
        {
            index = 0;
        }
        if (index >= (int16_t)char_count)
        {
            index = (int16_t)char_count - 1;
        }

        const uint8_t cell_x = (uint8_t)(x0 + (cell * cell_w));
        const char c = g_name_edit_chars[index];
        char one[2] = { c, '\0' };
        const uint8_t text_w = drv_display_text_width(one);
        const uint8_t text_x = (text_w >= cell_w)
            ? cell_x
            : (uint8_t)(cell_x + ((cell_w - text_w) / 2U));
        if (cell == (uint8_t)center)
        {
            drv_display_fill_rect(cell_x,
                                  (uint8_t)(NAME_EDIT_FRIEZE_Y - 1U),
                                  cell_w,
                                  (uint8_t)(font_h + 2U));
            drv_display_draw_text_inverted(text_x, NAME_EDIT_FRIEZE_Y, one);
        }
        else
        {
            drv_display_draw_text(text_x, NAME_EDIT_FRIEZE_Y, one);
        }
    }
}

static void ui_page_name_edit_render(void)
{
    char fit[UI_PAGE_NAME_EDIT_TEXT_MAX];
    drv_display_set_font(&FONT_4X6);
    ui_page_name_edit_fit_label(fit, sizeof(fit), g_name_edit.title, 56U);
    drv_display_draw_text(0U, 0U, fit);
    ui_page_name_edit_fit_label(fit, sizeof(fit), g_name_edit.context, 62U);
    drv_display_draw_text(64U, 0U, fit);
    drv_display_draw_line(0, NAME_EDIT_HEADER_LINE_Y, 127, NAME_EDIT_HEADER_LINE_Y);

    ui_page_name_edit_draw_name();
    ui_page_name_edit_draw_frieze();

    if (button_down(BTN_SHIFT) != 0U)
    {
        ui_page_name_edit_draw_centered_label(0U, 32U, NAME_EDIT_FOOTER_Y, "-");
        ui_page_name_edit_draw_centered_label(32U, 32U, NAME_EDIT_FOOTER_Y, "SPACE");
        ui_page_name_edit_draw_centered_label(64U, 32U, NAME_EDIT_FOOTER_Y, "-");
        ui_page_name_edit_draw_centered_label(96U, 32U, NAME_EDIT_FOOTER_Y, "-");
    }
    else
    {
        ui_page_name_edit_draw_centered_label(0U, 32U, NAME_EDIT_FOOTER_Y, "BACK");
        ui_page_name_edit_draw_centered_label(32U, 32U, NAME_EDIT_FOOTER_Y, "OK");
        ui_page_name_edit_draw_centered_label(64U, 32U, NAME_EDIT_FOOTER_Y, "CHAR");
        ui_page_name_edit_draw_centered_label(96U, 32U, NAME_EDIT_FOOTER_Y, "DEL");
    }
}

const ui_page_t g_ui_page_name_edit = {
    .enter = 0,
    .leave = 0,
    .handle_event = ui_page_name_edit_handle_event,
    .tick = 0,
    .sync_active_context = 0,
    .render = ui_page_name_edit_render,
    .context = 0,
};
