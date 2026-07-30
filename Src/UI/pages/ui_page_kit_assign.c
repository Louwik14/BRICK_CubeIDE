#include "pages/ui_page_kit_assign.h"

#include <stdio.h>
#include <string.h>

#include "buttons.h"
#include "drv_display.h"
#include "font.h"
#include "Storage/kit_sd_bank.h"
#include "Storage/kit_v1.h"
#include "Storage/pattern_live_ram.h"
#include "pages/ui_page_name_edit.h"
#include "ui_event.h"
#include "ui_page_manager.h"

typedef struct
{
    uint16_t selected_slot;
    uint8_t previous_page;
    uint8_t delete_confirm;
    char status[24];
} ui_page_kit_assign_state_t;

static ui_page_kit_assign_state_t g_kit_assign = {
    .selected_slot = 0U,
    .previous_page = UI_PAGE_TEMPLATE_CFG,
    .delete_confirm = 0U,
    .status = { 0 },
};

#define KIT_ASSIGN_HEADER_LINE_Y 7U
#define KIT_ASSIGN_LIST_Y0 13U
#define KIT_ASSIGN_LIST_PITCH 8U
#define KIT_ASSIGN_LIST_VISIBLE_LINES 2U
#define KIT_ASSIGN_MINI_X 8U
#define KIT_ASSIGN_MINI_Y 31U
#define KIT_ASSIGN_MINI_CELL_W 14U
#define KIT_ASSIGN_MINI_CELL_H 10U
#define KIT_ASSIGN_SELECT_H 8U
#define KIT_ASSIGN_FOOTER_LABEL_Y 58U
#define KIT_ASSIGN_SCROLL_X 126U

static void ui_page_kit_assign_name_done(ui_page_name_edit_result_t result,
                                         const char *name,
                                         void *user);

static void ui_page_kit_assign_set_status(const char *status)
{
    memset(g_kit_assign.status, 0, sizeof(g_kit_assign.status));
    if (status != 0)
    {
        (void)snprintf(g_kit_assign.status, sizeof(g_kit_assign.status), "%s", status);
    }
}

