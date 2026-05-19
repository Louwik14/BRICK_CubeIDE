#include "pages/ui_page_settings.h"

#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "stm32h7xx_hal.h"
#include "buttons.h"
#include "buttons_ids.h"
#include "ff.h"
#include "Storage/sd_access_gate.h"
#include "Storage/sd_preview.h"
#include "Storage/memory_layout.h"
#include "Storage/wav_convert.h"
#include "Storage/looper_storage.h"
#include "Storage/multi_record_writer.h"
#include "Storage/project_v1.h"
#include "Core/brick6_sampler_runtime.h"
#include "Sampler/sample_pool.h"
#include "Sampler/multi_sample_import.h"
#include "Sampler/multi_sample_index.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/multi_sample_pool.h"
#include "Storage/wav_loader.h"
#include "Seq/seq_runtime.h"
#include "drv_display.h"
#include "font.h"
#include "ui_core.h"
#include "ui_page_manager.h"

typedef enum
{
    UI_SETTINGS_VIEW_ROOT = 0,
    UI_SETTINGS_VIEW_PROJECT,
    UI_SETTINGS_VIEW_SAMPLER,
    UI_SETTINGS_VIEW_MULTI_SAMPLE,
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
    UI_SETTINGS_SAMPLE_FOCUS_LIBRARY = 0,
    UI_SETTINGS_SAMPLE_FOCUS_SLOTS
} ui_settings_sample_focus_t;

typedef enum
{
    UI_SETTINGS_SAMPLE_ENTRY_FILE = 0,
    UI_SETTINGS_SAMPLE_ENTRY_DIR,
    UI_SETTINGS_SAMPLE_ENTRY_PARENT
} ui_settings_sample_entry_type_t;

typedef enum
{
    UI_SETTINGS_SAMPLE_CONFIRM_NONE = 0,
    UI_SETTINGS_SAMPLE_CONFIRM_REPLACE,
    UI_SETTINGS_SAMPLE_CONFIRM_CLEAR,
    UI_SETTINGS_SAMPLE_CONFIRM_CONVERT,
    UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARE,
    UI_SETTINGS_SAMPLE_CONFIRM_MULTI_REPLACE,
    UI_SETTINGS_SAMPLE_CONFIRM_MULTI_UNLOAD
} ui_settings_sample_confirm_t;

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
#define UI_SETTINGS_SAMPLE_BROWSER_MAX WAV_LOADER_CATALOG_VIEW_MAX
#define UI_SETTINGS_MULTI_BROWSER_MAX 240U
#define UI_SETTINGS_SAMPLE_ROOT "0:/Samples"
#define UI_SETTINGS_MULTI_ROOT "0:/Multi"

typedef struct
{
    char path[WAV_LOADER_CATALOG_PATH_MAX];
    char label[24];
    uint16_t catalog_index;
    ui_settings_sample_entry_type_t type;
    uint8_t _pad[1];
} ui_settings_sample_entry_t;

typedef struct
{
    char path[MULTI_SAMPLE_POOL_PATH_MAX];
    char index_path[MULTI_SAMPLE_POOL_PATH_MAX];
    char label[MULTI_SAMPLE_POOL_NAME_MAX];
    uint16_t sample_count;
    uint16_t zone_count;
    uint8_t prepared;
    uint8_t _pad;
} ui_settings_multi_entry_t;

typedef struct
{
    ui_settings_sample_entry_t sample_entries[UI_SETTINGS_SAMPLE_BROWSER_MAX];
    ui_settings_multi_entry_t multi_entries[UI_SETTINGS_MULTI_BROWSER_MAX];
    char sample_dir[WAV_LOADER_CATALOG_PATH_MAX];
    char confirm_path[MULTI_SAMPLE_POOL_PATH_MAX];
    char convert_path[SAMPLE_POOL_PATH_MAX];
    char status_line[24];
    ui_settings_menu_level_t levels[UI_SETTINGS_MAX_LEVELS];
    uint8_t depth;
    uint8_t selected_slot;
    ui_settings_sampler_catalog_mode_t sampler_catalog_mode;
    uint16_t sample_entry_count;
    uint16_t sample_child_count;
    uint16_t sample_page_start;
    uint16_t multi_entry_count;
    uint16_t sample_selected;
    uint8_t sample_slot_selected;
    uint16_t sample_left_scroll;
    uint8_t sample_right_scroll;
    uint8_t sample_focus;
    uint8_t sample_confirm;
    uint8_t confirm_slot;
    uint8_t sample_preview_volume;
    uint16_t sample_parent_id;
    uint8_t sampler_slots[SAMPLE_POOL_SIZE];
    uint8_t sampler_slot_count;
    uint8_t project_slots[PROJECT_V1_SLOT_COUNT];
    uint8_t project_slot_count;
    uint8_t return_page_id;
    uint8_t preview_was_active;
    uint8_t preview_stop_origin;
    uint8_t convert_slot_valid;
    uint8_t convert_slot;
    uint32_t status_until_ms;
    int16_t encoder_accum[UI_SETTINGS_ENCODER_COUNT];
} ui_settings_state_t;

#define UI_SETTINGS_STATIC_ASSERT(name, cond) typedef char ui_settings_static_assert_##name[(cond) ? 1 : -1]
#define UI_SETTINGS_OFFSET_ALIGNED(type, field) ((offsetof(type, field) % 4U) == 0U)

UI_SETTINGS_STATIC_ASSERT(sample_entry_stride_aligned, (sizeof(ui_settings_sample_entry_t) % 4U) == 0U);
UI_SETTINGS_STATIC_ASSERT(sample_entry_path_aligned, UI_SETTINGS_OFFSET_ALIGNED(ui_settings_sample_entry_t, path));
UI_SETTINGS_STATIC_ASSERT(sample_entry_label_aligned, UI_SETTINGS_OFFSET_ALIGNED(ui_settings_sample_entry_t, label));
UI_SETTINGS_STATIC_ASSERT(sample_entries_aligned, UI_SETTINGS_OFFSET_ALIGNED(ui_settings_state_t, sample_entries));
UI_SETTINGS_STATIC_ASSERT(multi_entries_aligned, UI_SETTINGS_OFFSET_ALIGNED(ui_settings_state_t, multi_entries));
UI_SETTINGS_STATIC_ASSERT(sample_dir_aligned, UI_SETTINGS_OFFSET_ALIGNED(ui_settings_state_t, sample_dir));
UI_SETTINGS_STATIC_ASSERT(confirm_path_aligned, UI_SETTINGS_OFFSET_ALIGNED(ui_settings_state_t, confirm_path));
UI_SETTINGS_STATIC_ASSERT(convert_path_aligned, UI_SETTINGS_OFFSET_ALIGNED(ui_settings_state_t, convert_path));
UI_SETTINGS_STATIC_ASSERT(status_line_aligned, UI_SETTINGS_OFFSET_ALIGNED(ui_settings_state_t, status_line));

UI_STATE_SDRAM static ui_settings_state_t g_ui_settings;

static void ui_page_settings_status(const char *status);
static void ui_page_settings_sd_busy_status(void);
static void ui_page_settings_preview_stop(ui_settings_preview_stop_origin_t origin);
static const char *ui_page_settings_preview_error_label(sd_preview_error_t error);
static void ui_page_settings_back(void);
static void ui_page_settings_sample_load_to_slot(uint8_t slot, const char *path);
static void ui_page_settings_sample_copy_left(uint8_t shift_down);
static const ui_settings_sample_entry_t *ui_page_settings_sample_selected_entry(void);
static void ui_page_settings_apply_action(void);
static void ui_page_settings_copy_bounded(char *out, uint32_t out_size, const char *src);
static const ui_settings_multi_entry_t *ui_page_settings_multi_find_entry_by_path(const char *path);
static uint8_t ui_page_settings_multi_prepare_entry(const ui_settings_multi_entry_t *entry);
static void ui_page_settings_multi_load_entry_to_slot(uint8_t slot, const ui_settings_multi_entry_t *entry);

static const char *ui_page_settings_sampler_load_error_label(void)
{
    switch (sample_pool_get_last_load_error())
    {
        case SAMPLE_POOL_LOAD_INVALID_PATH:
            return "BAD PATH";
        case SAMPLE_POOL_LOAD_PATH_TOO_LONG:
            return "PATH LONG";
        case SAMPLE_POOL_LOAD_SD_MOUNT_FAIL:
            return "SD UNAVAILABLE";
        case SAMPLE_POOL_LOAD_SD_GATE_REFUSED:
            return "SD BUSY";
        case SAMPLE_POOL_LOAD_SD_FILE_NOT_FOUND:
            return "NO FILE";
        case SAMPLE_POOL_LOAD_SD_OPEN_FAIL:
            return "READ ERR";
        case SAMPLE_POOL_LOAD_WAV_PARSE_FAIL:
            return "BAD WAV";
        case SAMPLE_POOL_LOAD_WAV_UNSUPPORTED_FORMAT:
            return "UNSUPPORTED";
        case SAMPLE_POOL_LOAD_WAV_48K_REQUIRED:
            return "48K ONLY";
        case SAMPLE_POOL_LOAD_MEMORY_LIMIT:
            return "NO CACHE";
        case SAMPLE_POOL_LOAD_SD_READ_FAIL:
            return "READ ERR";
        case SAMPLE_POOL_LOAD_SD_SEEK_FAIL:
            return "READ ERR";
        case SAMPLE_POOL_LOAD_SD_SHORT_READ:
            return "READ ERR";
        case SAMPLE_POOL_LOAD_SD_READ_INT_ERR:
            return "READ ERR";
        case SAMPLE_POOL_LOAD_SD_NOT_READY:
            return "SD BUSY";
        case SAMPLE_POOL_LOAD_SD_INVALID_OBJECT:
            return "READ ERR";
        case SAMPLE_POOL_LOAD_SD_TIMEOUT:
            return "SD BUSY";
        case SAMPLE_POOL_LOAD_SD_NOT_ENOUGH_CORE:
            return "READ ERR";
        default:
            return "LOAD FAIL";
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
        case SD_PREVIEW_ERROR_RECORD_ACTIVE:
            return "REC ACTIVE";
        default:
            return "PREVIEW FAIL";
    }
}

