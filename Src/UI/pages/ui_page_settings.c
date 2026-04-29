#include "pages/ui_page_settings.h"

#include <stdio.h>
#include <string.h>

#include "stm32h7xx_hal.h"
#include "buttons.h"
#include "buttons_ids.h"
#include "Storage/sd_preview.h"
#include "Storage/project_v1.h"
#include "Sampler/sample_pool.h"
#include "Storage/wav_loader.h"
#include "drv_display.h"
#include "ui_page_manager.h"

typedef enum
{
    UI_SETTINGS_VIEW_ROOT = 0,
    UI_SETTINGS_VIEW_PROJECT,
    UI_SETTINGS_VIEW_SAMPLER,
    UI_SETTINGS_VIEW_SAMPLER_SLOT,
    UI_SETTINGS_VIEW_SAMPLER_CATALOG,
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

typedef enum
{
    UI_SETTINGS_SAMPLER_ACTION_LOAD_OR_REPLACE = 0,
    UI_SETTINGS_SAMPLER_ACTION_PREVIEW_OR_STOP,
    UI_SETTINGS_SAMPLER_ACTION_CLEAR,
    UI_SETTINGS_SAMPLER_ACTION_COUNT
} ui_settings_sampler_action_t;

typedef enum
{
    UI_SETTINGS_SAMPLER_CATALOG_MODE_LOAD = 0,
    UI_SETTINGS_SAMPLER_CATALOG_MODE_PREVIEW
} ui_settings_sampler_catalog_mode_t;

typedef enum
{
    UI_SETTINGS_PREVIEW_STOP_ORIGIN_NONE = 0,
    UI_SETTINGS_PREVIEW_STOP_ORIGIN_USER,
    UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT
} ui_settings_preview_stop_origin_t;

typedef struct
{
    ui_settings_view_t view;
    uint8_t selected_index;
} ui_settings_menu_level_t;

#define UI_SETTINGS_MAX_LEVELS 6U
#define UI_SETTINGS_STATUS_DURATION_MS 1000U
#define UI_SETTINGS_ENCODER_DIVIDER 4
#define UI_SETTINGS_ENCODER_COUNT 4U

typedef struct
{
    ui_settings_menu_level_t levels[UI_SETTINGS_MAX_LEVELS];
    uint8_t depth;
    uint8_t selected_slot;
    ui_settings_sampler_catalog_mode_t sampler_catalog_mode;
    uint8_t sampler_slots[SAMPLE_POOL_SIZE];
    uint8_t sampler_slot_count;
    uint8_t project_slots[PROJECT_V1_SLOT_COUNT];
    uint8_t project_slot_count;
    uint8_t return_page_id;
    uint8_t preview_was_active;
    uint8_t preview_stop_origin;
    char status_line[20];
    uint32_t status_until_ms;
    int16_t encoder_accum[UI_SETTINGS_ENCODER_COUNT];
} ui_settings_state_t;

static ui_settings_state_t g_ui_settings;

static const char *ui_page_settings_sampler_load_error_label(void)
{
    switch (sample_pool_get_last_load_error())
    {
        case SAMPLE_POOL_LOAD_SD_MOUNT_FAIL:
            return "SD UNAVAILABLE";
        case SAMPLE_POOL_LOAD_SD_GATE_REFUSED:
            return "SD GATE REFUSED";
        case SAMPLE_POOL_LOAD_SD_FILE_NOT_FOUND:
            return "FILE NOT FOUND";
        case SAMPLE_POOL_LOAD_SD_OPEN_FAIL:
            return "OPEN FAIL";
        case SAMPLE_POOL_LOAD_WAV_PARSE_FAIL:
            return "WAV INVALID";
        case SAMPLE_POOL_LOAD_WAV_UNSUPPORTED_FORMAT:
            return "WAV UNSUPPORTED";
        case SAMPLE_POOL_LOAD_MEMORY_LIMIT:
            return "MEMORY FULL";
        case SAMPLE_POOL_LOAD_SD_READ_FAIL:
            return "SD READ FAIL";
        case SAMPLE_POOL_LOAD_SD_SEEK_FAIL:
            return "SD SEEK FAIL";
        case SAMPLE_POOL_LOAD_SD_SHORT_READ:
            return "SD SHORT READ";
        case SAMPLE_POOL_LOAD_SD_READ_INT_ERR:
            return "SD INT ERR";
        case SAMPLE_POOL_LOAD_SD_NOT_READY:
            return "SD NOT READY";
        case SAMPLE_POOL_LOAD_SD_INVALID_OBJECT:
            return "SD OBJ ERR";
        case SAMPLE_POOL_LOAD_SD_TIMEOUT:
            return "SD TIMEOUT";
        case SAMPLE_POOL_LOAD_SD_NOT_ENOUGH_CORE:
            return "SD CORE LOW";
        default:
            return "LOAD SD FAIL";
    }
}

static const char *ui_page_settings_preview_error_label(sd_preview_error_t error)
{
    switch (error)
    {
        case SD_PREVIEW_ERROR_NONE:
            return "PREVIEW STOP";
        case SD_PREVIEW_ERROR_INVALID_PATH:
            return "BAD PATH";
        case SD_PREVIEW_ERROR_BUSY:
        case SD_PREVIEW_ERROR_GATE_REFUSED:
            return "SD BUSY";
        case SD_PREVIEW_ERROR_MOUNT_FAIL:
            return "SD UNAVAILABLE";
        case SD_PREVIEW_ERROR_OPEN_FAIL:
            return "OPEN FAIL";
        case SD_PREVIEW_ERROR_PARSE_FAIL:
            return "WAV INVALID";
        case SD_PREVIEW_ERROR_UNSUPPORTED_FORMAT:
            return "WAV UNSUPP";
        case SD_PREVIEW_ERROR_READ_FAIL:
            return "SD READ FAIL";
        default:
            return "PREVIEW FAIL";
    }
}

static void ui_page_settings_refresh_project_slots(void)
{
    project_v1_refresh_slots();
    g_ui_settings.project_slot_count = project_v1_list_slots(g_ui_settings.project_slots, PROJECT_V1_SLOT_COUNT);
}

static void ui_page_settings_refresh_sampler_slots(void)
{
    for (uint8_t i = 0U; i < SAMPLE_POOL_SIZE; ++i)
    {
        g_ui_settings.sampler_slots[i] = i;
    }
    g_ui_settings.sampler_slot_count = SAMPLE_POOL_SIZE;
}

static const char *ui_page_settings_view_title(ui_settings_view_t view)
{
    switch (view)
    {
        case UI_SETTINGS_VIEW_ROOT:
            return "SETTINGS";
        case UI_SETTINGS_VIEW_PROJECT:
            return "PROJECT";
        case UI_SETTINGS_VIEW_SAMPLER:
            return "SAMPLER";
        case UI_SETTINGS_VIEW_SAMPLER_SLOT:
            return "SAMPLER > SLOT";
        case UI_SETTINGS_VIEW_SAMPLER_CATALOG:
            return "SAMPLER > SD";
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
            return 2U;
        case UI_SETTINGS_VIEW_PROJECT:
            return 3U;
        case UI_SETTINGS_VIEW_SAMPLER:
            return g_ui_settings.sampler_slot_count;
        case UI_SETTINGS_VIEW_SAMPLER_SLOT:
            return (uint8_t)UI_SETTINGS_SAMPLER_ACTION_COUNT;
        case UI_SETTINGS_VIEW_SAMPLER_CATALOG:
            return wav_loader_catalog_count();
        case UI_SETTINGS_VIEW_PROJECT_LOAD:
            return (uint8_t)(g_ui_settings.project_slot_count + 1U);
        case UI_SETTINGS_VIEW_PROJECT_MANAGE:
            return g_ui_settings.project_slot_count;
        case UI_SETTINGS_VIEW_PROJECT_SAVE_AS:
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
            return (index == 0U) ? "PROJECT" : "SAMPLER";
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
            if (index == 0U)
            {
                return "BLANK PROJECT";
            }
            if ((index - 1U) >= g_ui_settings.project_slot_count)
            {
                return "-";
            }
            (void)snprintf(out, out_size, "PROJECT %02u", (unsigned)g_ui_settings.project_slots[index - 1U]);
            return out;
        case UI_SETTINGS_VIEW_PROJECT_MANAGE:
            if (index >= g_ui_settings.project_slot_count)
            {
                return "-";
            }
            (void)snprintf(out, out_size, "PROJECT %02u", (unsigned)g_ui_settings.project_slots[index]);
            return out;
        case UI_SETTINGS_VIEW_PROJECT_SAVE_AS:
            if (project_v1_slot_has_data(index) != 0U)
            {
                (void)snprintf(out, out_size, "PROJECT %02u *", (unsigned)index);
            }
            else
            {
                (void)snprintf(out, out_size, "PROJECT %02u", (unsigned)index);
            }
            return out;
        case UI_SETTINGS_VIEW_SAMPLER:
            if (index >= g_ui_settings.sampler_slot_count)
            {
                return "-";
            }
            {
                const sample_pool_slot_state_t state = sample_pool_get_state(g_ui_settings.sampler_slots[index]);
                const char *state_label = "EMPTY";
                if (state == SAMPLE_POOL_SLOT_LOADED)
                {
                    state_label = "LOADED";
                }
                else if (state == SAMPLE_POOL_SLOT_PREPARING)
                {
                    state_label = "PREP";
                }
                else if (state == SAMPLE_POOL_SLOT_ERROR)
                {
                    state_label = "ERROR";
                }
                else if (state == SAMPLE_POOL_SLOT_MISSING)
                {
                    state_label = "MISSING";
                }
                (void)snprintf(out, out_size, "SLOT %02u [%s]", (unsigned)g_ui_settings.sampler_slots[index], state_label);
                return out;
            }
        case UI_SETTINGS_VIEW_SAMPLER_SLOT:
            if (index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_LOAD_OR_REPLACE)
            {
                return (sample_pool_get_state(g_ui_settings.selected_slot) == SAMPLE_POOL_SLOT_EMPTY) ? "LOAD FROM SD" : "REPLACE FROM SD";
            }
            if (index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_PREVIEW_OR_STOP)
            {
                return (sd_preview_is_active() != 0U) ? "STOP" : "PREVIEW";
            }
            if (index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_CLEAR)
            {
                return "CLEAR";
            }
            return "-";
        case UI_SETTINGS_VIEW_SAMPLER_CATALOG:
            {
                const wav_loader_catalog_entry_t *entry = wav_loader_catalog_get(index);
                if (entry == 0)
                {
                    return "-";
                }
                return entry->name;
            }
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
        g_ui_settings.status_until_ms = 0U;
        return;
    }

    (void)snprintf(g_ui_settings.status_line,
                   sizeof(g_ui_settings.status_line),
                   "%s",
                   status);
    g_ui_settings.status_until_ms = HAL_GetTick() + UI_SETTINGS_STATUS_DURATION_MS;
}

static void ui_page_settings_preview_stop(ui_settings_preview_stop_origin_t origin)
{
    if (sd_preview_is_active() == 0U)
    {
        return;
    }

    sd_preview_stop();
    g_ui_settings.preview_stop_origin = origin;
}

static void ui_page_settings_preview_apply_termination_status(void)
{
    const uint8_t active = sd_preview_is_active();

    if ((g_ui_settings.preview_was_active != 0U) && (active == 0U))
    {
        if (g_ui_settings.preview_stop_origin == UI_SETTINGS_PREVIEW_STOP_ORIGIN_USER)
        {
            ui_page_settings_status("PREVIEW STOP");
        }
        else if (g_ui_settings.preview_stop_origin == UI_SETTINGS_PREVIEW_STOP_ORIGIN_NONE)
        {
            const sd_preview_error_t error = sd_preview_get_last_error();
            ui_page_settings_status((error == SD_PREVIEW_ERROR_NONE)
                                        ? "PREVIEW END"
                                        : ui_page_settings_preview_error_label(error));
        }
    }

    g_ui_settings.preview_stop_origin = UI_SETTINGS_PREVIEW_STOP_ORIGIN_NONE;
    g_ui_settings.preview_was_active = active;
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
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);

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
            if (level->selected_index == 0U)
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT);
            }
            else
            {
                ui_page_settings_refresh_sampler_slots();
                ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLER);
            }
            break;

        case UI_SETTINGS_VIEW_PROJECT:
            if (level->selected_index == 0U)
            {
                ui_page_settings_refresh_project_slots();
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT_LOAD);
            }
            else if (level->selected_index == 1U)
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT_SAVE_AS);
            }
            else
            {
                ui_page_settings_refresh_project_slots();
                if (g_ui_settings.project_slot_count == 0U)
                {
                    ui_page_settings_status("NO PROJECT");
                    break;
                }
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT_MANAGE);
            }
            break;

        case UI_SETTINGS_VIEW_PROJECT_LOAD:
            if (level->selected_index == 0U)
            {
                if (project_v1_load_blank() != 0U)
                {
                    ui_page_settings_status("BLANK OK");
                }
                else
                {
                    ui_page_settings_status("BLANK FAIL");
                }
            }
            else if (((level->selected_index - 1U) < g_ui_settings.project_slot_count)
                     && (project_v1_load_slot(g_ui_settings.project_slots[level->selected_index - 1U]) != 0U))
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
            {                ui_page_settings_refresh_project_slots();
                ui_page_settings_status("SAVE OK");
            }
            else
            {                ui_page_settings_status("SAVE FAIL");
            }
            break;

        case UI_SETTINGS_VIEW_PROJECT_MANAGE:
            if (level->selected_index < g_ui_settings.project_slot_count)
            {
                g_ui_settings.selected_slot = g_ui_settings.project_slots[level->selected_index];
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT);
            }
            break;

        case UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT:
            if (level->selected_index == (uint8_t)UI_SETTINGS_MANAGE_ACTION_LOAD_FROM)
            {
                if (project_v1_load_slot(g_ui_settings.selected_slot) != 0U)
                {                    ui_page_settings_status("LOAD FROM OK");
                }
                else
                {                    ui_page_settings_status("LOAD FROM FAIL");
                }
            }
            else if (level->selected_index == (uint8_t)UI_SETTINGS_MANAGE_ACTION_SAVE_TO)
            {
                if (project_v1_save_slot(g_ui_settings.selected_slot) != 0U)
                {                    ui_page_settings_status("SAVE TO OK");
                }
                else
                {                    ui_page_settings_status("SAVE TO FAIL");
                }
            }
            else if (project_v1_delete_slot(g_ui_settings.selected_slot) != 0U)
            {                ui_page_settings_refresh_project_slots();
                ui_page_settings_back();
                ui_page_settings_status("DELETE OK");
            }
            else
            {                ui_page_settings_status("DELETE FAIL");
            }
            break;

        case UI_SETTINGS_VIEW_SAMPLER:
            if (level->selected_index < g_ui_settings.sampler_slot_count)
            {
                g_ui_settings.selected_slot = g_ui_settings.sampler_slots[level->selected_index];
                ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLER_SLOT);
            }
            break;

        case UI_SETTINGS_VIEW_SAMPLER_SLOT:
            if (level->selected_index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_LOAD_OR_REPLACE)
            {
                ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
                if (wav_loader_catalog_count() != 0U)
                {
                    g_ui_settings.sampler_catalog_mode = UI_SETTINGS_SAMPLER_CATALOG_MODE_LOAD;
                    ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLER_CATALOG);
                }
                else
                {
                    ui_page_settings_status("NO WAV");
                }
            }
            else if (level->selected_index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_PREVIEW_OR_STOP)
            {
                if (sd_preview_is_active() != 0U)
                {
                    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_USER);
                }
                else if (wav_loader_catalog_count() != 0U)
                {
                    g_ui_settings.sampler_catalog_mode = UI_SETTINGS_SAMPLER_CATALOG_MODE_PREVIEW;
                    ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLER_CATALOG);
                }
                else
                {
                    ui_page_settings_status("NO WAV");
                }
            }
            else if (level->selected_index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_CLEAR)
            {
                ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
                sample_pool_clear(g_ui_settings.selected_slot);
                ui_page_settings_refresh_sampler_slots();
                ui_page_settings_status("CLEAR OK");
            }
            break;

        case UI_SETTINGS_VIEW_SAMPLER_CATALOG:
            if (level->selected_index < wav_loader_catalog_count())
            {
                const wav_loader_catalog_entry_t *entry = wav_loader_catalog_get(level->selected_index);
                if ((entry != 0) && (entry->state == WAV_LOADER_CATALOG_READY))
                {
                    if (g_ui_settings.sampler_catalog_mode == UI_SETTINGS_SAMPLER_CATALOG_MODE_PREVIEW)
                    {
                        if ((sd_preview_is_active() != 0U)
                            && (strcmp(sd_preview_get_path(), entry->path) == 0))
                        {
                            ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_USER);
                            break;
                        }

                        if (sd_preview_begin(entry->path) != 0U)
                        {
                            g_ui_settings.preview_stop_origin = UI_SETTINGS_PREVIEW_STOP_ORIGIN_NONE;
                            g_ui_settings.preview_was_active = sd_preview_is_active();
                            ui_page_settings_status("PREVIEW ON");
                        }
                        else
                        {
                            ui_page_settings_status(ui_page_settings_preview_error_label(sd_preview_get_last_error()));
                        }
                    }
                    else if (sample_pool_load(g_ui_settings.selected_slot, entry->path) != 0U)
                    {
                        ui_page_settings_refresh_sampler_slots();
                        ui_page_settings_status("LOAD SD OK");
                    }
                    else
                    {                        ui_page_settings_status(ui_page_settings_sampler_load_error_label());
                    }
                }
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
    g_ui_settings.sampler_catalog_mode = UI_SETTINGS_SAMPLER_CATALOG_MODE_LOAD;
    g_ui_settings.sampler_slot_count = 0U;
    g_ui_settings.project_slot_count = 0U;
    g_ui_settings.preview_was_active = 0U;
    g_ui_settings.preview_stop_origin = UI_SETTINGS_PREVIEW_STOP_ORIGIN_NONE;
    for (uint8_t i = 0U; i < UI_SETTINGS_ENCODER_COUNT; ++i)
    {
        g_ui_settings.encoder_accum[i] = 0;
    }
    ui_page_settings_refresh_project_slots();
    ui_page_settings_refresh_sampler_slots();
    ui_page_settings_status(0);
    ui_page_settings_push(UI_SETTINGS_VIEW_ROOT);
}