static void ui_page_kit_assign_fit_label(char *out,
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

static void ui_page_kit_assign_draw_centered_label(uint8_t x,
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

static uint8_t ui_page_kit_assign_slot_visible(uint16_t slot)
{
    return (kit_sd_bank_get_slot_state(slot) == KIT_SD_SLOT_VALID) ? 1U : 0U;
}

static uint16_t ui_page_kit_assign_visible_count(void)
{
    uint16_t count = 0U;
    for (uint16_t slot = 0U; slot < KIT_V1_SLOT_COUNT; ++slot)
    {
        if (ui_page_kit_assign_slot_visible(slot) != 0U)
        {
            ++count;
        }
    }
    return count;
}

static uint16_t ui_page_kit_assign_slot_for_view_index(uint16_t index)
{
    uint16_t seen = 0U;
    for (uint16_t slot = 0U; slot < KIT_V1_SLOT_COUNT; ++slot)
    {
        if (ui_page_kit_assign_slot_visible(slot) == 0U)
        {
            continue;
        }
        if (seen == index)
        {
            return slot;
        }
        ++seen;
    }
    return KIT_V1_INVALID_SLOT;
}

static uint16_t ui_page_kit_assign_view_index_for_slot(uint16_t selected_slot)
{
    uint16_t seen = 0U;
    for (uint16_t slot = 0U; slot < KIT_V1_SLOT_COUNT; ++slot)
    {
        if (ui_page_kit_assign_slot_visible(slot) == 0U)
        {
            continue;
        }
        if (slot == selected_slot)
        {
            return seen;
        }
        ++seen;
    }
    return KIT_V1_INVALID_SLOT;
}

static void ui_page_kit_assign_ensure_visible_selection(void)
{
    if (ui_page_kit_assign_slot_visible(g_kit_assign.selected_slot) != 0U)
    {
        return;
    }

    const uint16_t first_slot = ui_page_kit_assign_slot_for_view_index(0U);
    if (first_slot < KIT_V1_SLOT_COUNT)
    {
        g_kit_assign.selected_slot = first_slot;
    }
}

static void ui_page_kit_assign_step_selection(int16_t delta)
{
    g_kit_assign.delete_confirm = 0U;
    const uint16_t count = ui_page_kit_assign_visible_count();
    if (count == 0U)
    {
        ui_page_kit_assign_set_status("NO KIT");
        return;
    }

    uint16_t index = ui_page_kit_assign_view_index_for_slot(g_kit_assign.selected_slot);
    if (index >= count)
    {
        index = 0U;
    }

    int32_t next = (int32_t)index + (int32_t)delta;
    if (next < 0)
    {
        next = 0;
    }
    if (next >= (int32_t)count)
    {
        next = (int32_t)count - 1;
    }

    const uint16_t slot = ui_page_kit_assign_slot_for_view_index((uint16_t)next);
    if (slot < KIT_V1_SLOT_COUNT)
    {
        g_kit_assign.selected_slot = slot;
        ui_page_kit_assign_set_status(0);
    }
}

static void ui_page_kit_assign_begin_rename(void);
static void ui_page_kit_assign_delete_action(void);
static void ui_page_kit_assign_apply_action(void);

static void ui_page_kit_assign_enter(void)
{
}

void ui_page_kit_assign_open(void)
{
    const uint8_t current_page = ui_page_get_id();
    if (current_page != UI_PAGE_KIT_ASSIGN)
    {
        g_kit_assign.previous_page = current_page;
    }

    const uint16_t current_slot = kit_v1_get_current_slot();
    if (current_slot < KIT_V1_SLOT_COUNT)
    {
        g_kit_assign.selected_slot = current_slot;
    }
    else
    {
        g_kit_assign.selected_slot = 0U;
    }
    ui_page_kit_assign_ensure_visible_selection();
    g_kit_assign.delete_confirm = 0U;
    ui_page_kit_assign_set_status(ui_page_kit_assign_visible_count() == 0U ? "NO KIT" : 0);
    ui_page_set(UI_PAGE_KIT_ASSIGN);
}

void ui_page_kit_assign_close(void)
{
    if (ui_page_kit_assign_is_open() != 0U)
    {
        ui_page_set(g_kit_assign.previous_page);
    }
}

static void ui_page_kit_assign_handle_event(const ui_event_t *ev)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        return;
    }

    switch ((button_id_t)ev->id)
    {
        case BTN_PAGE_1:
            ui_page_kit_assign_close();
            break;

        case BTN_PAGE_2:
            g_kit_assign.delete_confirm = 0U;
            ui_page_kit_assign_apply_action();
            break;

        case BTN_PAGE_3:
            ui_page_kit_assign_begin_rename();
            break;

        case BTN_PAGE_4:
            ui_page_kit_assign_delete_action();
            break;

        default:
            g_kit_assign.delete_confirm = 0U;
            break;
    }
}

uint8_t ui_page_kit_assign_handle_encoder(uint8_t encoder, int16_t delta)
{
    if (delta == 0)
    {
        return 1U;
    }

    if (encoder == 0U)
    {
        g_kit_assign.delete_confirm = 0U;
        ui_page_kit_assign_step_selection(delta);
        return 1U;
    }

    return 1U;
}


static void ui_page_kit_assign_begin_rename(void)
{
    kit_v1_metadata_t meta;
    char name[KIT_V1_NAME_MAX];
    if (ui_page_kit_assign_visible_count() == 0U)
    {
        ui_page_kit_assign_set_status("NO KIT");
        return;
    }
    if (ui_page_kit_assign_slot_visible(g_kit_assign.selected_slot) == 0U)
    {
        ui_page_kit_assign_set_status("BAD KIT");
        return;
    }
    if (kit_sd_bank_get_slot_metadata(g_kit_assign.selected_slot, &meta) == 0U)
    {
        ui_page_kit_assign_set_status("BAD KIT");
        return;
    }

    memset(name, 0, sizeof(name));
    memcpy(name, meta.name, sizeof(name));
    if (name[0] == '\0')
    {
        (void)snprintf(name, sizeof(name), "KIT %03u", (unsigned)g_kit_assign.selected_slot);
    }
    g_kit_assign.delete_confirm = 0U;

    if (ui_page_name_edit_open(UI_PAGE_KIT_ASSIGN,
                               "KIT",
                               "RENAME",
                               name,
                               KIT_V1_NAME_MAX,
                               ui_page_kit_assign_name_done,
                               0) == 0U)
    {
        ui_page_kit_assign_set_status("ERROR");
    }
}