static const char *ui_page_settings_convert_error_label(wav_convert_error_t error)
{
    switch (error)
    {
        case WAV_CONVERT_ERROR_NO_SPACE:
            return "SD FULL";
        case WAV_CONVERT_ERROR_BUSY:
            return "SD BUSY";
        case WAV_CONVERT_ERROR_UNSUPPORTED:
            return "WAV UNSUPP";
        case WAV_CONVERT_ERROR_MOUNT_FAIL:
            return "SD UNAVAILABLE";
        case WAV_CONVERT_ERROR_OPEN_FAIL:
            return "OPEN FAIL";
        case WAV_CONVERT_ERROR_READ_FAIL:
            return "SD READ FAIL";
        case WAV_CONVERT_ERROR_WRITE_FAIL:
            return "SD WRITE FAIL";
        case WAV_CONVERT_ERROR_SYNC_FAIL:
            return "SD SYNC FAIL";
        case WAV_CONVERT_ERROR_VERIFY_FAIL:
            return "VERIFY FAIL";
        case WAV_CONVERT_ERROR_REPLACE_FAIL:
            return "REPLACE FAIL";
        case WAV_CONVERT_ERROR_CLOSE_FAIL:
            return "CLOSE FAIL";
        default:
            return "CONVERT FAIL";
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

static void ui_page_settings_sd_busy_status(void)
{
    char status[16];
    (void)snprintf(status, sizeof(status), "SD %s", sd_access_gate_busy_label());
    ui_page_settings_status(status);
}

static uint8_t ui_page_settings_wav_ext_is_wav(const char *name)
{
    const size_t len = (name != 0) ? strlen(name) : 0U;
    if (len < 4U)
    {
        return 0U;
    }

    return ((name[len - 4U] == '.')
            && ((name[len - 3U] == 'w') || (name[len - 3U] == 'W'))
            && ((name[len - 2U] == 'a') || (name[len - 2U] == 'A'))
            && ((name[len - 1U] == 'v') || (name[len - 1U] == 'V')))
        ? 1U
        : 0U;
}

static void ui_page_settings_make_sample_label(char *out, uint32_t out_size, const char *name, uint8_t is_dir)
{
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';
    if (name == 0)
    {
        return;
    }

    uint32_t len = (uint32_t)strlen(name);
    if ((is_dir == 0U) && (len > 4U) && (ui_page_settings_wav_ext_is_wav(name) != 0U))
    {
        len -= 4U;
    }

    if (len >= out_size)
    {
        len = out_size - 1U;
    }

    (void)memcpy(out, name, len);
    out[len] = '\0';
}

static uint8_t ui_page_settings_join_path(char *out,
                                          uint32_t out_size,
                                          const char *dir,
                                          const char *name)
{
    if ((out == 0) || (out_size == 0U) || (dir == 0) || (name == 0))
    {
        return 0U;
    }

    const size_t dir_len = strlen(dir);
    const char *const sep = ((dir_len != 0U) && (dir[dir_len - 1U] == '/')) ? "" : "/";
    const int written = snprintf(out, out_size, "%s%s%s", dir, sep, name);
    return ((written >= 0) && ((uint32_t)written < out_size)) ? 1U : 0U;
}

static void ui_page_settings_copy_bytes(char *out, const char *src, uint32_t len)
{
    if ((out == 0) || (src == 0))
    {
        return;
    }

    volatile char *const dst = (volatile char *)out;
    for (uint32_t i = 0U; i < len; ++i)
    {
        dst[i] = src[i];
    }
}

static uint8_t ui_page_settings_multi_index_name(const char *folder_path,
                                                 char *out,
                                                 uint32_t out_size)
{
    char folder[MULTI_SAMPLE_POOL_PATH_MAX];
    char name_copy[MULTI_SAMPLE_POOL_NAME_MAX];

    if ((folder_path == 0) || (out == 0) || (out_size == 0U))
    {
        return 0U;
    }

    const char *name = strrchr(folder_path, '/');
    name = (name != 0) ? (name + 1) : folder_path;
    if ((name == 0) || (name[0] == '\0'))
    {
        return 0U;
    }

    if ((strlen(folder_path) >= sizeof(folder)) || (strlen(name) >= sizeof(name_copy)))
    {
        return 0U;
    }

    const uint32_t folder_len = (uint32_t)strlen(folder_path);
    const uint32_t name_len = (uint32_t)strlen(name);
    ui_page_settings_copy_bytes(folder, folder_path, folder_len);
    folder[folder_len] = '\0';
    ui_page_settings_copy_bytes(name_copy, name, name_len);
    name_copy[name_len] = '\0';

    static const char k_suffix[] = ".brickmulti";
    const uint32_t suffix_len = (uint32_t)(sizeof(k_suffix) - 1U);
    const uint32_t total = folder_len + 1U + name_len + suffix_len;
    if (total >= out_size)
    {
        return 0U;
    }

    uint32_t pos = 0U;
    ui_page_settings_copy_bytes(&out[pos], folder, folder_len);
    pos += folder_len;
    out[pos++] = '/';
    ui_page_settings_copy_bytes(&out[pos], name_copy, name_len);
    pos += name_len;
    ui_page_settings_copy_bytes(&out[pos], k_suffix, suffix_len);
    pos += suffix_len;
    out[pos] = '\0';
    return 1U;
}

static uint8_t ui_page_settings_sample_browser_refresh(void)
{
    if ((wav_loader_catalog_loaded() == 0U) || (wav_loader_catalog_stale() != 0U))
    {
        ui_page_settings_status("REFRESH LIB");
        return 0U;
    }

    const uint16_t catalog_child_count = wav_loader_catalog_child_count(g_ui_settings.sample_parent_id);
    const uint16_t parent_entry_count =
        (g_ui_settings.sample_parent_id != WAV_LOADER_CATALOG_ROOT_PARENT) ? 1U : 0U;
    if (wav_loader_catalog_last_sd_busy() != 0U)
    {
        ui_page_settings_sd_busy_status();
        return 0U;
    }
    if (wav_loader_catalog_last_io_error() != 0U)
    {
        ui_page_settings_status("CAT FAIL");
        return 0U;
    }
    g_ui_settings.sample_child_count = (uint16_t)(catalog_child_count + parent_entry_count);
    if (g_ui_settings.sample_selected >= g_ui_settings.sample_child_count)
    {
        g_ui_settings.sample_selected = (g_ui_settings.sample_child_count == 0U)
            ? 0U
            : (uint16_t)(g_ui_settings.sample_child_count - 1U);
    }
    const uint16_t page_start =
        (uint16_t)(g_ui_settings.sample_selected
                   - (g_ui_settings.sample_selected % UI_SETTINGS_SAMPLE_BROWSER_MAX));

    if (g_ui_settings.sample_child_count > parent_entry_count)
    {
        const uint16_t first_catalog_child =
            (page_start > parent_entry_count) ? (uint16_t)(page_start - parent_entry_count) : 0U;
        if (wav_loader_catalog_get_child(g_ui_settings.sample_parent_id, first_catalog_child) == 0)
        {
            if (wav_loader_catalog_last_sd_busy() != 0U)
            {
                ui_page_settings_sd_busy_status();
            }
            else if (wav_loader_catalog_last_io_error() != 0U)
            {
                ui_page_settings_status("CAT FAIL");
            }
            else
            {
                ui_page_settings_status("CAT FAIL");
            }
            return 0U;
        }
    }

    g_ui_settings.sample_entry_count = 0U;
    g_ui_settings.sample_page_start = page_start;
    for (uint16_t child = page_start;
         (child < g_ui_settings.sample_child_count) && (g_ui_settings.sample_entry_count < UI_SETTINGS_SAMPLE_BROWSER_MAX);
         ++child)
    {
        if ((parent_entry_count != 0U) && (child == 0U))
        {
            ui_settings_sample_entry_t *const entry =
                &g_ui_settings.sample_entries[g_ui_settings.sample_entry_count];
            (void)snprintf(entry->path, sizeof(entry->path), "..");
            (void)snprintf(entry->label, sizeof(entry->label), "..");
            entry->catalog_index = WAV_LOADER_CATALOG_ROOT_PARENT;
            entry->type = UI_SETTINGS_SAMPLE_ENTRY_PARENT;
            g_ui_settings.sample_entry_count++;
            continue;
        }
        const uint16_t catalog_child = (uint16_t)(child - parent_entry_count);
        const wav_loader_catalog_entry_t *const catalog_entry =
            wav_loader_catalog_get_child(g_ui_settings.sample_parent_id, catalog_child);
        if (catalog_entry == 0)
        {
            if (wav_loader_catalog_last_sd_busy() != 0U)
            {
                ui_page_settings_sd_busy_status();
                return 0U;
            }
            if (wav_loader_catalog_last_io_error() != 0U)
            {
                ui_page_settings_status("CAT FAIL");
                return 0U;
            }
            break;
        }

        ui_settings_sample_entry_t *const entry =
            &g_ui_settings.sample_entries[g_ui_settings.sample_entry_count];
        const int path_len = snprintf(entry->path, sizeof(entry->path), "%s", catalog_entry->path);
        if ((path_len < 0) || ((uint32_t)path_len >= sizeof(entry->path)))
        {
            continue;
        }

        ui_page_settings_make_sample_label(entry->label,
                                           sizeof(entry->label),
                                           catalog_entry->name,
                                           (catalog_entry->type == WAV_LOADER_CATALOG_ENTRY_DIR) ? 1U : 0U);
        if (catalog_entry->type == WAV_LOADER_CATALOG_ENTRY_DIR)
        {
            char prefixed[24];
            (void)snprintf(prefixed, sizeof(prefixed), "> %s", entry->label);
            (void)snprintf(entry->label, sizeof(entry->label), "%s", prefixed);
        }
        entry->catalog_index = wav_loader_catalog_get_child_index(g_ui_settings.sample_parent_id, catalog_child);
        entry->type = (catalog_entry->type == WAV_LOADER_CATALOG_ENTRY_DIR)
            ? UI_SETTINGS_SAMPLE_ENTRY_DIR
            : UI_SETTINGS_SAMPLE_ENTRY_FILE;
        g_ui_settings.sample_entry_count++;
    }

    if (wav_loader_catalog_truncated() != 0U)
    {
        ui_page_settings_status("LIB FULL");
    }
    if (g_ui_settings.sample_left_scroll >= g_ui_settings.sample_child_count)
    {
        g_ui_settings.sample_left_scroll = 0U;
    }

    return 1U;
}

static void ui_page_settings_multi_load_metadata(ui_settings_multi_entry_t *entry)
{
    if (entry == 0)
    {
        return;
    }

    entry->prepared = 0U;
    entry->sample_count = 0U;
    entry->zone_count = 0U;
    if (entry->index_path[0] == '\0')
    {
        return;
    }

    multi_sample_index_t index;
    if (multi_sample_index_load(entry->index_path, &index) == MULTI_SAMPLE_INDEX_OK)
    {
        entry->prepared = 1U;
        entry->sample_count = index.sample_count;
        entry->zone_count = index.zone_count;
        if (index.instrument_name[0] != '\0')
        {
            ui_page_settings_copy_bounded(entry->label,
                                          sizeof(entry->label),
                                          index.instrument_name);
        }
    }
}

static uint8_t ui_page_settings_multi_browser_refresh(void)
{
    DIR dir;
    FILINFO fno;

    g_ui_settings.multi_entry_count = 0U;
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U)
    {
        ui_page_settings_sd_busy_status();
        return 0U;
    }

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        ui_page_settings_status("SD UNAVAILABLE");
        return 0U;
    }

    FRESULT fr = f_opendir(&dir, UI_SETTINGS_MULTI_ROOT);
    if (fr != FR_OK)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        ui_page_settings_status("NO MULTI");
        return 0U;
    }

    while (g_ui_settings.multi_entry_count < UI_SETTINGS_MULTI_BROWSER_MAX)
    {
        fr = f_readdir(&dir, &fno);
        if ((fr != FR_OK) || (fno.fname[0] == '\0'))
        {
            break;
        }
        if ((fno.fname[0] == '.') || ((fno.fattrib & AM_DIR) == 0U))
        {
            continue;
        }

        ui_settings_multi_entry_t *const entry =
            &g_ui_settings.multi_entries[g_ui_settings.multi_entry_count];
        memset(entry, 0, sizeof(*entry));
        if (ui_page_settings_join_path(entry->path, sizeof(entry->path), UI_SETTINGS_MULTI_ROOT, fno.fname) == 0U)
        {
            continue;
        }
        ui_page_settings_make_sample_label(entry->label, sizeof(entry->label), fno.fname, 1U);
        (void)ui_page_settings_multi_index_name(entry->path, entry->index_path, sizeof(entry->index_path));
        g_ui_settings.multi_entry_count++;
    }

    (void)f_closedir(&dir);
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);

    for (uint8_t i = 0U; i < g_ui_settings.multi_entry_count; ++i)
    {
        ui_page_settings_multi_load_metadata(&g_ui_settings.multi_entries[i]);
    }

    if (g_ui_settings.sample_selected >= g_ui_settings.multi_entry_count)
    {
        g_ui_settings.sample_selected = (g_ui_settings.multi_entry_count == 0U)
            ? 0U
            : (uint16_t)(g_ui_settings.multi_entry_count - 1U);
    }
    if (g_ui_settings.sample_left_scroll >= g_ui_settings.multi_entry_count)
    {
        g_ui_settings.sample_left_scroll = 0U;
    }

    return 1U;
}