static void ui_page_settings_leave(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
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

static void ui_page_settings_handle_event_page(const ui_event_t *ev)
{
    (void)ui_page_settings_handle_event(ev);
}

static void ui_page_settings_tick(void)
{
    ui_page_settings_preview_apply_termination_status();

    if ((g_ui_settings.status_line[0] != '\0')
        && ((int32_t)(g_ui_settings.status_until_ms - HAL_GetTick()) <= 0))
    {
        ui_page_settings_status(0);
    }
}

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
    else if (level->view == UI_SETTINGS_VIEW_SAMPLER_SLOT)
    {
        char slot_line[24];
        const sample_pool_slot_state_t state = sample_pool_get_state(g_ui_settings.selected_slot);
        const char *state_label = (state == SAMPLE_POOL_SLOT_LOADED) ? "LOADED"
                                 : (state == SAMPLE_POOL_SLOT_PREPARING) ? "PREP"
                                 : (state == SAMPLE_POOL_SLOT_ERROR) ? "ERROR"
                                 : (state == SAMPLE_POOL_SLOT_MISSING) ? "MISSING"
                                 : "EMPTY";
        (void)snprintf(slot_line, sizeof(slot_line), "SLOT %02u %s",
                       (unsigned)g_ui_settings.selected_slot,
                       state_label);
        drv_display_draw_text(0U, 54U, slot_line);
        if (g_ui_settings.status_line[0] == '\0')
        {
            drv_display_draw_text(0U, 42U, "COPY = ACTION");
        }
    }
    else if (level->view == UI_SETTINGS_VIEW_SAMPLER_CATALOG)
    {
        char slot_line[24];
        (void)snprintf(slot_line, sizeof(slot_line), "CATALOG %u", (unsigned)wav_loader_catalog_count());
        drv_display_draw_text(0U, 54U, slot_line);
        if (g_ui_settings.status_line[0] == '\0')
        {
            drv_display_draw_text(0U,
                                  42U,
                                  (g_ui_settings.sampler_catalog_mode == UI_SETTINGS_SAMPLER_CATALOG_MODE_PREVIEW)
                                      ? "COPY = PREVIEW/STOP"
                                      : "COPY = LOAD");
        }
    }
}