static void ui_page_kit_assign_name_done(ui_page_name_edit_result_t result,
                                         const char *name,
                                         void *user)
{
    (void)user;
    if (result != UI_PAGE_NAME_EDIT_RESULT_OK)
    {
        ui_page_kit_assign_set_status(0);
        return;
    }

    const kit_v1_result_t rename_result =
        kit_v1_rename_slot(g_kit_assign.selected_slot, name);
    ui_page_kit_assign_ensure_visible_selection();
    ui_page_kit_assign_set_status((rename_result == KIT_V1_RESULT_OK)
                                  ? "KIT RENAMED"
                                  : kit_v1_result_label(rename_result));
}

static void ui_page_kit_assign_apply_action(void)
{
    if (ui_page_kit_assign_visible_count() == 0U)
    {
        ui_page_kit_assign_set_status("NO KIT");
        return;
    }
    if (ui_page_kit_assign_slot_visible(g_kit_assign.selected_slot) == 0U)
    {
        ui_page_kit_assign_set_status("BAD KIT");
        return;
    }

    const kit_v1_result_t result = kit_v1_apply_slot(g_kit_assign.selected_slot);
    if (result == KIT_V1_RESULT_OK)
    {
        (void)pattern_live_link_active_kit(g_kit_assign.selected_slot);
    }
    ui_page_kit_assign_ensure_visible_selection();
    ui_page_kit_assign_set_status((result == KIT_V1_RESULT_OK)
                                  ? "KIT APPLIED"
                                  : kit_v1_result_label(result));
}

static void ui_page_kit_assign_delete_action(void)
{
    if (ui_page_kit_assign_visible_count() == 0U)
    {
        ui_page_kit_assign_set_status("NO KIT");
        g_kit_assign.delete_confirm = 0U;
        return;
    }
    if (ui_page_kit_assign_slot_visible(g_kit_assign.selected_slot) == 0U)
    {
        ui_page_kit_assign_set_status("BAD KIT");
        g_kit_assign.delete_confirm = 0U;
        return;
    }

    if (g_kit_assign.delete_confirm == 0U)
    {
        g_kit_assign.delete_confirm = 1U;
        ui_page_kit_assign_set_status("DELETE?");
        return;
    }

    uint16_t next_slot = g_kit_assign.selected_slot;
    const kit_v1_result_t result = kit_v1_delete_slot(g_kit_assign.selected_slot, &next_slot);
    if (result == KIT_V1_RESULT_OK)
    {
        pattern_live_clear_active_kit_link_if_slot(g_kit_assign.selected_slot);
        g_kit_assign.selected_slot = next_slot;
        ui_page_kit_assign_ensure_visible_selection();
        ui_page_kit_assign_set_status((ui_page_kit_assign_visible_count() == 0U)
                                      ? "NO KIT"
                                      : "KIT DELETED");
    }
    else
    {
        ui_page_kit_assign_set_status(kit_v1_result_label(result));
    }
    g_kit_assign.delete_confirm = 0U;
}

uint8_t ui_page_kit_assign_is_open(void)
{
    return (ui_page_get_id() == UI_PAGE_KIT_ASSIGN) ? 1U : 0U;
}