static void ui_page_settings_sample_browser_enter_root(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_SAMPLE_ROOT);
    g_ui_settings.sample_parent_id = WAV_LOADER_CATALOG_ROOT_PARENT;
    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_slot_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    g_ui_settings.sample_right_scroll = 0U;
    g_ui_settings.sample_focus = (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY;
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
    g_ui_settings.confirm_slot = 0U;
    g_ui_settings.sample_preview_volume = 100U;
    g_ui_settings.confirm_path[0] = '\0';
    sd_preview_set_gain(1.0f);
    (void)ui_page_settings_sample_browser_refresh();
}

static void ui_page_settings_multi_browser_enter_root(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_slot_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    g_ui_settings.sample_right_scroll = 0U;
    g_ui_settings.sample_focus = (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY;
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
    g_ui_settings.confirm_slot = 0U;
    g_ui_settings.confirm_path[0] = '\0';
    (void)ui_page_settings_multi_browser_refresh();
}

static void ui_page_settings_sample_browser_parent_or_exit(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (strcmp(g_ui_settings.sample_dir, UI_SETTINGS_SAMPLE_ROOT) == 0)
    {
        ui_page_settings_back();
        return;
    }

    if (g_ui_settings.sample_parent_id != WAV_LOADER_CATALOG_ROOT_PARENT)
    {
        const wav_loader_catalog_entry_t *const parent = wav_loader_catalog_get(g_ui_settings.sample_parent_id);
        if ((parent == 0) && (wav_loader_catalog_last_sd_busy() != 0U))
        {
            ui_page_settings_sd_busy_status();
            return;
        }
        g_ui_settings.sample_parent_id = (parent != 0) ? parent->parent_id : WAV_LOADER_CATALOG_ROOT_PARENT;
    }

    char *slash = strrchr(g_ui_settings.sample_dir, '/');
    if ((slash == 0) || (slash <= (g_ui_settings.sample_dir + strlen(UI_SETTINGS_SAMPLE_ROOT))))
    {
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_SAMPLE_ROOT);
    }
    else
    {
        *slash = '\0';
    }

    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    (void)ui_page_settings_sample_browser_refresh();
}

static uint8_t ui_page_settings_sample_restore_dir_after_refresh(const char *dir_path,
                                                                 const char *selection_path)
{
    if ((dir_path == 0) || (dir_path[0] == '\0'))
    {
        return 0U;
    }

    char probe[WAV_LOADER_CATALOG_PATH_MAX];
    (void)snprintf(probe, sizeof(probe), "%s", dir_path);
    while (1)
    {
        uint16_t dir_index = WAV_LOADER_CATALOG_ROOT_PARENT;
        wav_loader_catalog_entry_t dir_entry;
        uint8_t found = 0U;
        if (strcmp(probe, UI_SETTINGS_SAMPLE_ROOT) == 0)
        {
            found = 1U;
            dir_index = WAV_LOADER_CATALOG_ROOT_PARENT;
        }
        else if ((wav_loader_catalog_find_path(probe, &dir_index, &dir_entry) != 0U)
                 && (dir_entry.type == WAV_LOADER_CATALOG_ENTRY_DIR))
        {
            found = 1U;
        }

        if (found != 0U)
        {
            (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", probe);
            g_ui_settings.sample_parent_id = dir_index;
            g_ui_settings.sample_selected = 0U;
            if ((selection_path != 0) && (selection_path[0] != '\0'))
            {
                uint16_t selected_index = WAV_LOADER_CATALOG_ROOT_PARENT;
                wav_loader_catalog_entry_t selected_entry;
                if ((wav_loader_catalog_find_path(selection_path, &selected_index, &selected_entry) != 0U)
                    && (selected_entry.parent_id == dir_index))
                {
                    const uint16_t parent_entry_count =
                        (dir_index != WAV_LOADER_CATALOG_ROOT_PARENT) ? 1U : 0U;
                    const uint16_t child_count = wav_loader_catalog_child_count(dir_index);
                    for (uint16_t child = 0U; child < child_count; ++child)
                    {
                        if (wav_loader_catalog_get_child_index(dir_index, child) == selected_index)
                        {
                            g_ui_settings.sample_selected = (uint16_t)(child + parent_entry_count);
                            break;
                        }
                    }
                }
            }
            g_ui_settings.sample_left_scroll = 0U;
            (void)ui_page_settings_sample_browser_refresh();
            return 1U;
        }

        char *slash = strrchr(probe, '/');
        if ((slash == 0) || (slash <= (probe + strlen(UI_SETTINGS_SAMPLE_ROOT))))
        {
            (void)snprintf(probe, sizeof(probe), "%s", UI_SETTINGS_SAMPLE_ROOT);
            if (strcmp(dir_path, UI_SETTINGS_SAMPLE_ROOT) == 0)
            {
                return 0U;
            }
            dir_path = UI_SETTINGS_SAMPLE_ROOT;
        }
        else
        {
            *slash = '\0';
        }
    }
}

static void ui_page_settings_sample_catalog_refresh_action(uint8_t rebuild)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    char old_dir[WAV_LOADER_CATALOG_PATH_MAX];
    char old_selection[WAV_LOADER_CATALOG_PATH_MAX];
    (void)snprintf(old_dir, sizeof(old_dir), "%s", g_ui_settings.sample_dir);
    old_selection[0] = '\0';
    const ui_settings_sample_entry_t *const selected_entry = ui_page_settings_sample_selected_entry();
    if (selected_entry != 0)
    {
        (void)snprintf(old_selection, sizeof(old_selection), "%s", selected_entry->path);
    }

    ui_page_settings_status((rebuild != 0U) ? "REBUILD" : "REFRESH");
    if (rebuild != 0U)
    {
        wav_loader_catalog_rebuild();
    }
    else
    {
        wav_loader_catalog_refresh();
    }

    if ((wav_loader_catalog_last_sd_busy() != 0U) || (wav_loader_catalog_stale() != 0U))
    {
        ui_page_settings_sd_busy_status();
        return;
    }

    if (ui_page_settings_sample_restore_dir_after_refresh(old_dir, old_selection) == 0U)
    {
        g_ui_settings.sample_parent_id = WAV_LOADER_CATALOG_ROOT_PARENT;
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_SAMPLE_ROOT);
        g_ui_settings.sample_selected = 0U;
        g_ui_settings.sample_left_scroll = 0U;
        (void)ui_page_settings_sample_browser_refresh();
    }
    ui_page_settings_status((wav_loader_catalog_truncated() != 0U) ? "LIB FULL" : "LIB OK");
}

static int16_t ui_page_settings_sample_find_free_slot(void)
{
    for (uint8_t slot = 0U; slot < SAMPLE_POOL_SIZE; ++slot)
    {
        if (sample_pool_get_state(slot) == SAMPLE_POOL_SLOT_EMPTY)
        {
            return (int16_t)slot;
        }
    }
    return -1;
}

static const ui_settings_sample_entry_t *ui_page_settings_sample_selected_entry(void)
{
    if ((g_ui_settings.sample_selected < g_ui_settings.sample_page_start)
        || (g_ui_settings.sample_selected >= (uint16_t)(g_ui_settings.sample_page_start
                                                        + g_ui_settings.sample_entry_count)))
    {
        return 0;
    }
    return &g_ui_settings.sample_entries[g_ui_settings.sample_selected - g_ui_settings.sample_page_start];
}

