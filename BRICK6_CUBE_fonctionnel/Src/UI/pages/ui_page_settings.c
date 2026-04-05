#include "pages/ui_page_settings.h"

#include <stdio.h>
#include <string.h>

#include "Storage/project_v1.h"
#include "drv_display.h"
#include "ui_page_manager.h"

typedef enum
{
    UI_SETTINGS_VIEW_ROOT = 0,
    UI_SETTINGS_VIEW_PROJECT,
    UI_SETTINGS_VIEW_PROJECT_LOAD,
    UI_SETTINGS_VIEW_PROJECT_SAVE_AS,
    UI_SETTINGS_VIEW_PROJECT_MANAGE,
    UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT,

    UI_SETTINGS_VIEW_COUNT
} ui_settings_view_t;

typedef enum
{
    UI_SETTINGS_MANAGE_ACTION_LOAD_FROM = 0,
    UI_SETTINGS_MANAGE_ACTION_SAVE_TO,
    UI_SETTINGS_MANAGE_ACTION_DELETE,

    UI_SETTINGS_MANAGE_ACTION_COUNT
} ui_settings_manage_action_t;

typedef struct
{
    ui_settings_view_t view;
    uint8_t selected_index;
} ui_settings_menu_level_t;

#define UI_SETTINGS_MAX_LEVELS 6U

typedef struct
{
    ui_settings_menu_level_t levels[UI_SETTINGS_MAX_LEVELS];
    uint8_t depth;
    uint8_t selected_slot;
    uint8_t return_page_id;
    char status_line[20];
} ui_settings_state_t;

static ui_settings_state_t g_ui_settings;

static const char *ui_page_settings_view_title(ui_settings_view_t view)
{
    switch (view)
    {
        case UI_SETTINGS_VIEW_ROOT:
            return "SETTINGS";
        case UI_SETTINGS_VIEW_PROJECT:
            return "PROJECT";
        case UI_SETTINGS_VIEW_PROJECT_LOAD:
            return "PROJECT > LOAD";
        case UI_SETTINGS_VIEW_PROJECT_SAVE_AS:
            return "PROJECT > SAVE AS";
        case UI_SETTINGS_VIEW_PROJECT_MANAGE:
            return "PROJECT > MANAGE";
        case UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT:
            return "MANAGE > SLOT";
        default:
            return "SETTINGS";
    }
}

static uint8_t ui_page_settings_view_item_count(ui_settings_view_t view)
{
    switch (view)
    {
        case UI_SETTINGS_VIEW_ROOT:
            return 1U;
        case UI_SETTINGS_VIEW_PROJECT:
            return 3U;
        case UI_SETTINGS_VIEW_PROJECT_LOAD:
        case UI_SETTINGS_VIEW_PROJECT_SAVE_AS:
        case UI_SETTINGS_VIEW_PROJECT_MANAGE:
            return PROJECT_V1_SLOT_COUNT;
        case UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT:
            return (uint8_t)UI_SETTINGS_MANAGE_ACTION_COUNT;
        default:
            return 0U;
    }
}

static const char *ui_page_settings_item_label(ui_settings_view_t view, uint8_t index, char *out, uint32_t out_size)
{
    (void)out_size;
    switch (view)
    {
        case UI_SETTINGS_VIEW_ROOT:
            return "PROJECT";
        case UI_SETTINGS_VIEW_PROJECT:
            if (index == 0U)
            {
                return "LOAD";
            }
            if (index == 1U)
            {
                return "SAVE AS";
            }
            return "MANAGE";
        case UI_SETTINGS_VIEW_PROJECT_LOAD:
        case UI_SETTINGS_VIEW_PROJECT_SAVE_AS:
        case UI_SETTINGS_VIEW_PROJECT_MANAGE:
            (void)snprintf(out, out_size, "PROJECT %02u", (unsigned)index);
            return out;
        case UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT:
            if (index == (uint8_t)UI_SETTINGS_MANAGE_ACTION_LOAD_FROM)
            {
                return "LOAD FROM";
            }
            if (index == (uint8_t)UI_SETTINGS_MANAGE_ACTION_SAVE_TO)
            {
                return "SAVE TO";
            }
            return "DELETE";
        default:
            return "-";
    }
}