static void ui_page_kit_assign_draw_miniature(void)
{
    kit_v1_metadata_t meta;
    if ((ui_page_kit_assign_slot_visible(g_kit_assign.selected_slot) == 0U)
            || (kit_sd_bank_get_slot_metadata(g_kit_assign.selected_slot, &meta) == 0U))
    {
        return;
    }

    drv_display_set_font(&FONT_4X6);
    for (uint8_t track = 0U; track < KIT_V1_TRACK_MAX; ++track)
    {
        const uint8_t col = (uint8_t)(track & 0x07U);
        const uint8_t row = (uint8_t)(track >> 3U);
        const uint8_t x = (uint8_t)(KIT_ASSIGN_MINI_X + (col * KIT_ASSIGN_MINI_CELL_W));
        const uint8_t y = (uint8_t)(KIT_ASSIGN_MINI_Y + (row * KIT_ASSIGN_MINI_CELL_H));
        const kit_v1_track_summary_t *const summary = &meta.summary[track];
        const uint8_t empty = ((track >= meta.track_count)
                               || (summary->off != 0U)
                               || (summary->label_code == (uint8_t)KIT_V1_LABEL_OFF)) ? 1U : 0U;

        drv_display_draw_rect(x, y, KIT_ASSIGN_MINI_CELL_W - 1, KIT_ASSIGN_MINI_CELL_H - 1);
        if (empty != 0U)
        {
            drv_display_draw_line(x + 2, y + 2, x + KIT_ASSIGN_MINI_CELL_W - 4, y + KIT_ASSIGN_MINI_CELL_H - 4);
            drv_display_draw_line(x + KIT_ASSIGN_MINI_CELL_W - 4, y + 2, x + 2, y + KIT_ASSIGN_MINI_CELL_H - 4);
            continue;
        }

        const char *const label = kit_v1_label_code_short_name(summary->label_code);
        const uint8_t text_w = drv_display_text_width(label);
        const uint8_t text_x = (text_w >= (KIT_ASSIGN_MINI_CELL_W - 2U))
            ? (uint8_t)(x + 1U)
            : (uint8_t)(x + ((KIT_ASSIGN_MINI_CELL_W - text_w) / 2U));
        drv_display_draw_text(text_x, (uint8_t)(y + 2U), label);
    }
}

static void ui_page_kit_assign_draw_row(uint8_t row, uint16_t slot, uint8_t selected)
{
    char line[40];
    char fit[40];
    const uint8_t y = (uint8_t)(KIT_ASSIGN_LIST_Y0 + (row * KIT_ASSIGN_LIST_PITCH));
    kit_v1_metadata_t meta;

    if (kit_sd_bank_get_slot_metadata(slot, &meta) != 0U)
    {
        char short_name[19];
        memset(short_name, 0, sizeof(short_name));
        for (uint8_t i = 0U; i < (uint8_t)(sizeof(short_name) - 1U); ++i)
        {
            short_name[i] = meta.name[i];
            if (meta.name[i] == '\0')
            {
                break;
            }
        }
        if (short_name[0] == '\0')
        {
            (void)snprintf(short_name, sizeof(short_name), "KIT %03u", (unsigned)slot);
        }
        (void)snprintf(line,
                       sizeof(line),
                       "%c%-17s %02uT",
                       (slot == kit_v1_get_current_slot()) ? '*' : ' ',
                       short_name,
                       (unsigned)meta.track_count);
    }
    else
    {
        (void)snprintf(line, sizeof(line), "BAD KIT");
    }

    ui_page_kit_assign_fit_label(fit, sizeof(fit), line, 122U);
    if (selected != 0U)
    {
        drv_display_fill_rect(0U, (uint8_t)(y - 1U), 124U, KIT_ASSIGN_SELECT_H);
        drv_display_draw_text_inverted(1U, y, fit);
    }
    else
    {
        drv_display_draw_text(1U, y, fit);
    }
}