static void ui_page_settings_sample_confirm_convert(uint8_t slot, const char *path)
{
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_CONVERT;
    g_ui_settings.confirm_slot = slot;
    (void)snprintf(g_ui_settings.confirm_path, sizeof(g_ui_settings.confirm_path), "%s", path);
    ui_page_settings_status("CONVERT TO 48K ?");
}

static void ui_page_settings_sample_load_to_slot(uint8_t slot, const char *path)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (sample_pool_load(slot, path) != 0U)
    {
        ui_page_settings_refresh_sampler_slots();
        ui_page_settings_status("LOAD OK");
    }
    else
    {
        if (((sample_pool_get_last_load_error() == SAMPLE_POOL_LOAD_WAV_UNSUPPORTED_FORMAT)
             || (sample_pool_get_last_load_error() == SAMPLE_POOL_LOAD_WAV_48K_REQUIRED))
            && (wav_convert_path_needs_48k(path, 0) != 0U))
        {
            ui_page_settings_sample_confirm_convert(slot, path);
            return;
        }
        ui_page_settings_status(ui_page_settings_sampler_load_error_label());
    }
}

static void ui_page_settings_sample_preview_left(void)
{
    const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
    if ((entry == 0) || (entry->type != UI_SETTINGS_SAMPLE_ENTRY_FILE))
    {
        return;
    }

    if ((sd_preview_is_active() != 0U) && (strcmp(sd_preview_get_path(), entry->path) == 0))
    {
        ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_USER);
        return;
    }

    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
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

static void ui_page_settings_sample_preview_right(void)
{
    const sample_pool_slot_state_t state = sample_pool_get_state(g_ui_settings.sample_slot_selected);
    if (state == SAMPLE_POOL_SLOT_EMPTY)
    {
        ui_page_settings_status("SLOT EMPTY");
        return;
    }

    const sample_desc_t *const desc = sample_pool_get(g_ui_settings.sample_slot_selected);
    if ((desc == 0) || (desc->path[0] == '\0'))
    {
        ui_page_settings_status("NO PATH");
        return;
    }

    if ((sd_preview_is_active() != 0U) && (strcmp(sd_preview_get_path(), desc->path) == 0))
    {
        ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_USER);
        return;
    }

    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (sd_preview_begin(desc->path) != 0U)
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

static void ui_page_settings_sample_preview_current(void)
{
    if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
    {
        ui_page_settings_sample_preview_left();
        return;
    }

    ui_page_settings_sample_preview_right();
}

static void ui_page_settings_sample_ok_action(void)
{
    ui_page_settings_sample_copy_left(0U);
}

static void ui_page_settings_sample_confirm_replace(uint8_t slot, const char *path)
{
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_REPLACE;
    g_ui_settings.confirm_slot = slot;
    (void)snprintf(g_ui_settings.confirm_path, sizeof(g_ui_settings.confirm_path), "%s", path);
    ui_page_settings_status("OK YES RETURN NO");
}

static void ui_page_settings_sample_request_replace(uint8_t slot, const char *path)
{
    if ((path == 0) || (path[0] == '\0'))
    {
        return;
    }

    if (sample_pool_get_state(slot) == SAMPLE_POOL_SLOT_EMPTY)
    {
        ui_page_settings_sample_load_to_slot(slot, path);
        return;
    }

    ui_page_settings_sample_confirm_replace(slot, path);
}

static void ui_page_settings_sample_confirm_clear(uint8_t slot)
{
    if (sample_pool_get_state(slot) == SAMPLE_POOL_SLOT_EMPTY)
    {
        ui_page_settings_status("SLOT EMPTY");
        return;
    }

    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_CLEAR;
    g_ui_settings.confirm_slot = slot;
    g_ui_settings.confirm_path[0] = '\0';
    ui_page_settings_status("OK YES RETURN NO");
}

static void ui_page_settings_sample_confirm_accept(void)
{
    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_CONVERT)
    {
        const uint8_t slot = g_ui_settings.confirm_slot;
        char path[SAMPLE_POOL_PATH_MAX];
        if (strlen(g_ui_settings.confirm_path) >= sizeof(path))
        {
            g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
            g_ui_settings.confirm_path[0] = '\0';
            ui_page_settings_status("PATH TOO LONG");
            return;
        }
        ui_page_settings_copy_bounded(path, sizeof(path), g_ui_settings.confirm_path);
        g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
        g_ui_settings.confirm_path[0] = '\0';

        if ((seq_runtime_is_running() != 0U)
            || (seq_runtime_is_start_pending() != 0U)
            || (multi_record_writer_any_active() != 0U)
            || (looper_storage_raw_export_is_active() != 0U))
        {
            ui_page_settings_status("STOP AUDIO TO CONVERT");
            return;
        }

        ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
        if (wav_convert_start_destructive_48k(path) == 0U)
        {
            ui_page_settings_status("CONVERT FAIL");
            return;
        }

        g_ui_settings.convert_slot_valid = 1U;
        g_ui_settings.convert_slot = slot;
        (void)snprintf(g_ui_settings.convert_path, sizeof(g_ui_settings.convert_path), "%s", path);
        ui_page_settings_status("CONVERT 0%");
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_REPLACE)
    {
        const uint8_t slot = g_ui_settings.confirm_slot;
        char path[SAMPLE_POOL_PATH_MAX];
        if (strlen(g_ui_settings.confirm_path) >= sizeof(path))
        {
            g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
            g_ui_settings.confirm_path[0] = '\0';
            ui_page_settings_status("PATH TOO LONG");
            return;
        }
        ui_page_settings_copy_bounded(path, sizeof(path), g_ui_settings.confirm_path);
        g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
        g_ui_settings.confirm_path[0] = '\0';
        ui_page_settings_sample_load_to_slot(slot, path);
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_CLEAR)
    {
        sample_pool_clear(g_ui_settings.confirm_slot);
        ui_page_settings_refresh_sampler_slots();
        g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
        ui_page_settings_status("CLEAR OK");
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARE)
    {
        const uint8_t slot = g_ui_settings.confirm_slot;
        char path[MULTI_SAMPLE_POOL_PATH_MAX];
        (void)snprintf(path, sizeof(path), "%s", g_ui_settings.confirm_path);
        g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
        g_ui_settings.confirm_path[0] = '\0';

        const ui_settings_multi_entry_t *entry = ui_page_settings_multi_find_entry_by_path(path);
        if ((entry == 0) || (ui_page_settings_multi_prepare_entry(entry) == 0U))
        {
            return;
        }
        entry = ui_page_settings_multi_find_entry_by_path(path);
        ui_page_settings_multi_load_entry_to_slot(slot, entry);
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_REPLACE)
    {
        const uint8_t slot = g_ui_settings.confirm_slot;
        char path[MULTI_SAMPLE_POOL_PATH_MAX];
        (void)snprintf(path, sizeof(path), "%s", g_ui_settings.confirm_path);
        g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
        g_ui_settings.confirm_path[0] = '\0';

        const ui_settings_multi_entry_t *entry = ui_page_settings_multi_find_entry_by_path(path);
        if (entry == 0)
        {
            ui_page_settings_status("NO MULTI");
            return;
        }
        if ((entry->prepared == 0U) && (ui_page_settings_multi_prepare_entry(entry) == 0U))
        {
            return;
        }
        entry = ui_page_settings_multi_find_entry_by_path(path);
        ui_page_settings_multi_load_entry_to_slot(slot, entry);
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_UNLOAD)
    {
        if (multi_sample_pool_clear_instrument(g_ui_settings.confirm_slot) != 0U)
        {
            ui_page_settings_status("UNLOAD OK");
        }
        else
        {
            ui_page_settings_status("UNLOAD FAIL");
        }
        g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
        return;
    }

    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
}

static void ui_page_settings_sample_confirm_cancel(void)
{
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
    g_ui_settings.confirm_path[0] = '\0';
    ui_page_settings_status("CANCEL");
}

