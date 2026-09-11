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
#define NAME_EDIT_STATUS_Y 48U
#define NAME_EDIT_FOOTER_Y 58U
#define NAME_EDIT_NAME_VISIBLE_CHARS 20U
#define NAME_EDIT_FRIEZE_CELLS 13U

typedef struct
{
    uint8_t return_page;
    uint8_t max_chars;
    uint8_t pos;
    uint8_t char_index;
    char title[13];
    char context[21];
    char status[21];
    char name[NAME_CONTRACT_BUFFER_BYTES];
    ui_page_name_edit_done_fn done;
    void *user;
} ui_page_name_edit_state_t;

static ui_page_name_edit_state_t g_name_edit = {
    .max_chars = NAME_CONTRACT_MAX_CHARS,
};

static uint32_t g_name_edit_random_state = 0x6D2B79F5U;
static uint8_t g_name_edit_random_format = 0U;

static const char *const g_name_edit_techno[] = {
    "Flux", "Pulse", "Chrome", "Neon", "Vector", "Phase", "Circuit"
};
static const char *const g_name_edit_industrial[] = {
    "Rotor", "Forge", "Steel", "Torque", "Piston", "Alloy", "Grind"
};
static const char *const g_name_edit_spatial[] = {
    "Lunar", "Orbit", "Nova", "Astro", "Comet", "Zenith", "Quasar"
};
static const char *const g_name_edit_abstract[] = {
    "Haze", "Static", "Shadow", "Void", "Bloom", "Drift", "Echo"
};
static const char *const g_name_edit_urban[] = {
    "Sector", "Metro", "Grid", "Concrete", "Avenue", "Tower", "District"
};
static const char *const g_name_edit_nature[] = {
    "Moss", "Cedar", "River", "Bloom", "Stone", "Ember", "Tide"
};
static const char *const g_name_edit_signal[] = {
    "Bit", "Sync", "Packet", "Kernel", "Binary", "Signal", "Data"
};
static const char *const g_name_edit_music[] = {
    "Chord", "Resonance", "Filter", "Arp", "Wave", "Velvet", "Sub"
};

typedef struct
{
    const char *const *words;
    uint8_t count;
} ui_page_name_edit_word_family_t;