static ui_settings_menu_level_t *ui_page_settings_current_level(void)
{
    if ((g_ui_settings.depth == 0U) || (g_ui_settings.depth > UI_SETTINGS_MAX_LEVELS))
    {
        return 0;
    }

    return &g_ui_settings.levels[g_ui_settings.depth - 1U];
}

static void ui_page_settings_status(const char *status)
{
    if (status == 0)
    {
        g_ui_settings.status_line[0] = '\0';
        return;
    }

    (void)snprintf(g_ui_settings.status_line,
                   sizeof(g_ui_settings.status_line),
                   "%s",
                   status);
}

static void ui_page_settings_close(void)
{
    ui_page_set(g_ui_settings.return_page_id);
}

static void ui_page_settings_push(ui_settings_view_t view)
{
    if (g_ui_settings.depth >= UI_SETTINGS_MAX_LEVELS)
    {
        return;
    }

    g_ui_settings.levels[g_ui_settings.depth].view = view;
    g_ui_settings.levels[g_ui_settings.depth].selected_index = 0U;
    g_ui_settings.depth++;
}

static void ui_page_settings_back(void)
{
    if (g_ui_settings.depth <= 1U)
    {
        ui_page_settings_close();
        return;
    }

    g_ui_settings.depth--;
}

static void ui_page_settings_apply_action(void)
{
    ui_settings_menu_level_t *const level = ui_page_settings_current_level();
    if (level == 0)
    {
        return;
    }

    switch (level->view)
    {
        case UI_SETTINGS_VIEW_ROOT:
            ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT);
            break;

        case UI_SETTINGS_VIEW_PROJECT:
            if (level->selected_index == 0U)
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT_LOAD);
            }
            else if (level->selected_index == 1U)
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT_SAVE_AS);
            }
            else
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT_MANAGE);
            }
            break;

        case UI_SETTINGS_VIEW_PROJECT_LOAD:
            if (project_v1_load_slot(level->selected_index) != 0U)
            {
                ui_page_settings_status("LOAD OK");
            }
            else
            {
                ui_page_settings_status("LOAD FAIL");
            }
            break;

        case UI_SETTINGS_VIEW_PROJECT_SAVE_AS:
            if (project_v1_save_slot(level->selected_index) != 0U)
            {
                ui_page_settings_status("SAVE OK");
            }
            else
            {
                ui_page_settings_status("SAVE FAIL");
            }
            break;

        case UI_SETTINGS_VIEW_PROJECT_MANAGE:
            g_ui_settings.selected_slot = level->selected_index;
            ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT);
            break;

        case UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT:
            if (level->selected_index == (uint8_t)UI_SETTINGS_MANAGE_ACTION_LOAD_FROM)
            {
                if (project_v1_load_slot(g_ui_settings.selected_slot) != 0U)
                {
                    ui_page_settings_status("LOAD FROM OK");
                }
                else
                {
                    ui_page_settings_status("LOAD FROM FAIL");
                }
            }
            else if (level->selected_index == (uint8_t)UI_SETTINGS_MANAGE_ACTION_SAVE_TO)
            {
                if (project_v1_save_slot(g_ui_settings.selected_slot) != 0U)
                {
                    ui_page_settings_status("SAVE TO OK");
                }
                else
                {
                    ui_page_settings_status("SAVE TO FAIL");
                }
            }
            else if (project_v1_delete_slot(g_ui_settings.selected_slot) != 0U)
            {
                ui_page_settings_status("DELETE OK");
            }
            else
            {
                ui_page_settings_status("DELETE FAIL");
            }
            break;

        default:
            break;
    }
}