static void ui_page_settings_sample_copy_left(uint8_t shift_down)
{
    const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
    if (entry == 0)
    {
        ui_page_settings_status("NO WAV");
        return;
    }

    if (entry->type == UI_SETTINGS_SAMPLE_ENTRY_PARENT)
    {
        ui_page_settings_sample_browser_parent_or_exit();
        return;
    }

    if (entry->type == UI_SETTINGS_SAMPLE_ENTRY_DIR)
    {
        ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
        const uint16_t old_parent_id = g_ui_settings.sample_parent_id;
        char old_dir[WAV_LOADER_CATALOG_PATH_MAX];
        (void)snprintf(old_dir, sizeof(old_dir), "%s", g_ui_settings.sample_dir);
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", entry->path);
        g_ui_settings.sample_parent_id = entry->catalog_index;
        g_ui_settings.sample_selected = 0U;
        if (ui_page_settings_sample_browser_refresh() == 0U)
        {
            g_ui_settings.sample_parent_id = old_parent_id;
            (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", old_dir);
        }
        return;
    }

    if (shift_down != 0U)
    {
        ui_page_settings_sample_request_replace(g_ui_settings.sample_slot_selected, entry->path);
        return;
    }

    const int16_t free_slot = ui_page_settings_sample_find_free_slot();
    if (free_slot < 0)
    {
        ui_page_settings_status("POOL FULL");
        return;
    }

    ui_page_settings_sample_load_to_slot((uint8_t)free_slot, entry->path);
    g_ui_settings.sample_slot_selected = (uint8_t)free_slot;
}

static void ui_page_settings_sample_copy_right(uint8_t shift_down)
{
    if (shift_down != 0U)
    {
        const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
        if ((entry == 0) || (entry->type != UI_SETTINGS_SAMPLE_ENTRY_FILE))
        {
            ui_page_settings_status("SELECT WAV");
            return;
        }
        ui_page_settings_sample_request_replace(g_ui_settings.sample_slot_selected, entry->path);
        return;
    }

    ui_page_settings_sample_confirm_clear(g_ui_settings.sample_slot_selected);
}

static const ui_settings_multi_entry_t *ui_page_settings_multi_selected_entry(void)
{
    if (g_ui_settings.sample_selected >= g_ui_settings.multi_entry_count)
    {
        return 0;
    }
    return &g_ui_settings.multi_entries[g_ui_settings.sample_selected];
}

static const ui_settings_multi_entry_t *ui_page_settings_multi_find_entry_by_path(const char *path)
{
    if ((path == 0) || (path[0] == '\0'))
    {
        return 0;
    }
    for (uint8_t i = 0U; i < g_ui_settings.multi_entry_count; ++i)
    {
        if (strcmp(g_ui_settings.multi_entries[i].path, path) == 0)
        {
            return &g_ui_settings.multi_entries[i];
        }
    }
    return 0;
}

static uint16_t ui_page_settings_multi_pool_used(void)
{
    return multi_sample_pool_get_sample_capacity_used();
}

static void ui_page_settings_append_u16(char *out, uint32_t out_size, uint16_t value)
{
    char digits[5];
    uint8_t digit_count = 0U;

    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    do
    {
        digits[digit_count++] = (char)('0' + (value % 10U));
        value = (uint16_t)(value / 10U);
    } while ((value != 0U) && (digit_count < sizeof(digits)));

    const uint32_t len = (uint32_t)strlen(out);
    uint32_t pos = len;
    while ((digit_count != 0U) && ((pos + 1U) < out_size))
    {
        out[pos++] = digits[--digit_count];
    }
    out[pos] = '\0';
}

static int16_t ui_page_settings_multi_find_free_slot(void)
{
    for (uint8_t slot = 0U; slot < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++slot)
    {
        if (multi_sample_pool_get_state(slot) == MULTI_SAMPLE_INSTRUMENT_EMPTY)
        {
            return (int16_t)slot;
        }
    }
    return -1;
}

static int16_t ui_page_settings_multi_find_loaded_path(const char *index_path)
{
    if ((index_path == 0) || (index_path[0] == '\0'))
    {
        return -1;
    }

    for (uint8_t slot = 0U; slot < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++slot)
    {
        const multi_sample_instrument_t *const instrument = multi_sample_pool_get_instrument(slot);
        if ((instrument != 0) && (instrument->index_path[0] != '\0')
            && (strcmp(instrument->index_path, index_path) == 0))
        {
            return (int16_t)slot;
        }
    }
    return -1;
}

static uint8_t ui_page_settings_multi_assign_active_track(uint16_t instrument_id, const char *index_path)
{
    const uint8_t track = ui_get_active_track();

    if ((index_path == 0)
        || (index_path[0] == '\0')
        || (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
        || (ui_get_track_family(track) != UI_TRACK_FAMILY_SAMPLER)
        || (ui_get_track_type(track) != UI_TRACK_TYPE_MULTI))
    {
        return 0U;
    }

    if (project_v1_set_track_multi_path(track, index_path) == 0U)
    {
        return 0U;
    }

    brick6_sampler_runtime_set_multi_instrument(track, instrument_id);
    return 1U;
}

static const char *ui_page_settings_multi_import_error_label(multi_sample_import_result_t result)
{
    switch (result)
    {
        case MULTI_SAMPLE_IMPORT_SD_BUSY:
            return "SD BUSY";
        case MULTI_SAMPLE_IMPORT_SD_MOUNT_FAIL:
            return "SD UNAVAILABLE";
        case MULTI_SAMPLE_IMPORT_OPEN_DIR_FAIL:
            return "NO SAMPLES";
        case MULTI_SAMPLE_IMPORT_NO_WAV:
            return "NO WAV";
        case MULTI_SAMPLE_IMPORT_TOO_MANY_SAMPLES:
            return "TOO MANY";
        case MULTI_SAMPLE_IMPORT_PATH_TOO_LONG:
            return "PATH LONG";
        case MULTI_SAMPLE_IMPORT_WAV_OPEN_FAIL:
            return "OPEN FAIL";
        case MULTI_SAMPLE_IMPORT_WAV_PARSE_FAIL:
            return "WAV INVALID";
        case MULTI_SAMPLE_IMPORT_WAV_UNSUPPORTED:
            return "WAV UNSUPP";
        case MULTI_SAMPLE_IMPORT_DUPLICATE_ZONE:
        {
            const char *const diag = multi_sample_import_get_last_diagnostic();
            return ((diag != 0) && (diag[0] != '\0')) ? diag : "DUP ZONE";
        }
        case MULTI_SAMPLE_IMPORT_ZONE_LIMIT:
            return "ZONE FULL";
        case MULTI_SAMPLE_IMPORT_INDEX_WRITE_FAIL:
            return "INDEX FAIL";
        default:
            return "PREP FAIL";
    }
}

static const char *ui_page_settings_multi_load_error_label(multi_sample_load_result_t result)
{
    switch (result)
    {
        case MULTI_SAMPLE_LOAD_ALREADY_READY:
            return "ALREADY";
        case MULTI_SAMPLE_LOAD_SD_BUSY:
            return "SD BUSY";
        case MULTI_SAMPLE_LOAD_INDEX_FAIL:
            return "INDEX FAIL";
        case MULTI_SAMPLE_LOAD_POOL_FAIL:
            return "POOL FAIL";
        case MULTI_SAMPLE_LOAD_PATH_TOO_LONG:
            return "PATH LONG";
        case MULTI_SAMPLE_LOAD_REGISTER_FAIL:
            return "REGISTER FAIL";
        case MULTI_SAMPLE_LOAD_NOT_ENOUGH_CACHE:
            return "CACHE FULL";
        case MULTI_SAMPLE_LOAD_PAGE_ERROR:
            return "PAGE FAIL";
        default:
            return "LOAD FAIL";
    }
}

static uint8_t ui_page_settings_multi_prepare_entry(const ui_settings_multi_entry_t *entry)
{
    if (entry == 0)
    {
        return 0U;
    }
    if (entry->prepared != 0U)
    {
        return 1U;
    }

    const multi_sample_import_result_t result = multi_sample_import_folder(entry->path);
    if (result != MULTI_SAMPLE_IMPORT_OK)
    {
        ui_page_settings_status(ui_page_settings_multi_import_error_label(result));
        return 0U;
    }

    (void)ui_page_settings_multi_browser_refresh();
    ui_page_settings_status("PREP OK");
    return 1U;
}

static void ui_page_settings_multi_confirm_prepare(uint8_t slot)
{
    const ui_settings_multi_entry_t *const entry = ui_page_settings_multi_selected_entry();
    if (entry == 0)
    {
        return;
    }
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARE;
    g_ui_settings.confirm_slot = slot;
    ui_page_settings_copy_bounded(g_ui_settings.confirm_path,
                                  sizeof(g_ui_settings.confirm_path),
                                  entry->path);
    ui_page_settings_status("OK=YES RETURN=NO");
}

static void ui_page_settings_multi_confirm_replace(uint8_t slot)
{
    const ui_settings_multi_entry_t *const entry = ui_page_settings_multi_selected_entry();
    if (entry == 0)
    {
        return;
    }
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_REPLACE;
    g_ui_settings.confirm_slot = slot;
    ui_page_settings_copy_bounded(g_ui_settings.confirm_path,
                                  sizeof(g_ui_settings.confirm_path),
                                  entry->path);
    ui_page_settings_status("OK=YES RETURN=NO");
}

static void ui_page_settings_multi_confirm_unload(uint8_t slot)
{
    if (multi_sample_pool_get_state(slot) == MULTI_SAMPLE_INSTRUMENT_EMPTY)
    {
        ui_page_settings_status("SLOT EMPTY");
        return;
    }
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_UNLOAD;
    g_ui_settings.confirm_slot = slot;
    g_ui_settings.confirm_path[0] = '\0';
    ui_page_settings_status("OK=YES RETURN=NO");
}

static void ui_page_settings_multi_load_entry_to_slot(uint8_t slot, const ui_settings_multi_entry_t *entry)
{
    if ((entry == 0) || (entry->index_path[0] == '\0'))
    {
        ui_page_settings_status("NO INDEX");
        return;
    }

    const int16_t existing = ui_page_settings_multi_find_loaded_path(entry->index_path);
    if (existing >= 0)
    {
        g_ui_settings.sample_slot_selected = (uint8_t)existing;
        (void)ui_page_settings_multi_assign_active_track((uint16_t)existing, entry->index_path);
        ui_page_settings_status("REUSED");
        return;
    }

    if (entry->sample_count > (uint16_t)(MULTI_SAMPLE_POOL_MAX_SAMPLES - ui_page_settings_multi_pool_used()))
    {
        char status[24];
        ui_page_settings_copy_bounded(status, sizeof(status), "FULL need ");
        ui_page_settings_append_u16(status, sizeof(status), entry->sample_count);
        ui_page_settings_status(status);
        return;
    }

    if (multi_sample_pool_get_state(slot) != MULTI_SAMPLE_INSTRUMENT_EMPTY)
    {
        (void)multi_sample_pool_clear_instrument(slot);
    }

    const multi_sample_load_result_t result = multi_sample_load_instrument(entry->index_path, slot);
    if ((result == MULTI_SAMPLE_LOAD_OK) || (result == MULTI_SAMPLE_LOAD_ALREADY_READY))
    {
        (void)multi_sample_pool_set_index_path(slot, entry->index_path);
        g_ui_settings.sample_slot_selected = slot;
        (void)ui_page_settings_multi_assign_active_track(slot, entry->index_path);
        ui_page_settings_status("LOAD QUEUED");
    }
    else
    {
        ui_page_settings_status(ui_page_settings_multi_load_error_label(result));
    }
}

static void ui_page_settings_multi_load_selected_to_slot(uint8_t slot)
{
    const ui_settings_multi_entry_t *entry = ui_page_settings_multi_selected_entry();
    if (entry == 0)
    {
        ui_page_settings_status("NO MULTI");
        return;
    }

    if (entry->prepared == 0U)
    {
        ui_page_settings_multi_confirm_prepare(slot);
        return;
    }
    ui_page_settings_multi_load_entry_to_slot(slot, entry);
}

static void ui_page_settings_multi_copy_left(uint8_t shift_down)
{
    const ui_settings_multi_entry_t *const entry = ui_page_settings_multi_selected_entry();
    if (entry == 0)
    {
        ui_page_settings_status("NO MULTI");
        return;
    }

    if (shift_down != 0U)
    {
        if (multi_sample_pool_get_state(g_ui_settings.sample_slot_selected) != MULTI_SAMPLE_INSTRUMENT_EMPTY)
        {
            ui_page_settings_multi_confirm_replace(g_ui_settings.sample_slot_selected);
            return;
        }
        ui_page_settings_multi_load_selected_to_slot(g_ui_settings.sample_slot_selected);
        return;
    }

    const int16_t existing = ui_page_settings_multi_find_loaded_path(entry->index_path);
    if (existing >= 0)
    {
        g_ui_settings.sample_slot_selected = (uint8_t)existing;
        (void)ui_page_settings_multi_assign_active_track((uint16_t)existing, entry->index_path);
        ui_page_settings_status("REUSED");
        return;
    }

    const int16_t free_slot = ui_page_settings_multi_find_free_slot();
    if (free_slot < 0)
    {
        ui_page_settings_status("POOL FULL");
        return;
    }
    ui_page_settings_multi_load_selected_to_slot((uint8_t)free_slot);
}

static void ui_page_settings_multi_copy_right(uint8_t shift_down)
{
    if (shift_down != 0U)
    {
        if (multi_sample_pool_get_state(g_ui_settings.sample_slot_selected) != MULTI_SAMPLE_INSTRUMENT_EMPTY)
        {
            ui_page_settings_multi_confirm_replace(g_ui_settings.sample_slot_selected);
            return;
        }
        ui_page_settings_multi_load_selected_to_slot(g_ui_settings.sample_slot_selected);
        return;
    }

    ui_page_settings_multi_confirm_unload(g_ui_settings.sample_slot_selected);
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
            return "SAMPLES";
        case UI_SETTINGS_VIEW_MULTI_SAMPLE:
            return "MULTI";
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
            return 3U;
        case UI_SETTINGS_VIEW_PROJECT:
            return 3U;
        case UI_SETTINGS_VIEW_SAMPLER:
        case UI_SETTINGS_VIEW_MULTI_SAMPLE:
            return 0U;
        case UI_SETTINGS_VIEW_SAMPLER_SLOT:
            return (uint8_t)UI_SETTINGS_SAMPLER_ACTION_COUNT;
        case UI_SETTINGS_VIEW_SAMPLER_CATALOG:
            return (wav_loader_catalog_count() > 255U) ? 255U : (uint8_t)wav_loader_catalog_count();
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
            if (index == 0U)
            {
                return "SAMPLE";
            }
            if (index == 1U)
            {
                return "MULTI-SAMPLE";
            }
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
    const ui_settings_menu_level_t *const level = ui_page_settings_current_level();
    if ((level != 0) && (level->view == UI_SETTINGS_VIEW_SAMPLER))
    {
        sd_preview_set_gain(1.0f);
    }

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
                ui_page_settings_refresh_sampler_slots();
                ui_page_settings_sample_browser_enter_root();
                ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLER);
            }
            else if (level->selected_index == 1U)
            {
                ui_page_settings_multi_browser_enter_root();
                ui_page_settings_push(UI_SETTINGS_VIEW_MULTI_SAMPLE);
            }
            else
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT);
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
            if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
            {
                ui_page_settings_sample_copy_left((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            else
            {
                ui_page_settings_sample_copy_right((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            break;

        case UI_SETTINGS_VIEW_MULTI_SAMPLE:
            if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
            {
                ui_page_settings_multi_copy_left((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            else
            {
                ui_page_settings_multi_copy_right((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            break;

        case UI_SETTINGS_VIEW_SAMPLER_SLOT:
            if (level->selected_index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_LOAD_OR_REPLACE)
            {
                ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
                if ((wav_loader_catalog_loaded() != 0U)
                    && (wav_loader_catalog_stale() == 0U)
                    && (wav_loader_catalog_count() != 0U))
                {
                    g_ui_settings.sampler_catalog_mode = UI_SETTINGS_SAMPLER_CATALOG_MODE_LOAD;
                    if (wav_loader_catalog_truncated() != 0U)
                    {
                        ui_page_settings_status("LIB FULL");
                    }
                    else if (wav_loader_catalog_path_truncated() != 0U)
                    {
                        ui_page_settings_status("PATH LONG");
                    }
                    ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLER_CATALOG);
                }
                else
                {
                    ui_page_settings_status("REFRESH LIB");
                }
            }
            else if (level->selected_index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_PREVIEW_OR_STOP)
            {
                if (sd_preview_is_active() != 0U)
                {
                    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_USER);
                }
                else
                {
                    if ((wav_loader_catalog_loaded() != 0U)
                        && (wav_loader_catalog_stale() == 0U)
                        && (wav_loader_catalog_count() != 0U))
                    {
                        g_ui_settings.sampler_catalog_mode = UI_SETTINGS_SAMPLER_CATALOG_MODE_PREVIEW;
                        if (wav_loader_catalog_truncated() != 0U)
                        {
                            ui_page_settings_status("LIB FULL");
                        }
                        else if (wav_loader_catalog_path_truncated() != 0U)
                        {
                            ui_page_settings_status("PATH LONG");
                        }
                        ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLER_CATALOG);
                    }
                    else
                    {
                        ui_page_settings_status("REFRESH LIB");
                    }
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
                    else
                    {
                        ui_page_settings_sample_load_to_slot(g_ui_settings.selected_slot, entry->path);
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
    g_ui_settings.convert_slot_valid = 0U;
    g_ui_settings.convert_slot = 0U;
    g_ui_settings.convert_path[0] = '\0';
    for (uint8_t i = 0U; i < UI_SETTINGS_ENCODER_COUNT; ++i)
    {
        g_ui_settings.encoder_accum[i] = 0;
    }
    ui_page_settings_refresh_sampler_slots();
    ui_page_settings_status(0);
    ui_page_settings_push(UI_SETTINGS_VIEW_ROOT);
}

static void ui_page_settings_leave(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    sd_preview_set_gain(1.0f);
    g_ui_settings.depth = 0U;
}

static void ui_page_settings_handle_event_internal(const ui_event_t *ev)
{
    if ((wav_convert_is_active() != 0U) || (g_ui_settings.convert_slot_valid != 0U))
    {
        return;
    }

    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        if ((ev != 0)
            && (ev->type == UI_EVENT_HALL_PRESS)
            && (ui_page_settings_current_level() != 0)
            && (ui_page_settings_current_level()->view == UI_SETTINGS_VIEW_SAMPLER))
        {
            ui_page_settings_sample_preview_current();
        }
        return;
    }

    ui_settings_menu_level_t *const level = ui_page_settings_current_level();
    if ((level != 0)
        && ((level->view == UI_SETTINGS_VIEW_SAMPLER)
            || (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)))
    {
        if (g_ui_settings.sample_confirm != (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE)
        {
            if (ev->id == (uint8_t)BTN_PAGE_2)
            {
                ui_page_settings_sample_confirm_accept();
                return;
            }
            if (ev->id == (uint8_t)BTN_PAGE_1)
            {
                ui_page_settings_sample_confirm_cancel();
                return;
            }
            if ((ev->id == (uint8_t)BTN_COPY) || (ev->id == (uint8_t)BTN_PASTE))
            {
                return;
            }
        }

        if ((ev->id == (uint8_t)BTN_COPY) || (ev->id == (uint8_t)BTN_PASTE))
        {
            return;
        }

        if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
            && (ev->id == (uint8_t)BTN_PAGE_1))
        {
            ui_page_settings_sample_browser_parent_or_exit();
            return;
        }

        if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
            && (ev->id == (uint8_t)BTN_PAGE_2)
            && (button_down(BTN_SHIFT) == 0U))
        {
            ui_page_settings_sample_ok_action();
            return;
        }

        if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
            && (ev->id == (uint8_t)BTN_PAGE_3))
        {
            return;
        }

        if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
            && (ev->id == (uint8_t)BTN_PAGE_4))
        {
            ui_page_settings_sample_catalog_refresh_action((uint8_t)(button_down(BTN_SHIFT) != 0U));
            return;
        }

        if (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
        {
            if (ev->id == (uint8_t)BTN_PAGE_1)
            {
                ui_page_settings_back();
                return;
            }
            if ((ev->id == (uint8_t)BTN_PAGE_2) && (button_down(BTN_SHIFT) == 0U))
            {
                ui_page_settings_apply_action();
                return;
            }
            if ((ev->id == (uint8_t)BTN_PAGE_3) || (ev->id == (uint8_t)BTN_PAGE_4))
            {
                return;
            }
        }

        if (ev->id == (uint8_t)BTN_SETTINGS)
        {
            ui_page_settings_back();
            return;
        }

        return;
    }

    if (ev->id == (uint8_t)BTN_PAGE_1)
    {
        ui_page_settings_back();
        return;
    }

    if (ev->id == (uint8_t)BTN_PAGE_2)
    {
        ui_page_settings_apply_action();
        return;
    }

    if ((ev->id == (uint8_t)BTN_PAGE_3) || (ev->id == (uint8_t)BTN_PAGE_4)
        || (ev->id == (uint8_t)BTN_COPY) || (ev->id == (uint8_t)BTN_PASTE))
    {
        return;
    }

    if (ev->id == (uint8_t)BTN_SETTINGS)
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
    if ((wav_convert_is_active() != 0U) || (g_ui_settings.convert_slot_valid != 0U))
    {
        wav_convert_service(65536U);
        const wav_convert_state_t convert_state = wav_convert_get_state();
        if (convert_state == WAV_CONVERT_STATE_ACTIVE)
        {
            (void)snprintf(g_ui_settings.status_line,
                           sizeof(g_ui_settings.status_line),
                           "CONVERT %u%%",
                           (unsigned)wav_convert_get_progress_percent());
            g_ui_settings.status_until_ms = HAL_GetTick() + UI_SETTINGS_STATUS_DURATION_MS;
            return;
        }
        if (convert_state == WAV_CONVERT_STATE_DONE)
        {
            const uint8_t slot = g_ui_settings.convert_slot;
            char path[SAMPLE_POOL_PATH_MAX];
            (void)snprintf(path, sizeof(path), "%s", g_ui_settings.convert_path);
            g_ui_settings.convert_slot_valid = 0U;
            g_ui_settings.convert_path[0] = '\0';
            wav_convert_clear_finished();
            wav_loader_catalog_mark_stale();
            ui_page_settings_sample_load_to_slot(slot, path);
            return;
        }
        if (convert_state == WAV_CONVERT_STATE_FAILED)
        {
            const char *const label =
                ui_page_settings_convert_error_label(wav_convert_get_last_error());
            g_ui_settings.convert_slot_valid = 0U;
            g_ui_settings.convert_path[0] = '\0';
            wav_convert_clear_finished();
            ui_page_settings_status(label);
            return;
        }
    }

    ui_page_settings_preview_apply_termination_status();

    if ((g_ui_settings.status_line[0] != '\0')
        && ((int32_t)(g_ui_settings.status_until_ms - HAL_GetTick()) <= 0))
    {
        ui_page_settings_status(0);
    }
}

static void ui_page_settings_copy_bounded(char *out, uint32_t out_size, const char *src)
{
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';
    if (src == 0)
    {
        return;
    }

    uint32_t len = (uint32_t)strlen(src);
    if (len >= out_size)
    {
        len = out_size - 1U;
    }

    (void)memcpy(out, src, len);
    out[len] = '\0';
}

static void ui_page_settings_build_middle_ellipsis(char *out,
                                                   uint32_t out_size,
                                                   const char *src,
                                                   uint32_t keep_chars)
{
    static const char k_ellipsis[] = "~";

    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';
    if ((src == 0) || (keep_chars == 0U))
    {
        return;
    }

    const uint32_t src_len = (uint32_t)strlen(src);
    if (src_len <= keep_chars)
    {
        ui_page_settings_copy_bounded(out, out_size, src);
        return;
    }

    if (keep_chars + 2U > out_size)
    {
        keep_chars = (out_size > 2U) ? (out_size - 2U) : 0U;
    }

    const uint32_t prefix_len = (keep_chars + 1U) / 2U;
    const uint32_t suffix_len = keep_chars - prefix_len;
    uint32_t pos = 0U;

    if (prefix_len != 0U)
    {
        (void)memcpy(&out[pos], src, prefix_len);
        pos += prefix_len;
    }

    (void)memcpy(&out[pos], k_ellipsis, 1U);
    pos += 1U;

    if (suffix_len != 0U)
    {
        (void)memcpy(&out[pos], &src[src_len - suffix_len], suffix_len);
        pos += suffix_len;
    }

    out[pos] = '\0';
}

static void ui_page_settings_fit_label(char *out, uint32_t out_size, const char *src, uint8_t max_px)
{
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';
    if (src == 0)
    {
        return;
    }

    ui_page_settings_copy_bounded(out, out_size, src);
    if (drv_display_text_width(out) <= max_px)
    {
        return;
    }

    const uint32_t src_len = (uint32_t)strlen(src);
    uint32_t max_keep = (out_size > 2U) ? (out_size - 2U) : 0U;
    if (max_keep > src_len)
    {
        max_keep = src_len;
    }

    for (uint32_t keep = max_keep; keep > 0U; --keep)
    {
        ui_page_settings_build_middle_ellipsis(out, out_size, src, keep);
        if (drv_display_text_width(out) <= max_px)
        {
            return;
        }
    }

    ui_page_settings_copy_bounded(out, out_size, "~");
    while ((out[0] != '\0') && (drv_display_text_width(out) > max_px))
    {
        const uint32_t len = (uint32_t)strlen(out);
        out[len - 1U] = '\0';
    }
}

static void ui_page_settings_sample_slot_label(uint8_t slot, char *out, uint32_t out_size, uint8_t max_px)
{
    char sample_name[SAMPLE_POOL_PATH_MAX];
    static const char k_empty[] = "--";
    static const char k_loaded[] = "LOADED";
    static const char k_prep[] = "PREP";
    static const char k_error[] = "ERROR";
    static const char k_missing[] = "MISS";

    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';
    if (out_size < 4U)
    {
        return;
    }

    const sample_pool_slot_state_t state = sample_pool_get_state(slot);
    out[0] = (char)('0' + (((slot + 1U) / 10U) % 10U));
    out[1] = (char)('0' + ((slot + 1U) % 10U));
    out[2] = ' ';
    out[3] = '\0';

    const uint8_t prefix_px = drv_display_text_width(out);
    const uint8_t name_px = (max_px > prefix_px) ? (uint8_t)(max_px - prefix_px) : 0U;

    if (state == SAMPLE_POOL_SLOT_EMPTY)
    {
        ui_page_settings_fit_label(&out[3], (out_size > 3U) ? (out_size - 3U) : 0U, k_empty, name_px);
        return;
    }

    const sample_desc_t *const desc = sample_pool_get(slot);
    const char *name = (desc != 0) ? strrchr(desc->path, '/') : 0;
    name = (name != 0) ? (name + 1) : ((desc != 0) ? desc->path : "");

    ui_page_settings_make_sample_label(sample_name, sizeof(sample_name), name, 0U);
    if (sample_name[0] == '\0')
    {
        const char *state_label = (state == SAMPLE_POOL_SLOT_LOADED) ? k_loaded
                                 : (state == SAMPLE_POOL_SLOT_PREPARING) ? k_prep
                                 : (state == SAMPLE_POOL_SLOT_ERROR) ? k_error
                                 : k_missing;
        ui_page_settings_fit_label(&out[3], (out_size > 3U) ? (out_size - 3U) : 0U, state_label, name_px);
        return;
    }

    ui_page_settings_fit_label(&out[3], (out_size > 3U) ? (out_size - 3U) : 0U, sample_name, name_px);
}

static void ui_page_settings_multi_left_label(const ui_settings_multi_entry_t *entry,
                                              char *out,
                                              uint32_t out_size,
                                              uint8_t max_px)
{
    char raw[48];
    if ((entry == 0) || (out == 0) || (out_size == 0U))
    {
        return;
    }

    if (entry->prepared != 0U)
    {
        (void)snprintf(raw, sizeof(raw), "%s %03u", entry->label, (unsigned)entry->sample_count);
    }
    else
    {
        (void)snprintf(raw, sizeof(raw), "%s NEW", entry->label);
    }
    ui_page_settings_fit_label(out, out_size, raw, max_px);
}

static void ui_page_settings_multi_slot_label(uint8_t slot, char *out, uint32_t out_size, uint8_t max_px)
{
    char raw[48];
    const multi_sample_instrument_t *const instrument = multi_sample_pool_get_instrument(slot);
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    if (instrument == 0)
    {
        (void)snprintf(raw, sizeof(raw), "M%02u ---", (unsigned)(slot + 1U));
    }
    else
    {
        (void)snprintf(raw,
                       sizeof(raw),
                       "M%02u %s %03u",
                       (unsigned)(slot + 1U),
                       instrument->name,
                       (unsigned)instrument->sample_count);
    }
    ui_page_settings_fit_label(out, out_size, raw, max_px);
}

static uint16_t ui_page_settings_clamp_scroll(uint16_t scroll,
                                              uint16_t selected,
                                              uint16_t count,
                                              uint8_t visible_lines)
{
    if ((count == 0U) || (count <= visible_lines))
    {
        return 0U;
    }

    const uint16_t max_start = (uint16_t)(count - visible_lines);
    if (scroll > max_start)
    {
        scroll = max_start;
    }
    if (selected < scroll)
    {
        scroll = selected;
    }
    else if (selected >= (uint16_t)(scroll + visible_lines))
    {
        scroll = (uint16_t)(selected - (visible_lines - 1U));
    }

    return scroll;
}

static void ui_page_settings_draw_centered_label(uint8_t x, uint8_t w, uint8_t y, const char *label)
{
    if ((label == 0) || (w == 0U))
    {
        return;
    }

    const uint8_t text_w = drv_display_text_width(label);
    const uint8_t text_x = (text_w >= w) ? x : (uint8_t)(x + ((w - text_w) / 2U));
    drv_display_draw_text(text_x, y, label);
}

static void ui_page_settings_draw_page_footer(const char *page4_label)
{
    const uint8_t y = 54U;
    drv_display_set_font(&FONT_4X6);
    ui_page_settings_draw_centered_label(0U, 32U, y, "RETURN");
    ui_page_settings_draw_centered_label(32U, 32U, y, "OK");
    ui_page_settings_draw_centered_label(64U, 32U, y, "-");
    ui_page_settings_draw_centered_label(96U, 32U, y, page4_label);
}

static void ui_page_settings_draw_sample_footer(void)
{
    ui_page_settings_draw_page_footer((button_down(BTN_SHIFT) != 0U) ? "REBUILD" : "REFRESH");
}

static void ui_page_settings_draw_sample_split_position(uint16_t sample_total, uint16_t slot_total)
{
    enum
    {
        UI_SETTINGS_SPLIT_X = 60,
        UI_SETTINGS_SPLIT_Y0 = 10,
        UI_SETTINGS_SPLIT_Y1 = 51,
        UI_SETTINGS_SPLIT_H = UI_SETTINGS_SPLIT_Y1 - UI_SETTINGS_SPLIT_Y0
    };

    drv_display_draw_line(UI_SETTINGS_SPLIT_X, UI_SETTINGS_SPLIT_Y0, UI_SETTINGS_SPLIT_X, UI_SETTINGS_SPLIT_Y1);

    uint16_t selected = 0U;
    uint16_t total = 0U;
    if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
    {
        selected = g_ui_settings.sample_selected;
        total = sample_total;
    }
    else if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS)
    {
        selected = g_ui_settings.sample_slot_selected;
        total = slot_total;
    }
    else
    {
        return;
    }

    if (total <= 1U)
    {
        return;
    }
    if (selected >= total)
    {
        selected = (uint16_t)(total - 1U);
    }

    const uint16_t y =
        (uint16_t)(UI_SETTINGS_SPLIT_Y0
                   + ((((uint32_t)selected * UI_SETTINGS_SPLIT_H) + ((uint32_t)(total - 1U) / 2U))
                      / (uint32_t)(total - 1U)));
    drv_display_draw_line(UI_SETTINGS_SPLIT_X - 3, (int)y, UI_SETTINGS_SPLIT_X + 3, (int)y);
}

static void ui_page_settings_render_multi_browser(void)
{
    enum
    {
        UI_SETTINGS_SAMPLE_LEFT_TEXT_X = 1,
        UI_SETTINGS_SAMPLE_LEFT_TEXT_W = 58,
        UI_SETTINGS_SAMPLE_RIGHT_TEXT_X = 64,
        UI_SETTINGS_SAMPLE_RIGHT_TEXT_W = 62
    };

    char header[32];
    (void)snprintf(header,
                   sizeof(header),
                   "MULTI %u/%u",
                   (unsigned)ui_page_settings_multi_pool_used(),
                   (unsigned)MULTI_SAMPLE_POOL_MAX_SAMPLES);
    drv_display_draw_text(0U, 0U, header);
    drv_display_draw_line(0, 9, 127, 9);

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARE)
    {
        const ui_settings_multi_entry_t *const entry =
            ui_page_settings_multi_find_entry_by_path(g_ui_settings.confirm_path);
        drv_display_set_font(&FONT_4X6);
        drv_display_draw_text(0U, 14U, "PREPARE?");
        if (entry != 0)
        {
            char name_line[32];
            ui_page_settings_fit_label(name_line, sizeof(name_line), entry->label, 124U);
            drv_display_draw_text(0U, 26U, name_line);
        }
        drv_display_draw_text(0U, 38U, "OK=YES");
        drv_display_draw_text(0U, 50U, "RETURN=NO");
        drv_display_set_font(&FONT_5X7);
        return;
    }

    ui_page_settings_draw_sample_split_position(g_ui_settings.multi_entry_count,
                                                MULTI_SAMPLE_POOL_MAX_INSTRUMENTS);
    drv_display_set_font(&FONT_4X6);

    const uint8_t visible_lines = 4U;
    g_ui_settings.sample_left_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_left_scroll,
                                                                     g_ui_settings.sample_selected,
                                                                     g_ui_settings.multi_entry_count,
                                                                     visible_lines);
    g_ui_settings.sample_right_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_right_scroll,
                                                                      g_ui_settings.sample_slot_selected,
                                                                      MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,
                                                                      visible_lines);

    for (uint8_t line = 0U; line < visible_lines; ++line)
    {
        const uint8_t y = (uint8_t)(12U + (line * 10U));
        const uint16_t left_index = (uint16_t)(g_ui_settings.sample_left_scroll + line);
        const uint8_t right_index = (uint8_t)(g_ui_settings.sample_right_scroll + line);

        if (left_index < g_ui_settings.multi_entry_count)
        {
            char left[32];
            ui_page_settings_multi_left_label(&g_ui_settings.multi_entries[left_index],
                                              left,
                                              sizeof(left),
                                              (uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_W);
            if ((left_index == g_ui_settings.sample_selected)
                && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY))
            {
                drv_display_fill_rect(0, y - 1U, 58, 9);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
        }

        if (right_index < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
        {
            char right[32];
            ui_page_settings_multi_slot_label(right_index,
                                              right,
                                              sizeof(right),
                                              (uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_W);
            if ((right_index == g_ui_settings.sample_slot_selected)
                && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS))
            {
                drv_display_fill_rect(62, y - 1U, 66, 9);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
            }
        }
    }

    if (g_ui_settings.status_line[0] != '\0')
    {
        drv_display_draw_text(0U, 54U, g_ui_settings.status_line);
        drv_display_set_font(&FONT_5X7);
        return;
    }

    ui_page_settings_draw_page_footer("-");
    drv_display_set_font(&FONT_5X7);
}

static void ui_page_settings_render_sample_browser(void)
{
    enum
    {
        UI_SETTINGS_SAMPLE_LEFT_TEXT_X = 1,
        UI_SETTINGS_SAMPLE_LEFT_TEXT_W = 58,
        UI_SETTINGS_SAMPLE_RIGHT_TEXT_X = 64,
        UI_SETTINGS_SAMPLE_RIGHT_TEXT_W = 62
    };

    drv_display_draw_text(0U, 0U, "SAMPLES");
    drv_display_draw_line(0, 9, 127, 9);
    ui_page_settings_draw_sample_split_position(g_ui_settings.sample_child_count, SAMPLE_POOL_SIZE);
    drv_display_set_font(&FONT_4X6);

    const uint8_t visible_lines = 4U;
    g_ui_settings.sample_left_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_left_scroll,
                                                                     g_ui_settings.sample_selected,
                                                                     g_ui_settings.sample_child_count,
                                                                     visible_lines);
    g_ui_settings.sample_right_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_right_scroll,
                                                                      g_ui_settings.sample_slot_selected,
                                                                      SAMPLE_POOL_SIZE,
                                                                      visible_lines);
    const uint16_t left_start = g_ui_settings.sample_left_scroll;
    const uint8_t right_start = g_ui_settings.sample_right_scroll;

    for (uint8_t line = 0U; line < visible_lines; ++line)
    {
        const uint8_t y = (uint8_t)(12U + (line * 10U));
        const uint16_t left_index = (uint16_t)(left_start + line);
        const uint8_t right_index = (uint8_t)(right_start + line);

        if ((left_index >= g_ui_settings.sample_page_start)
            && (left_index < (uint16_t)(g_ui_settings.sample_page_start
                                        + g_ui_settings.sample_entry_count)))
        {
            char left[32];
            const ui_settings_sample_entry_t *const entry =
                &g_ui_settings.sample_entries[left_index - g_ui_settings.sample_page_start];
            ui_page_settings_fit_label(left,
                                       sizeof(left),
                                       entry->label,
                                       (uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_W);
            if ((left_index == g_ui_settings.sample_selected)
                && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY))
            {
                drv_display_fill_rect(0, y - 1U, 58, 9);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
        }

        if (right_index < SAMPLE_POOL_SIZE)
        {
            char right[32];
            ui_page_settings_sample_slot_label(right_index,
                                               right,
                                               sizeof(right),
                                               (uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_W);
            if ((right_index == g_ui_settings.sample_slot_selected)
                && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS))
            {
                drv_display_fill_rect(62, y - 1U, 66, 9);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
            }
        }
    }

    if (g_ui_settings.status_line[0] != '\0')
    {
        drv_display_draw_text(0U, 54U, g_ui_settings.status_line);
        drv_display_set_font(&FONT_5X7);
        return;
    }

    ui_page_settings_draw_sample_footer();
    drv_display_set_font(&FONT_5X7);
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

    if (level->view == UI_SETTINGS_VIEW_SAMPLER)
    {
        ui_page_settings_render_sample_browser();
        return;
    }
    if (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
    {
        ui_page_settings_render_multi_browser();
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
        drv_display_draw_text(0U, 42U, slot_line);
        ui_page_settings_draw_page_footer("-");
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
        drv_display_draw_text(0U, 42U, slot_line);
        ui_page_settings_draw_page_footer("-");
    }
    else if (level->view == UI_SETTINGS_VIEW_SAMPLER_CATALOG)
    {
        char slot_line[24];
        (void)snprintf(slot_line,
                       sizeof(slot_line),
                       "CATALOG %u%s",
                       (unsigned)wav_loader_catalog_count(),
                       (wav_loader_catalog_truncated() != 0U) ? "+" : "");
        drv_display_draw_text(0U, 42U, slot_line);
        ui_page_settings_draw_page_footer("-");
    }
    else if (g_ui_settings.status_line[0] == '\0')
    {
        ui_page_settings_draw_page_footer("-");
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
    if ((wav_convert_is_active() != 0U) || (g_ui_settings.convert_slot_valid != 0U))
    {
        return;
    }

    ui_settings_menu_level_t *const level = ui_page_settings_current_level();
    if ((level == 0) || (delta == 0) || (encoder >= UI_SETTINGS_ENCODER_COUNT))
    {
        return;
    }

    if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
        || (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE))
    {
        g_ui_settings.encoder_accum[encoder] = (int16_t)(g_ui_settings.encoder_accum[encoder] + delta);
        const int16_t step = (int16_t)(g_ui_settings.encoder_accum[encoder] / UI_SETTINGS_ENCODER_DIVIDER);
        g_ui_settings.encoder_accum[encoder] = (int16_t)(g_ui_settings.encoder_accum[encoder] - (step * UI_SETTINGS_ENCODER_DIVIDER));
        if (step == 0)
        {
            return;
        }

        if (encoder == 0U)
        {
            g_ui_settings.sample_focus = (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY;
            const uint16_t entry_count = (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
                ? g_ui_settings.multi_entry_count
                : g_ui_settings.sample_child_count;
            if (entry_count == 0U)
            {
                g_ui_settings.sample_selected = 0U;
                return;
            }
            int32_t index = g_ui_settings.sample_selected;
            index += step;
            if (index < 0)
            {
                index = 0;
            }
            else if (index >= entry_count)
            {
                index = (int32_t)entry_count - 1;
            }
            const uint16_t old_selected = g_ui_settings.sample_selected;
            if ((uint16_t)index != old_selected)
            {
                if (level->view == UI_SETTINGS_VIEW_SAMPLER)
                {
                    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
                }
            }
            g_ui_settings.sample_selected = (uint16_t)index;
            if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
                && ((g_ui_settings.sample_selected < g_ui_settings.sample_page_start)
                    || (g_ui_settings.sample_selected >= (uint16_t)(g_ui_settings.sample_page_start
                                                                    + g_ui_settings.sample_entry_count))))
            {
                if (ui_page_settings_sample_browser_refresh() == 0U)
                {
                    g_ui_settings.sample_selected = old_selected;
                    (void)ui_page_settings_sample_browser_refresh();
                }
            }
            return;
        }

        if (encoder == 1U)
        {
            g_ui_settings.sample_focus = (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS;
            int32_t index = g_ui_settings.sample_slot_selected;
            const int32_t slot_count = (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
                ? (int32_t)MULTI_SAMPLE_POOL_MAX_INSTRUMENTS
                : (int32_t)SAMPLE_POOL_SIZE;
            index += step;
            if (index < 0)
            {
                index = 0;
            }
            else if (index >= slot_count)
            {
                index = slot_count - 1;
            }
            if ((uint8_t)index != g_ui_settings.sample_slot_selected)
            {
                if (level->view == UI_SETTINGS_VIEW_SAMPLER)
                {
                    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
                }
            }
            g_ui_settings.sample_slot_selected = (uint8_t)index;
            return;
        }

        if (encoder == 2U)
        {
            return;
        }

        if ((encoder == 3U) && (level->view == UI_SETTINGS_VIEW_SAMPLER))
        {
            int32_t volume = (int32_t)g_ui_settings.sample_preview_volume + ((int32_t)step * 5);
            if (volume < 0)
            {
                volume = 0;
            }
            else if (volume > 100)
            {
                volume = 100;
            }

            g_ui_settings.sample_preview_volume = (uint8_t)volume;
            sd_preview_set_gain((float)g_ui_settings.sample_preview_volume * 0.01f);

            char status[20];
            (void)snprintf(status,
                           sizeof(status),
                           "PVOL %03u",
                           (unsigned int)g_ui_settings.sample_preview_volume);
            ui_page_settings_status(status);
        }
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