static const ui_page_name_edit_word_family_t g_name_edit_families[] = {
    { g_name_edit_techno, (uint8_t)(sizeof(g_name_edit_techno) / sizeof(g_name_edit_techno[0])) },
    { g_name_edit_industrial, (uint8_t)(sizeof(g_name_edit_industrial) / sizeof(g_name_edit_industrial[0])) },
    { g_name_edit_spatial, (uint8_t)(sizeof(g_name_edit_spatial) / sizeof(g_name_edit_spatial[0])) },
    { g_name_edit_abstract, (uint8_t)(sizeof(g_name_edit_abstract) / sizeof(g_name_edit_abstract[0])) },
    { g_name_edit_urban, (uint8_t)(sizeof(g_name_edit_urban) / sizeof(g_name_edit_urban[0])) },
    { g_name_edit_nature, (uint8_t)(sizeof(g_name_edit_nature) / sizeof(g_name_edit_nature[0])) },
    { g_name_edit_signal, (uint8_t)(sizeof(g_name_edit_signal) / sizeof(g_name_edit_signal[0])) },
    { g_name_edit_music, (uint8_t)(sizeof(g_name_edit_music) / sizeof(g_name_edit_music[0])) },
};

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
    for (uint32_t keep = len; keep > 1U; --keep)
    {
        out[keep - 1U] = '~';
        out[keep] = '\0';
        if (drv_display_text_width(out) <= max_px)
        {
            return;
        }
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

static uint8_t ui_page_name_edit_name_len(void)
{
    uint8_t len = 0U;
    while ((len < g_name_edit.max_chars) && (g_name_edit.name[len] != '\0'))
    {
        ++len;
    }
    return len;
}

static void ui_page_name_edit_clamp_pos(void)
{
    const uint8_t len = ui_page_name_edit_name_len();
    const uint8_t max_pos = (len < g_name_edit.max_chars)
        ? len : (uint8_t)(g_name_edit.max_chars - 1U);
    if (g_name_edit.pos > max_pos)
    {
        g_name_edit.pos = max_pos;
    }
}

static void ui_page_name_edit_sync_char_to_pos(void)
{
    uint8_t index = 0U;
    if ((g_name_edit.pos < ui_page_name_edit_name_len())
            && (name_contract_char_index(g_name_edit.name[g_name_edit.pos], &index) != 0U))
    {
        g_name_edit.char_index = index;
    }
    else
    {
        (void)name_contract_char_index(' ', &g_name_edit.char_index);
    }
}

static void ui_page_name_edit_set_status(const char *status)
{
    memset(g_name_edit.status, 0, sizeof(g_name_edit.status));
    if (status != 0)
    {
        (void)snprintf(g_name_edit.status, sizeof(g_name_edit.status), "%s", status);
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

static void ui_page_name_edit_write_current_char(void)
{
    ui_page_name_edit_clamp_pos();
    if (g_name_edit.max_chars == 0U)
    {
        return;
    }
    g_name_edit.name[g_name_edit.pos] =
        name_contract_alphabet_char(g_name_edit.char_index);
    g_name_edit.name[g_name_edit.max_chars] = '\0';
}

static void ui_page_name_edit_random(void)
{
    char candidate[NAME_CONTRACT_BUFFER_BYTES];
    const uint32_t state = g_name_edit_random_state =
        (g_name_edit_random_state * 1664525U) + 1013904223U;
    const uint8_t family_index = (uint8_t)(state % (sizeof(g_name_edit_families)
                                                    / sizeof(g_name_edit_families[0])));
    const ui_page_name_edit_word_family_t *const family = &g_name_edit_families[family_index];
    const char *const first = family->words[(state >> 8U) % family->count];
    const char *const second = family->words[(state >> 16U) % family->count];
    const uint8_t format = g_name_edit_random_format;
    g_name_edit_random_format = (uint8_t)((g_name_edit_random_format + 1U) % 4U);

    switch (format)
    {
        case 0U:
            (void)snprintf(candidate, sizeof(candidate), "%s", first);
            break;
        case 1U:
            (void)snprintf(candidate, sizeof(candidate), "%s %s", first, second);
            break;
        case 2U:
            (void)snprintf(candidate, sizeof(candidate), "%s %02u", first,
                           (unsigned)((state >> 4U) % 100U));
            break;
        default:
            (void)snprintf(candidate, sizeof(candidate), "%s %c%u", first,
                           (char)('A' + ((state >> 12U) % 26U)),
                           (unsigned)((state >> 20U) % 10U));
            break;
    }

    if (name_contract_normalize(candidate, g_name_edit.name) != NAME_CONTRACT_RESULT_OK)
    {
        (void)snprintf(g_name_edit.name, sizeof(g_name_edit.name), "Flux");
    }
    g_name_edit.pos = 0U;
    ui_page_name_edit_sync_char_to_pos();
    ui_page_name_edit_set_status(0);
}

static void ui_page_name_edit_delete(void)
{
    const uint8_t len = ui_page_name_edit_name_len();
    if (len == 0U)
    {
        return;
    }

    uint8_t target = g_name_edit.pos;
    if (target >= len)
    {
        target = (uint8_t)(len - 1U);
        g_name_edit.pos = target;
    }
    for (uint8_t i = target; i < g_name_edit.max_chars; ++i)
    {
        g_name_edit.name[i] = g_name_edit.name[i + 1U];
        if (g_name_edit.name[i] == '\0')
        {
            break;
        }
    }
    g_name_edit.name[g_name_edit.max_chars] = '\0';
    ui_page_name_edit_clamp_pos();
    ui_page_name_edit_sync_char_to_pos();
    ui_page_name_edit_set_status(0);
}

static void ui_page_name_edit_finish(ui_page_name_edit_result_t result,
                                     const char *confirmed_name)
{
    ui_page_name_edit_done_fn done = g_name_edit.done;
    void *user = g_name_edit.user;
    char name[NAME_CONTRACT_BUFFER_BYTES] = { 0 };
    if ((result == UI_PAGE_NAME_EDIT_RESULT_CONFIRM) && (confirmed_name != 0))
    {
        memcpy(name, confirmed_name, sizeof(name));
        name[sizeof(name) - 1U] = '\0';
    }

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
    if ((done == 0) || (max_len < 2U) || (max_len > NAME_CONTRACT_BUFFER_BYTES))
    {
        return 0U;
    }

    char initial_name[NAME_CONTRACT_BUFFER_BYTES] = { 0 };
    if (initial != 0)
    {
        const name_contract_result_t initial_result =
            name_contract_normalize(initial, initial_name);
        if ((initial_result != NAME_CONTRACT_RESULT_OK)
                && (initial_result != NAME_CONTRACT_RESULT_EMPTY))
        {
            return 0U;
        }
        if (strlen(initial_name) >= max_len)
        {
            return 0U;
        }
    }

    memset(&g_name_edit, 0, sizeof(g_name_edit));
    g_name_edit.return_page = return_page;
    g_name_edit.max_chars = (uint8_t)(max_len - 1U);
    g_name_edit.done = done;
    g_name_edit.user = user;
    (void)snprintf(g_name_edit.title, sizeof(g_name_edit.title), "%s",
                   (title != 0) ? title : "NAME");
    (void)snprintf(g_name_edit.context, sizeof(g_name_edit.context), "%s",
                   (context != 0) ? context : "");
    memcpy(g_name_edit.name, initial_name, sizeof(g_name_edit.name));
    g_name_edit.name[g_name_edit.max_chars] = '\0';
    ui_page_name_edit_clamp_pos();
    ui_page_name_edit_sync_char_to_pos();
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
        int32_t next = (int32_t)g_name_edit.char_index + (int32_t)delta;
        if (next < 0)
        {
            next = 0;
        }
        if (next >= (int32_t)name_contract_alphabet_size())
        {
            next = (int32_t)name_contract_alphabet_size() - 1;
        }
        g_name_edit.char_index = (uint8_t)next;
        ui_page_name_edit_write_current_char();
        ui_page_name_edit_set_status(0);
        return 1U;
    }

    if (encoder == 1U)
    {
        int32_t next = (int32_t)g_name_edit.pos + (int32_t)delta;
        if (next < 0)
        {
            next = 0;
        }
        if (next >= (int32_t)g_name_edit.max_chars)
        {
            next = (int32_t)g_name_edit.max_chars - 1;
        }
        g_name_edit.pos = (uint8_t)next;
        ui_page_name_edit_clamp_pos();
        ui_page_name_edit_sync_char_to_pos();
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
            if (button_down(BTN_SHIFT) == 0U)
            {
                ui_page_name_edit_random();
            }
            break;
        case BTN_PAGE_2:
            if (button_down(BTN_SHIFT) == 0U)
            {
                ui_page_name_edit_delete();
            }
            break;
        case BTN_PAGE_3:
            if (button_down(BTN_SHIFT) == 0U)
            {
                char normalized[NAME_CONTRACT_BUFFER_BYTES];
                const name_contract_result_t result =
                    name_contract_normalize(g_name_edit.name, normalized);
                if (result == NAME_CONTRACT_RESULT_OK)
                {
                    ui_page_name_edit_finish(UI_PAGE_NAME_EDIT_RESULT_CONFIRM, normalized);
                }
                else if (result == NAME_CONTRACT_RESULT_EMPTY)
                {
                    ui_page_name_edit_set_status("NAME EMPTY");
                }
                else if (result == NAME_CONTRACT_RESULT_TOO_LONG)
                {
                    ui_page_name_edit_set_status("NAME TOO LONG");
                }
                else
                {
                    ui_page_name_edit_set_status("INVALID CHAR");
                }
            }
            break;
        case BTN_PAGE_4:
            if (button_down(BTN_SHIFT) == 0U)
            {
                ui_page_name_edit_finish(UI_PAGE_NAME_EDIT_RESULT_CANCEL, 0);
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
    if ((first + visible) > g_name_edit.max_chars)
    {
        first = (g_name_edit.max_chars > visible)
            ? (uint8_t)(g_name_edit.max_chars - visible) : 0U;
    }

    memset(text, 0, sizeof(text));
    for (uint8_t i = 0U; i < visible; ++i)
    {
        const uint8_t src = (uint8_t)(first + i);
        if (src >= g_name_edit.max_chars)
        {
            break;
        }
        text[i] = (g_name_edit.name[src] == '\0') ? ' ' : g_name_edit.name[src];
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
            ? ' ' : g_name_edit.name[g_name_edit.pos];
        char one[2] = { c, '\0' };
        drv_display_fill_rect(x, (uint8_t)(NAME_EDIT_NAME_Y - 1U),
                              (uint8_t)(glyph_w + 2U), (uint8_t)(font_h + 2U));
        drv_display_draw_text_inverted(x, NAME_EDIT_NAME_Y, one);
    }
}

static void ui_page_name_edit_draw_frieze(void)
{
    const uint8_t char_count = name_contract_alphabet_size();
    const uint8_t current = (g_name_edit.char_index < char_count)
        ? g_name_edit.char_index : 0U;
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
        const char c = name_contract_alphabet_char((uint8_t)index);
        char one[2] = { c, '\0' };
        const uint8_t text_w = drv_display_text_width(one);
        const uint8_t text_x = (text_w >= cell_w)
            ? cell_x : (uint8_t)(cell_x + ((cell_w - text_w) / 2U));
        if (cell == (uint8_t)center)
        {
            drv_display_fill_rect(cell_x, (uint8_t)(NAME_EDIT_FRIEZE_Y - 1U),
                                  cell_w, (uint8_t)(font_h + 2U));
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
    char fit[NAME_CONTRACT_BUFFER_BYTES];
    drv_display_set_font(&FONT_4X6);
    ui_page_name_edit_fit_label(fit, sizeof(fit), g_name_edit.title, 56U);
    drv_display_draw_text(0U, 0U, fit);
    ui_page_name_edit_fit_label(fit, sizeof(fit), g_name_edit.context, 62U);
    drv_display_draw_text(64U, 0U, fit);
    drv_display_draw_line(0, NAME_EDIT_HEADER_LINE_Y, 127, NAME_EDIT_HEADER_LINE_Y);
    ui_page_name_edit_draw_name();
    ui_page_name_edit_draw_frieze();
    if (g_name_edit.status[0] != '\0')
    {
        ui_page_name_edit_draw_centered_label(0U, 128U, NAME_EDIT_STATUS_Y,
                                              g_name_edit.status);
    }
    ui_page_name_edit_draw_centered_label(0U, 32U, NAME_EDIT_FOOTER_Y, "RANDOM");
    ui_page_name_edit_draw_centered_label(32U, 32U, NAME_EDIT_FOOTER_Y, "DELETE");
    ui_page_name_edit_draw_centered_label(64U, 32U, NAME_EDIT_FOOTER_Y, "SAVE");
    ui_page_name_edit_draw_centered_label(96U, 32U, NAME_EDIT_FOOTER_Y, "CANCEL");
}

const ui_page_t g_ui_page_name_edit = {
    .enter = 0,
    .leave = 0,
    .handle_encoder = ui_page_name_edit_handle_encoder,
    .handle_event = ui_page_name_edit_handle_event,
    .tick = 0,
    .sync_active_context = 0,
    .render = ui_page_name_edit_render,
    .context = 0,
};