static void ui_page_settings_enter(void)
{
    g_ui_settings.depth = 0U;
    g_ui_settings.selected_slot = 0U;
    ui_page_settings_status(0);
    ui_page_settings_push(UI_SETTINGS_VIEW_ROOT);
}

static void ui_page_settings_leave(void)
{
    g_ui_settings.depth = 0U;
}

static void ui_page_settings_handle_event_internal(const ui_event_t *ev)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        return;
    }

    if (ev->id == (uint8_t)BTN_COPY)
    {
        ui_page_settings_apply_action();
        return;
    }

    if ((ev->id == (uint8_t)BTN_PASTE) || (ev->id == (uint8_t)BTN_SETTINGS))
    {
        ui_page_settings_back();
        return;
    }
}

static void ui_page_settings_tick(void) {}

static void ui_page_settings_render(void)
{
    ui_settings_menu_level_t *const level = ui_page_settings_current_level();
    if (level == 0)
    {
        drv_display_draw_text(0U, 0U, "SETTINGS");
        drv_display_draw_text(0U, 12U, "EMPTY");
        return;
    }

    drv_display_draw_text(0U, 0U, ui_page_settings_view_title(level->view));
    drv_display_draw_line(0, 9, 127, 9);

    const uint8_t count = ui_page_settings_view_item_count(level->view);
    const uint8_t selected = (count > 0U) ? (uint8_t)(level->selected_index % count) : 0U;
    const uint8_t visible_lines = 4U;
    uint8_t start = 0U;
    if (selected >= visible_lines)
    {
        start = (uint8_t)(selected - (visible_lines - 1U));
    }

    for (uint8_t line = 0U; line < visible_lines; ++line)
    {
        const uint8_t item = (uint8_t)(start + line);
        if (item >= count)
        {
            break;
        }

        char buf[24];
        const char *label = ui_page_settings_item_label(level->view, item, buf, sizeof(buf));
        const uint8_t y = (uint8_t)(12U + (line * 12U));

        if (item == selected)
        {
            drv_display_fill_rect(0, y - 1U, 128, 10);
            drv_display_draw_text_inverted(2U, y, label);
        }
        else
        {
            drv_display_draw_text(2U, y, label);
        }
    }

    if (g_ui_settings.status_line[0] != '\0')
    {
        drv_display_draw_text(0U, 54U, g_ui_settings.status_line);
    }
    else if (level->view == UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT)
    {
        char slot_line[24];
        (void)snprintf(slot_line, sizeof(slot_line), "SLOT %02u", (unsigned)g_ui_settings.selected_slot);
        drv_display_draw_text(0U, 54U, slot_line);
    }
}

const ui_page_t g_ui_page_settings = {
    .enter = ui_page_settings_enter,
    .leave = ui_page_settings_leave,
    .handle_event = ui_page_settings_handle_event_internal,
    .tick = ui_page_settings_tick,
    .render = ui_page_settings_render,
};

void ui_page_settings_open(uint8_t return_page_id)
{
    g_ui_settings.return_page_id = return_page_id;
    ui_page_set(UI_PAGE_SETTINGS);
}

uint8_t ui_page_settings_is_open(void)
{
    return (ui_page_get_id() == UI_PAGE_SETTINGS) ? 1U : 0U;
}

void ui_page_settings_handle_encoder(int16_t delta)
{
    ui_settings_menu_level_t *const level = ui_page_settings_current_level();
    if ((level == 0) || (delta == 0))
    {
        return;
    }

    const uint8_t count = ui_page_settings_view_item_count(level->view);
    if (count == 0U)
    {
        level->selected_index = 0U;
        return;
    }

    int32_t index = level->selected_index;
    index += delta;

    while (index < 0)
    {
        index += count;
    }

    while (index >= count)
    {
        index -= count;
    }

    level->selected_index = (uint8_t)index;
}

uint8_t ui_page_settings_handle_event(const ui_event_t *ev)
{
    if (ui_page_settings_is_open() == 0U)
    {
        return 0U;
    }

    ui_page_settings_handle_event_internal(ev);
    return 1U;
}