static void ui_page_kit_assign_draw_position(uint16_t selected_view, uint16_t visible_count)
{
    enum
    {
        POS_Y0 = KIT_ASSIGN_LIST_Y0 - 1U,
        POS_Y1 = 51,
        POS_H = POS_Y1 - POS_Y0,
        POS_CURSOR_MAX_H = 9,
        POS_CURSOR_MIN_H = 3
    };

    drv_display_draw_line(KIT_ASSIGN_SCROLL_X, POS_Y0, KIT_ASSIGN_SCROLL_X, POS_Y1);
    if (visible_count <= 1U)
    {
        drv_display_draw_pixel(KIT_ASSIGN_SCROLL_X, POS_Y0, true);
        return;
    }

    uint8_t cursor_h = (uint8_t)((uint32_t)POS_H * KIT_ASSIGN_LIST_VISIBLE_LINES / visible_count);
    if (cursor_h > POS_CURSOR_MAX_H)
    {
        cursor_h = POS_CURSOR_MAX_H;
    }
    if (cursor_h < POS_CURSOR_MIN_H)
    {
        cursor_h = POS_CURSOR_MIN_H;
    }

    const uint8_t travel = (POS_H > cursor_h) ? (uint8_t)(POS_H - cursor_h) : 0U;
    const uint8_t cursor_y = (uint8_t)(POS_Y0
                                       + (((uint32_t)selected_view * travel)
                                          / (visible_count - 1U)));
    drv_display_fill_rect(KIT_ASSIGN_SCROLL_X, cursor_y, 2U, cursor_h);
}

static void ui_page_kit_assign_draw_footer(void)
{
    ui_page_kit_assign_draw_centered_label(0U, 32U, KIT_ASSIGN_FOOTER_LABEL_Y, "RETURN");
    ui_page_kit_assign_draw_centered_label(32U, 32U, KIT_ASSIGN_FOOTER_LABEL_Y, "APPLY");
    ui_page_kit_assign_draw_centered_label(64U, 32U, KIT_ASSIGN_FOOTER_LABEL_Y, "REN");
    ui_page_kit_assign_draw_centered_label(96U, 32U, KIT_ASSIGN_FOOTER_LABEL_Y, "DEL");
}

static void ui_page_kit_assign_render(void)
{
    const uint16_t visible_count = ui_page_kit_assign_visible_count();
    uint16_t selected_view = ui_page_kit_assign_view_index_for_slot(g_kit_assign.selected_slot);
    if ((visible_count == 0U) || (selected_view >= visible_count))
    {
        selected_view = 0U;
    }

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text(0U, 0U, "KIT");
    drv_display_draw_line(0, KIT_ASSIGN_HEADER_LINE_Y, 127, KIT_ASSIGN_HEADER_LINE_Y);

    if (visible_count == 0U)
    {
        drv_display_set_font(&FONT_5X7);
        ui_page_kit_assign_draw_centered_label(0U, 128U, 34U, "NO KIT");
    }
    else
    {
        uint16_t first_view = 0U;
        if (selected_view > 0U)
        {
            first_view = (uint16_t)(selected_view - 1U);
        }
        if ((visible_count > KIT_ASSIGN_LIST_VISIBLE_LINES)
                && (first_view > (uint16_t)(visible_count - KIT_ASSIGN_LIST_VISIBLE_LINES)))
        {
            first_view = (uint16_t)(visible_count - KIT_ASSIGN_LIST_VISIBLE_LINES);
        }

        drv_display_set_font(&FONT_4X6);
        for (uint8_t row = 0U; row < KIT_ASSIGN_LIST_VISIBLE_LINES; ++row)
        {
            const uint16_t view_index = (uint16_t)(first_view + row);
            if (view_index >= visible_count)
            {
                break;
            }

            const uint16_t slot = ui_page_kit_assign_slot_for_view_index(view_index);
            if (slot >= KIT_V1_SLOT_COUNT)
            {
                break;
            }
            ui_page_kit_assign_draw_row(row,
                                        slot,
                                        (slot == g_kit_assign.selected_slot) ? 1U : 0U);
        }
        ui_page_kit_assign_draw_position(selected_view, visible_count);
        ui_page_kit_assign_draw_miniature();
    }

    if (g_kit_assign.status[0] != '\0')
    {
        drv_display_set_font(&FONT_4X6);
        drv_display_draw_text(0U, 54U, g_kit_assign.status);
    }
    else
    {
        ui_page_kit_assign_draw_footer();
    }
}

const ui_page_t g_ui_page_kit_assign = {
    .enter = ui_page_kit_assign_enter,
    .leave = 0,
    .handle_event = ui_page_kit_assign_handle_event,
    .tick = 0,
    .sync_active_context = 0,
    .render = ui_page_kit_assign_render,
    .context = 0,
};