const ui_page_t g_ui_page_settings = {
    .enter = ui_page_settings_enter,
    .leave = ui_page_settings_leave,
    .handle_event = ui_page_settings_handle_event_page,
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

void ui_page_settings_handle_encoder(uint8_t encoder, int16_t delta)
{
    ui_settings_menu_level_t *const level = ui_page_settings_current_level();
    if ((level == 0) || (delta == 0) || (encoder >= UI_SETTINGS_ENCODER_COUNT))
    {
        return;
    }

    const uint8_t count = ui_page_settings_view_item_count(level->view);
    if (count == 0U)
    {
        level->selected_index = 0U;
        return;
    }

    g_ui_settings.encoder_accum[encoder] = (int16_t)(g_ui_settings.encoder_accum[encoder] + delta);
    const int16_t step = (int16_t)(g_ui_settings.encoder_accum[encoder] / UI_SETTINGS_ENCODER_DIVIDER);
    g_ui_settings.encoder_accum[encoder] = (int16_t)(g_ui_settings.encoder_accum[encoder] - (step * UI_SETTINGS_ENCODER_DIVIDER));
    if (step == 0)
    {
        return;
    }

    if ((level->view == UI_SETTINGS_VIEW_SAMPLER_CATALOG)
        && (g_ui_settings.sampler_catalog_mode == UI_SETTINGS_SAMPLER_CATALOG_MODE_PREVIEW))
    {
        ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    }

    int32_t index = level->selected_index;
    index += step;
    if (index < 0)
    {
        index = 0;
    }
    else if (index >= count)
    {
        index = (int32_t)count - 1;
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

