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
#include "Core/brick_build_config.h"
#if BRICK_TEST_BUILD
#include "Core/audio_test_runner.h"
#include "Core/audio_test2.h"
#include "Core/monkey_test.h"
#endif
#include "Core/brick6_sampler_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Sampler/sample_pool.h"
#include "Sampler/sample_page_cache_config.h"
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

#if defined(BRICK6_VARIANT_LOWCOST)
#include "pages/ui_page_calibration.h"
#endif

#if BRICK_TEST_BUILD && defined(BRICK6_VARIANT_LOWCOST)
#include "App/Hall/hall_adc.h"
#include "App/Hall/hall_engine.h"
#endif

typedef enum
{
    UI_SETTINGS_VIEW_ROOT = 0,
    UI_SETTINGS_VIEW_PROJECT,
    UI_SETTINGS_VIEW_SAMPLE,
    UI_SETTINGS_VIEW_SAMPLE_RAM,
    UI_SETTINGS_VIEW_WAVETABLE,
    UI_SETTINGS_VIEW_SAMPLER,
    UI_SETTINGS_VIEW_MULTI_SAMPLE,
    UI_SETTINGS_VIEW_SAMPLER_SLOT,
    UI_SETTINGS_VIEW_SAMPLER_CATALOG,
    UI_SETTINGS_VIEW_PROJECT_LOAD,
    UI_SETTINGS_VIEW_PROJECT_SAVE_AS,
    UI_SETTINGS_VIEW_PROJECT_MANAGE,
    UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT,
#if defined(BRICK6_VARIANT_LOWCOST)
    UI_SETTINGS_VIEW_CALIBRATION,
#endif
#if BRICK_TEST_BUILD
    UI_SETTINGS_VIEW_TEST,
#if defined(BRICK6_VARIANT_LOWCOST)
    UI_SETTINGS_VIEW_TEST_HALL,
#endif
    UI_SETTINGS_VIEW_TEST_AUDIO,
    UI_SETTINGS_VIEW_TEST_AUDIO2,
    UI_SETTINGS_VIEW_TEST_MONKEY,
#endif

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
    UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM = 0,
    UI_SETTINGS_MULTI_ENTRY_NAV_FOLDER,
    UI_SETTINGS_MULTI_ENTRY_EMPTY_FOLDER
} ui_settings_multi_entry_type_t;

typedef enum
{
    UI_SETTINGS_SAMPLE_CONFIRM_NONE = 0,
    UI_SETTINGS_SAMPLE_CONFIRM_REPLACE,
    UI_SETTINGS_SAMPLE_CONFIRM_CLEAR,
    UI_SETTINGS_SAMPLE_CONFIRM_CONVERT,
    UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARE,
    UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARING,
    UI_SETTINGS_SAMPLE_CONFIRM_MULTI_REPLACE,
    UI_SETTINGS_SAMPLE_CONFIRM_MULTI_UNLOAD,
    UI_SETTINGS_SAMPLE_CONFIRM_MULTI_CLEAR_INDEX
} ui_settings_sample_confirm_t;

typedef enum
{
    UI_SETTINGS_PREVIEW_STOP_ORIGIN_NONE = 0,
    UI_SETTINGS_PREVIEW_STOP_ORIGIN_USER,
    UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT
} ui_settings_preview_stop_origin_t;

typedef enum
{
    UI_SETTINGS_MULTI_PREP_PHASE_NONE = 0,
    UI_SETTINGS_MULTI_PREP_PHASE_SCAN,
    UI_SETTINGS_MULTI_PREP_PHASE_COMMIT,
    UI_SETTINGS_MULTI_PREP_PHASE_PREPARE,
    UI_SETTINGS_MULTI_PREP_PHASE_REFRESH
} ui_settings_multi_prepare_phase_t;

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
#define UI_SETTINGS_WAVETABLE_ROOT "0:/WAVETABLES"
#define UI_SETTINGS_SAMPLE_BROWSER_VISIBLE_LINES 4U
#define UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y 7U
#define UI_SETTINGS_SAMPLE_BROWSER_PATH_LABEL_Y 9U
#define UI_SETTINGS_SAMPLE_BROWSER_PATH_LINE_Y 15U
#define UI_SETTINGS_SAMPLE_BROWSER_TEXT_Y0 22U
#define UI_SETTINGS_SAMPLE_BROWSER_TEXT_PITCH 8U
#define UI_SETTINGS_SAMPLE_BROWSER_SELECT_H 8U
#define UI_SETTINGS_MB_BYTES (1024UL * 1024UL)
#define UI_SETTINGS_HEADER_SLOT_X 48U
#define UI_SETTINGS_HEADER_SEP1_X 43U
#define UI_SETTINGS_HEADER_MEM_RIGHT_X 127U
#define UI_SETTINGS_FOOTER_LABEL_Y (OLED_HEIGHT - 6U)
#define UI_SETTINGS_HEADER_FLASH_DURATION_MS 1200U

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
    uint16_t wav_count;
    uint16_t slot_cost;
    uint8_t prepared;
    ui_settings_multi_entry_type_t type;
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
#if BRICK_TEST_BUILD && defined(BRICK6_VARIANT_LOWCOST)
    uint8_t hall_test_key;
    uint8_t hall_test_mux;
#endif
    ui_settings_sampler_catalog_mode_t sampler_catalog_mode;
    uint16_t sample_entry_count;
    uint16_t sample_child_count;
    uint16_t sample_page_start;
    uint16_t multi_entry_count;
    uint16_t sample_selected;
    uint16_t sample_slot_selected;
    uint16_t sample_left_scroll;
    uint16_t sample_right_scroll;
    uint8_t sample_focus;
    uint8_t sample_confirm;
    uint16_t confirm_slot;
    uint8_t sample_preview_volume;
    uint16_t multi_prepare_progress_done;
    uint16_t multi_prepare_progress_total;
    uint8_t multi_prepare_phase;
    uint16_t sample_parent_id;
    uint16_t sampler_slots[SAMPLE_GLOBAL_POOL_FINAL_SLOTS];
    uint16_t sampler_slot_count;
    uint8_t project_slots[PROJECT_V1_SLOT_COUNT];
    uint8_t project_slot_count;
    uint8_t return_page_id;
    uint8_t preview_was_active;
    uint8_t preview_stop_origin;
    uint8_t convert_slot_valid;
    uint16_t convert_slot;
    uint32_t status_until_ms;
    uint32_t header_slot_flash_until_ms;
    uint32_t header_mem_flash_until_ms;
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
static void ui_page_settings_sample_load_to_slot(uint16_t slot, const char *path);
static void ui_page_settings_ram_load_to_slot(uint16_t slot, const char *path);
static void ui_page_settings_wavetable_load_to_slot(uint16_t slot, const char *path);
static void ui_page_settings_sample_copy_left(uint8_t shift_down);
static void ui_page_settings_ram_copy_left(uint8_t shift_down);
static void ui_page_settings_ram_copy_right(uint8_t shift_down);
static void ui_page_settings_wavetable_copy_left(uint8_t shift_down);
static void ui_page_settings_wavetable_copy_right(uint8_t shift_down);
static const ui_settings_sample_entry_t *ui_page_settings_sample_selected_entry(void);
static void ui_page_settings_apply_action(void);
static void ui_page_settings_copy_bounded(char *out, uint32_t out_size, const char *src);
static const char *ui_page_settings_basename(const char *path);
static void ui_page_settings_draw_browser_context_label(const char *tag, const char *label);
static const ui_settings_multi_entry_t *ui_page_settings_multi_find_entry_by_path(const char *path);
static int16_t ui_page_settings_multi_find_loaded_path(const char *index_path);
static uint8_t ui_page_settings_multi_prepare_entry(const ui_settings_multi_entry_t *entry);
static void ui_page_settings_multi_load_entry_to_slot(uint8_t slot, const ui_settings_multi_entry_t *entry);
static void ui_page_settings_multi_confirm_clear_indexes(void);
static void ui_page_settings_multi_prepare_progress_cb(uint16_t done, uint16_t total, void *user);
static void ui_page_settings_multi_prepare_begin(uint8_t slot,
                                                 const char *path,
                                                 ui_settings_multi_prepare_phase_t phase);
static void ui_page_settings_multi_prepare_finish(const char *status);
static void ui_page_settings_multi_prepare_poll(void);
static void ui_page_settings_multi_prepare_flush_progress(void);
static const char *ui_page_settings_multi_load_error_label(multi_sample_load_result_t result);
static void ui_page_settings_flash_sample_header_slots(void);
static void ui_page_settings_flash_sample_header_memory(void);
static uint32_t ui_page_settings_frames_to_prep_bytes(uint32_t frames);
static uint16_t ui_page_settings_global_entry_count_used(void);
static void ui_page_settings_draw_progress_bar(uint8_t x,
                                               uint8_t y,
                                               uint8_t w,
                                               uint8_t h,
                                               uint16_t done,
                                               uint16_t total);
static void ui_page_settings_draw_sample_header(const char *title,
                                                uint16_t used_slots,
                                                uint16_t max_slots,
                                                uint32_t used_bytes,
                                                uint32_t max_bytes);

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

static void ui_page_settings_refresh_global_kind_slots(sample_global_kind_t kind)
{
    uint16_t count = 0U;
    const uint16_t capacity = sample_global_pool_get_active_slot_capacity();
    for (uint16_t i = 0U; i < capacity; ++i)
    {
        const sample_global_slot_t *const slot = sample_global_pool_get_slot(i);
        if ((slot != 0)
            && (slot->kind == kind)
            && (count < SAMPLE_GLOBAL_POOL_FINAL_SLOTS))
        {
            g_ui_settings.sampler_slots[count++] = i;
        }
    }
    g_ui_settings.sampler_slot_count = count;
    if (g_ui_settings.sample_slot_selected >= capacity)
    {
        g_ui_settings.sample_slot_selected = 0U;
    }
    uint8_t selected_visible = 0U;
    for (uint16_t i = 0U; i < count; ++i)
    {
        if (g_ui_settings.sampler_slots[i] == g_ui_settings.sample_slot_selected)
        {
            selected_visible = 1U;
            break;
        }
    }
    if ((selected_visible == 0U) && (count != 0U))
    {
        g_ui_settings.sample_slot_selected = g_ui_settings.sampler_slots[0];
    }
}

static void ui_page_settings_refresh_sampler_slots(void)
{
    ui_page_settings_refresh_global_kind_slots(SAMPLE_GLOBAL_KIND_STREAM);
}

static void ui_page_settings_refresh_ram_slots(void)
{
    ui_page_settings_refresh_global_kind_slots(SAMPLE_GLOBAL_KIND_RAM);
}

static void ui_page_settings_refresh_wavetable_slots(void)
{
    ui_page_settings_refresh_global_kind_slots(SAMPLE_GLOBAL_KIND_WAVETABLE);
}

static void ui_page_settings_sd_busy_status(void)
{
    char status[16];
    (void)snprintf(status, sizeof(status), "SD %s", sd_access_gate_busy_label());
    ui_page_settings_status(status);
}

static uint8_t ui_page_settings_stream_backend_from_global(uint16_t global_slot,
                                                           uint16_t *out_backend_slot)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (out_backend_slot != 0)
    {
        *out_backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }

    const sample_global_slot_t *const slot = sample_global_pool_get_slot(global_slot);
    if ((slot == 0)
        || (slot->kind != SAMPLE_GLOBAL_KIND_STREAM)
        || (sample_global_pool_resolve_backend(global_slot,
                                               SAMPLE_GLOBAL_KIND_STREAM,
                                               &backend_slot) == 0U)
        || (backend_slot >= SAMPLE_POOL_SIZE))
    {
        return 0U;
    }

    if (out_backend_slot != 0)
    {
        *out_backend_slot = backend_slot;
    }
    return 1U;
}

static uint16_t ui_page_settings_filtered_index_for_global(uint16_t global_slot)
{
    for (uint16_t i = 0U; i < g_ui_settings.sampler_slot_count; ++i)
    {
        if (g_ui_settings.sampler_slots[i] == global_slot)
        {
            return i;
        }
    }
    return 0U;
}

static uint8_t ui_page_settings_multi_backend_from_global(uint16_t global_slot,
                                                          uint16_t *out_backend_slot)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (out_backend_slot != 0)
    {
        *out_backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }

    const sample_global_slot_t *const slot = sample_global_pool_get_slot(global_slot);
    if ((slot == 0)
        || (slot->kind != SAMPLE_GLOBAL_KIND_MULTI)
        || (sample_global_pool_resolve_backend(global_slot,
                                               SAMPLE_GLOBAL_KIND_MULTI,
                                               &backend_slot) == 0U)
        || (backend_slot >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
    {
        return 0U;
    }

    if (out_backend_slot != 0)
    {
        *out_backend_slot = backend_slot;
    }
    return 1U;
}

static uint8_t ui_page_settings_ram_backend_from_global(uint16_t global_slot,
                                                        uint16_t *out_backend_slot)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (out_backend_slot != 0)
    {
        *out_backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }

    const sample_global_slot_t *const slot = sample_global_pool_get_slot(global_slot);
    if ((slot == 0)
        || (slot->kind != SAMPLE_GLOBAL_KIND_RAM)
        || (sample_global_pool_resolve_backend(global_slot,
                                               SAMPLE_GLOBAL_KIND_RAM,
                                               &backend_slot) == 0U)
        || (backend_slot >= SAMPLER_RAM_POOL_MAX_SLOTS))
    {
        return 0U;
    }

    if (out_backend_slot != 0)
    {
        *out_backend_slot = backend_slot;
    }
    return 1U;
}

static uint8_t ui_page_settings_wavetable_backend_from_global(uint16_t global_slot,
                                                              uint16_t *out_backend_slot)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (out_backend_slot != 0)
    {
        *out_backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }

    const sample_global_slot_t *const slot = sample_global_pool_get_slot(global_slot);
    if ((slot == 0)
        || (slot->kind != SAMPLE_GLOBAL_KIND_WAVETABLE)
        || (sample_global_pool_resolve_backend(global_slot,
                                               SAMPLE_GLOBAL_KIND_WAVETABLE,
                                               &backend_slot) == 0U)
        || (backend_slot >= WAVETABLE_POOL_MAX_SLOTS))
    {
        return 0U;
    }

    if (out_backend_slot != 0)
    {
        *out_backend_slot = backend_slot;
    }
    return 1U;
}

static int16_t ui_page_settings_stream_find_free_backend_slot(void)
{
    for (uint16_t slot = 0U; slot < SAMPLE_POOL_SIZE; ++slot)
    {
        if (sample_pool_get_state(slot) == SAMPLE_POOL_SLOT_EMPTY)
        {
            return (int16_t)slot;
        }
    }
    return -1;
}

static int16_t ui_page_settings_ram_find_free_backend_slot(void)
{
    const uint16_t slot = sampler_ram_pool_find_free_slot();
    return (slot < SAMPLER_RAM_POOL_MAX_SLOTS) ? (int16_t)slot : -1;
}

static int16_t ui_page_settings_wavetable_find_free_backend_slot(void)
{
    const uint16_t slot = wavetable_pool_find_free_slot();
    return (slot < WAVETABLE_POOL_MAX_SLOTS) ? (int16_t)slot : -1;
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

static uint8_t ui_page_settings_wavetable_ext_is_wav(const char *name)
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

static void ui_page_settings_make_wavetable_label(char *out, uint32_t out_size, const char *name, uint8_t is_dir)
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
    if ((is_dir == 0U) && (len > 4U) && (ui_page_settings_wavetable_ext_is_wav(name) != 0U))
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

static ui_settings_multi_entry_type_t ui_page_settings_multi_classify_folder(const char *folder_path,
                                                                            uint16_t *out_wav_count)
{
    DIR dir;
    FILINFO fno;
    uint8_t has_subdir = 0U;
    uint16_t wav_count = 0U;

    if ((folder_path == 0) || (folder_path[0] == '\0'))
    {
        if (out_wav_count != 0)
        {
            *out_wav_count = 0U;
        }
        return UI_SETTINGS_MULTI_ENTRY_EMPTY_FOLDER;
    }

    if (f_opendir(&dir, folder_path) != FR_OK)
    {
        if (out_wav_count != 0)
        {
            *out_wav_count = 0U;
        }
        return UI_SETTINGS_MULTI_ENTRY_EMPTY_FOLDER;
    }

    while (1)
    {
        memset(&fno, 0, sizeof(fno));
        const FRESULT fr = f_readdir(&dir, &fno);
        if ((fr != FR_OK) || (fno.fname[0] == '\0'))
        {
            break;
        }
        if (fno.fname[0] == '.')
        {
            continue;
        }
        if ((fno.fattrib & AM_DIR) != 0U)
        {
            has_subdir = 1U;
            continue;
        }
        if (ui_page_settings_wav_ext_is_wav(fno.fname) != 0U)
        {
            if (wav_count < UINT16_MAX)
            {
                wav_count++;
            }
        }
    }

    (void)f_closedir(&dir);
    if (out_wav_count != 0)
    {
        *out_wav_count = wav_count;
    }
    if (wav_count != 0U)
    {
        return UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM;
    }
    return (has_subdir != 0U)
        ? UI_SETTINGS_MULTI_ENTRY_NAV_FOLDER
        : UI_SETTINGS_MULTI_ENTRY_EMPTY_FOLDER;
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
            (void)snprintf(prefixed,
                           sizeof(prefixed),
                           "> %.*s",
                           (int)(sizeof(prefixed) - 3U),
                           entry->label);
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

static uint8_t ui_page_settings_wavetable_append_entry(const char *dir,
                                                       const FILINFO *fno,
                                                       uint8_t want_dirs)
{
    if ((dir == 0) || (fno == 0) || (g_ui_settings.sample_entry_count >= UI_SETTINGS_SAMPLE_BROWSER_MAX))
    {
        return 0U;
    }

    const uint8_t is_dir = ((fno->fattrib & AM_DIR) != 0U) ? 1U : 0U;
    if (is_dir != want_dirs)
    {
        return 0U;
    }
    if ((fno->fname[0] == '.')
        || ((is_dir == 0U) && (ui_page_settings_wavetable_ext_is_wav(fno->fname) == 0U)))
    {
        return 0U;
    }

    ui_settings_sample_entry_t *const entry =
        &g_ui_settings.sample_entries[g_ui_settings.sample_entry_count];
    memset(entry, 0, sizeof(*entry));
    if (ui_page_settings_join_path(entry->path, sizeof(entry->path), dir, fno->fname) == 0U)
    {
        return 0U;
    }

    ui_page_settings_make_wavetable_label(entry->label, sizeof(entry->label), fno->fname, is_dir);
    if (is_dir != 0U)
    {
        char prefixed[24];
        (void)snprintf(prefixed,
                       sizeof(prefixed),
                       "> %.*s",
                       (int)(sizeof(prefixed) - 3U),
                       entry->label);
        (void)snprintf(entry->label, sizeof(entry->label), "%s", prefixed);
        entry->type = UI_SETTINGS_SAMPLE_ENTRY_DIR;
    }
    else
    {
        entry->type = UI_SETTINGS_SAMPLE_ENTRY_FILE;
    }
    entry->catalog_index = WAV_LOADER_CATALOG_ROOT_PARENT;
    g_ui_settings.sample_entry_count++;
    return 1U;
}

static uint8_t ui_page_settings_wavetable_scan_pass(DIR *dir, uint8_t want_dirs)
{
    FILINFO fno;
    if (dir == 0)
    {
        return 0U;
    }

    while (g_ui_settings.sample_entry_count < UI_SETTINGS_SAMPLE_BROWSER_MAX)
    {
        memset(&fno, 0, sizeof(fno));
        const FRESULT fr = f_readdir(dir, &fno);
        if ((fr != FR_OK) || (fno.fname[0] == '\0'))
        {
            return (fr == FR_OK) ? 1U : 0U;
        }
        (void)ui_page_settings_wavetable_append_entry(g_ui_settings.sample_dir, &fno, want_dirs);
    }
    return 1U;
}

static uint8_t ui_page_settings_wavetable_browser_refresh(void)
{
    DIR dir;

    g_ui_settings.sample_entry_count = 0U;
    g_ui_settings.sample_page_start = 0U;
    g_ui_settings.sample_child_count = 0U;

    if (strcmp(g_ui_settings.sample_dir, UI_SETTINGS_WAVETABLE_ROOT) != 0)
    {
        ui_settings_sample_entry_t *const entry = &g_ui_settings.sample_entries[g_ui_settings.sample_entry_count++];
        (void)snprintf(entry->path, sizeof(entry->path), "..");
        (void)snprintf(entry->label, sizeof(entry->label), "..");
        entry->catalog_index = WAV_LOADER_CATALOG_ROOT_PARENT;
        entry->type = UI_SETTINGS_SAMPLE_ENTRY_PARENT;
    }

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

    FRESULT fr = f_opendir(&dir, g_ui_settings.sample_dir);
    if (fr != FR_OK)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        ui_page_settings_status("NO WAVETABLES");
        return 0U;
    }

    if (ui_page_settings_wavetable_scan_pass(&dir, 1U) == 0U)
    {
        (void)f_closedir(&dir);
        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        ui_page_settings_status("SD READ FAIL");
        return 0U;
    }
    (void)f_closedir(&dir);

    fr = f_opendir(&dir, g_ui_settings.sample_dir);
    if (fr == FR_OK)
    {
        if (ui_page_settings_wavetable_scan_pass(&dir, 0U) == 0U)
        {
            (void)f_closedir(&dir);
            sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
            ui_page_settings_status("SD READ FAIL");
            return 0U;
        }
        (void)f_closedir(&dir);
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);

    g_ui_settings.sample_child_count = g_ui_settings.sample_entry_count;
    if (g_ui_settings.sample_selected >= g_ui_settings.sample_child_count)
    {
        g_ui_settings.sample_selected = (g_ui_settings.sample_child_count == 0U)
            ? 0U
            : (uint16_t)(g_ui_settings.sample_child_count - 1U);
    }
    if (g_ui_settings.sample_left_scroll >= g_ui_settings.sample_child_count)
    {
        g_ui_settings.sample_left_scroll = 0U;
    }
    if (g_ui_settings.sample_entry_count >= UI_SETTINGS_SAMPLE_BROWSER_MAX)
    {
        ui_page_settings_status("LIST FULL");
    }

    return 1U;
}

static void ui_page_settings_multi_load_metadata(ui_settings_multi_entry_t *entry)
{
    if ((entry == 0) || (entry->type != UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM))
    {
        return;
    }

    entry->prepared = 0U;
    entry->sample_count = 0U;
    entry->zone_count = 0U;
    entry->slot_cost = entry->wav_count;
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
        uint32_t used_bytes = 0U;
        for (uint16_t i = 0U; i < index.sample_count; ++i)
        {
            used_bytes += ui_page_settings_frames_to_prep_bytes(index.samples[i].total_frames);
        }
        const uint32_t slot_bytes = SAMPLE_PAGE_MIN_READY_PAGES * SAMPLE_PAGE_BYTES;
        const uint32_t slots = (slot_bytes == 0U)
            ? 0U
            : ((used_bytes + slot_bytes - 1U) / slot_bytes);
        entry->slot_cost = (slots > UINT16_MAX) ? UINT16_MAX : (uint16_t)slots;
        if (index.instrument_name[0] != '\0')
        {
            ui_page_settings_copy_bounded(entry->label,
                                          sizeof(entry->label),
                                          index.instrument_name);
        }
    }
}

static uint8_t ui_page_settings_multi_entry_is_folder_like(const ui_settings_multi_entry_t *entry)
{
    return ((entry != 0) && (entry->type != UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)) ? 1U : 0U;
}

static void ui_page_settings_multi_sort_folders_first(void)
{
    for (uint16_t i = 1U; i < g_ui_settings.multi_entry_count; ++i)
    {
        ui_settings_multi_entry_t key = g_ui_settings.multi_entries[i];
        if (ui_page_settings_multi_entry_is_folder_like(&key) == 0U)
        {
            continue;
        }

        uint16_t j = i;
        while ((j > 0U)
               && (ui_page_settings_multi_entry_is_folder_like(&g_ui_settings.multi_entries[j - 1U]) == 0U))
        {
            g_ui_settings.multi_entries[j] = g_ui_settings.multi_entries[j - 1U];
            j--;
        }
        g_ui_settings.multi_entries[j] = key;
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

    FRESULT fr = f_opendir(&dir, g_ui_settings.sample_dir);
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
        if (ui_page_settings_join_path(entry->path, sizeof(entry->path), g_ui_settings.sample_dir, fno.fname) == 0U)
        {
            continue;
        }
        entry->type = ui_page_settings_multi_classify_folder(entry->path, &entry->wav_count);
        ui_page_settings_make_sample_label(entry->label, sizeof(entry->label), fno.fname, 1U);
        if (entry->type != UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
        {
            char prefixed[MULTI_SAMPLE_POOL_NAME_MAX];
            (void)snprintf(prefixed,
                           sizeof(prefixed),
                           "> %.*s",
                           (int)(sizeof(prefixed) - 3U),
                           entry->label);
            (void)snprintf(entry->label, sizeof(entry->label), "%s", prefixed);
        }
        (void)ui_page_settings_multi_index_name(entry->path, entry->index_path, sizeof(entry->index_path));
        g_ui_settings.multi_entry_count++;
    }

    (void)f_closedir(&dir);
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);

    ui_page_settings_multi_sort_folders_first();

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
    (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_MULTI_ROOT);
    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_slot_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    g_ui_settings.sample_right_scroll = 0U;
    g_ui_settings.sample_focus = (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY;
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
    g_ui_settings.confirm_slot = 0U;
    g_ui_settings.confirm_path[0] = '\0';
    g_ui_settings.multi_prepare_progress_done = 0U;
    g_ui_settings.multi_prepare_progress_total = 0U;
    g_ui_settings.multi_prepare_phase = (uint8_t)UI_SETTINGS_MULTI_PREP_PHASE_NONE;
    g_ui_settings.header_slot_flash_until_ms = 0U;
    g_ui_settings.header_mem_flash_until_ms = 0U;
    (void)ui_page_settings_multi_browser_refresh();
}

static void ui_page_settings_wavetable_browser_enter_root(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_WAVETABLE_ROOT);
    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_slot_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    g_ui_settings.sample_right_scroll = 0U;
    g_ui_settings.sample_focus = (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY;
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
    g_ui_settings.confirm_slot = 0U;
    g_ui_settings.confirm_path[0] = '\0';
    g_ui_settings.header_slot_flash_until_ms = 0U;
    g_ui_settings.header_mem_flash_until_ms = 0U;
    (void)ui_page_settings_wavetable_browser_refresh();
}

static void ui_page_settings_multi_browser_parent_or_exit(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (strcmp(g_ui_settings.sample_dir, UI_SETTINGS_MULTI_ROOT) == 0)
    {
        ui_page_settings_back();
        return;
    }

    char *slash = strrchr(g_ui_settings.sample_dir, '/');
    if ((slash == 0) || (slash <= (g_ui_settings.sample_dir + strlen(UI_SETTINGS_MULTI_ROOT))))
    {
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_MULTI_ROOT);
    }
    else
    {
        *slash = '\0';
    }

    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    (void)ui_page_settings_multi_browser_refresh();
}

static void ui_page_settings_wavetable_browser_parent_or_exit(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (strcmp(g_ui_settings.sample_dir, UI_SETTINGS_WAVETABLE_ROOT) == 0)
    {
        ui_page_settings_back();
        return;
    }

    char *slash = strrchr(g_ui_settings.sample_dir, '/');
    if ((slash == 0) || (slash <= (g_ui_settings.sample_dir + strlen(UI_SETTINGS_WAVETABLE_ROOT))))
    {
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_WAVETABLE_ROOT);
    }
    else
    {
        *slash = '\0';
    }

    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    (void)ui_page_settings_wavetable_browser_refresh();
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
    if (sample_global_pool_get_used_entries() >= sample_global_pool_get_entry_capacity())
    {
        return -1;
    }
    return ui_page_settings_stream_find_free_backend_slot();
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

static void ui_page_settings_sample_confirm_convert(uint16_t slot, const char *path)
{
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_CONVERT;
    g_ui_settings.confirm_slot = slot;
    (void)snprintf(g_ui_settings.confirm_path, sizeof(g_ui_settings.confirm_path), "%s", path);
    ui_page_settings_status("CONVERT TO 48K ?");
}

static void ui_page_settings_sample_load_to_slot(uint16_t slot, const char *path)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (sample_pool_load(slot, path) != 0U)
    {
        ui_page_settings_refresh_sampler_slots();
        uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        if (sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_STREAM,
                                               slot,
                                               &global_slot) != 0U)
        {
            g_ui_settings.sample_slot_selected = global_slot;
        }
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

static void ui_page_settings_ram_load_to_slot(uint16_t slot, const char *path)
{
    uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    const sampler_ram_result_t result =
        sampler_ram_pool_load_wav(slot, path, &global_slot);
    if (result == SAMPLER_RAM_RESULT_OK)
    {
        ui_page_settings_refresh_ram_slots();
        g_ui_settings.sample_slot_selected = global_slot;
        ui_page_settings_status("LOAD OK");
        return;
    }

    ui_page_settings_refresh_ram_slots();
    if ((result == SAMPLER_RAM_RESULT_GLOBAL_SLOT_FULL)
        || (result == SAMPLER_RAM_RESULT_POOL_FULL))
    {
        ui_page_settings_flash_sample_header_slots();
    }
    else if ((result == SAMPLER_RAM_RESULT_GLOBAL_BUDGET_FULL)
             || (result == SAMPLER_RAM_RESULT_RAM_POOL_FULL)
             || (result == SAMPLER_RAM_RESULT_TOO_LARGE))
    {
        ui_page_settings_flash_sample_header_memory();
    }
    ui_page_settings_status(sampler_ram_pool_result_label(result));
}

static void ui_page_settings_wavetable_load_to_slot(uint16_t slot, const char *path)
{
    uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    const wavetable_result_t result =
        wavetable_pool_load_wav(slot, path, &global_slot);
    if (result == WAVETABLE_RESULT_OK)
    {
        ui_page_settings_refresh_wavetable_slots();
        g_ui_settings.sample_slot_selected = global_slot;
        ui_page_settings_status("LOAD OK");
        return;
    }

    ui_page_settings_refresh_wavetable_slots();
    if ((result == WAVETABLE_RESULT_GLOBAL_SLOT_FULL)
        || (result == WAVETABLE_RESULT_POOL_FULL))
    {
        ui_page_settings_flash_sample_header_slots();
    }
    else if ((result == WAVETABLE_RESULT_GLOBAL_BUDGET_FULL)
             || (result == WAVETABLE_RESULT_RAM_POOL_FULL)
             || (result == WAVETABLE_RESULT_TOO_LARGE))
    {
        ui_page_settings_flash_sample_header_memory();
    }
    ui_page_settings_status(wavetable_pool_result_label(result));
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
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (ui_page_settings_stream_backend_from_global(g_ui_settings.sample_slot_selected,
                                                    &backend_slot) == 0U)
    {
        ui_page_settings_status("NO STREAM");
        return;
    }

    const sample_pool_slot_state_t state = sample_pool_get_state(backend_slot);
    if (state == SAMPLE_POOL_SLOT_EMPTY)
    {
        ui_page_settings_status("SLOT EMPTY");
        return;
    }

    const sample_desc_t *const desc = sample_pool_get(backend_slot);
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

static void ui_page_settings_sample_confirm_replace(uint16_t slot, const char *path)
{
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_REPLACE;
    g_ui_settings.confirm_slot = slot;
    (void)snprintf(g_ui_settings.confirm_path, sizeof(g_ui_settings.confirm_path), "%s", path);
    ui_page_settings_status("OK YES RETURN NO");
}

static void ui_page_settings_sample_request_replace(uint16_t slot, const char *path)
{
    if ((path == 0) || (path[0] == '\0'))
    {
        return;
    }

    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (ui_page_settings_stream_backend_from_global(slot, &backend_slot) == 0U)
    {
        ui_page_settings_status("SELECT STREAM");
        return;
    }

    ui_page_settings_sample_confirm_replace(backend_slot, path);
}

static void ui_page_settings_sample_confirm_clear(uint16_t slot)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if ((ui_page_settings_stream_backend_from_global(slot, &backend_slot) == 0U)
        || (sample_pool_get_state(backend_slot) == SAMPLE_POOL_SLOT_EMPTY))
    {
        ui_page_settings_status("SLOT EMPTY");
        return;
    }

    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_CLEAR;
    g_ui_settings.confirm_slot = backend_slot;
    g_ui_settings.confirm_path[0] = '\0';
    ui_page_settings_status("OK YES RETURN NO");
}

static void ui_page_settings_sample_confirm_accept(void)
{
    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_CONVERT)
    {
        const uint16_t slot = g_ui_settings.confirm_slot;
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
        const uint16_t slot = g_ui_settings.confirm_slot;
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
        const uint16_t slot = g_ui_settings.confirm_slot;
        char path[MULTI_SAMPLE_POOL_PATH_MAX];
        (void)snprintf(path, sizeof(path), "%s", g_ui_settings.confirm_path);
        ui_page_settings_multi_prepare_begin(slot,
                                             path,
                                             UI_SETTINGS_MULTI_PREP_PHASE_SCAN);

        const ui_settings_multi_entry_t *entry = ui_page_settings_multi_find_entry_by_path(path);
        if ((entry == 0) || (ui_page_settings_multi_prepare_entry(entry) == 0U))
        {
            ui_page_settings_multi_prepare_finish(0);
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
        if (entry->prepared == 0U)
        {
            ui_page_settings_multi_prepare_begin(slot,
                                                 path,
                                                 UI_SETTINGS_MULTI_PREP_PHASE_SCAN);
            if (ui_page_settings_multi_prepare_entry(entry) == 0U)
            {
                ui_page_settings_multi_prepare_finish(0);
                return;
            }
        }
        entry = ui_page_settings_multi_find_entry_by_path(path);
        ui_page_settings_multi_load_entry_to_slot(slot, entry);
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_UNLOAD)
    {
        brick6_sampler_runtime_stop_multi_instrument(g_ui_settings.confirm_slot);
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

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_CLEAR_INDEX)
    {
        uint16_t deleted = 0U;
        g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
        g_ui_settings.confirm_path[0] = '\0';

        if ((multi_record_writer_any_active() != 0U)
            || (looper_storage_raw_export_is_active() != 0U)
            || (sample_cache_has_pending_sd_work() != 0U)
            || (multi_sample_load_has_pending() != 0U))
        {
            ui_page_settings_sd_busy_status();
            return;
        }

        for (uint16_t i = 0U; i < g_ui_settings.multi_entry_count; ++i)
        {
            const ui_settings_multi_entry_t *const entry = &g_ui_settings.multi_entries[i];
            if ((entry->type == UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
                && (entry->index_path[0] != '\0')
                && (ui_page_settings_multi_find_loaded_path(entry->index_path) >= 0))
            {
                ui_page_settings_status("UNLOAD FIRST");
                return;
            }
        }

        if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U)
        {
            ui_page_settings_sd_busy_status();
            return;
        }
        if (sd_access_fs_mount_if_needed() == 0U)
        {
            sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
            ui_page_settings_status("SD UNAVAILABLE");
            return;
        }

        for (uint16_t i = 0U; i < g_ui_settings.multi_entry_count; ++i)
        {
            const ui_settings_multi_entry_t *const entry = &g_ui_settings.multi_entries[i];
            if ((entry->type != UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
                || (entry->index_path[0] == '\0'))
            {
                continue;
            }

            const FRESULT fr = f_unlink(entry->index_path);
            if (fr == FR_OK)
            {
                if (deleted < UINT16_MAX)
                {
                    deleted++;
                }
            }
            else if (fr != FR_NO_FILE)
            {
                sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
                (void)ui_page_settings_multi_browser_refresh();
                ui_page_settings_status("CLEAR FAIL");
                return;
            }
        }

        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        (void)ui_page_settings_multi_browser_refresh();
        ui_page_settings_status((deleted != 0U) ? "CLEAR OK" : "NO INDEX");
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

    ui_page_settings_sample_load_to_slot((uint16_t)free_slot, entry->path);
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

static void ui_page_settings_ram_request_replace(uint16_t global_slot, const char *path)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if ((path == 0) || (path[0] == '\0'))
    {
        return;
    }
    if (ui_page_settings_ram_backend_from_global(global_slot, &backend_slot) == 0U)
    {
        ui_page_settings_status("SELECT RAM");
        return;
    }
    ui_page_settings_ram_load_to_slot(backend_slot, path);
}

static void ui_page_settings_ram_copy_left(uint8_t shift_down)
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
        ui_page_settings_ram_request_replace(g_ui_settings.sample_slot_selected, entry->path);
        return;
    }

    if (sample_global_pool_get_used_entries() >= sample_global_pool_get_entry_capacity())
    {
        ui_page_settings_flash_sample_header_slots();
        ui_page_settings_status("POOL FULL");
        return;
    }

    const int16_t free_slot = ui_page_settings_ram_find_free_backend_slot();
    if (free_slot < 0)
    {
        ui_page_settings_status("RAM FULL");
        return;
    }

    ui_page_settings_ram_load_to_slot((uint16_t)free_slot, entry->path);
}

static void ui_page_settings_ram_copy_right(uint8_t shift_down)
{
    if (shift_down != 0U)
    {
        const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
        if ((entry == 0) || (entry->type != UI_SETTINGS_SAMPLE_ENTRY_FILE))
        {
            ui_page_settings_status("SELECT WAV");
            return;
        }
        ui_page_settings_ram_request_replace(g_ui_settings.sample_slot_selected, entry->path);
        return;
    }

    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (ui_page_settings_ram_backend_from_global(g_ui_settings.sample_slot_selected,
                                                 &backend_slot) == 0U)
    {
        ui_page_settings_status("SELECT RAM");
        return;
    }
    sampler_ram_pool_clear(backend_slot);
    ui_page_settings_refresh_ram_slots();
    ui_page_settings_status("CLEAR OK");
}

static void ui_page_settings_wavetable_request_replace(uint16_t global_slot, const char *path)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if ((path == 0) || (path[0] == '\0'))
    {
        return;
    }
    if (ui_page_settings_wavetable_backend_from_global(global_slot, &backend_slot) == 0U)
    {
        ui_page_settings_status("SELECT WAVE");
        return;
    }
    ui_page_settings_wavetable_load_to_slot(backend_slot, path);
}

static void ui_page_settings_wavetable_copy_left(uint8_t shift_down)
{
    const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
    if (entry == 0)
    {
        ui_page_settings_status("NO WAV");
        return;
    }

    if (entry->type == UI_SETTINGS_SAMPLE_ENTRY_PARENT)
    {
        ui_page_settings_wavetable_browser_parent_or_exit();
        return;
    }

    if (entry->type == UI_SETTINGS_SAMPLE_ENTRY_DIR)
    {
        char restore_dir[WAV_LOADER_CATALOG_PATH_MAX];
        (void)snprintf(restore_dir, sizeof(restore_dir), "%s", g_ui_settings.sample_dir);
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", entry->path);
        g_ui_settings.sample_selected = 0U;
        g_ui_settings.sample_left_scroll = 0U;
        if (ui_page_settings_wavetable_browser_refresh() == 0U)
        {
            (void)snprintf(g_ui_settings.sample_dir,
                           sizeof(g_ui_settings.sample_dir),
                           "%s",
                           restore_dir);
            (void)ui_page_settings_wavetable_browser_refresh();
        }
        return;
    }

    if (shift_down != 0U)
    {
        ui_page_settings_wavetable_request_replace(g_ui_settings.sample_slot_selected, entry->path);
        return;
    }

    if (sample_global_pool_get_used_entries() >= sample_global_pool_get_entry_capacity())
    {
        ui_page_settings_flash_sample_header_slots();
        ui_page_settings_status("POOL FULL");
        return;
    }

    const int16_t free_slot = ui_page_settings_wavetable_find_free_backend_slot();
    if (free_slot < 0)
    {
        ui_page_settings_status("WAVE FULL");
        return;
    }

    ui_page_settings_wavetable_load_to_slot((uint16_t)free_slot, entry->path);
}

static void ui_page_settings_wavetable_copy_right(uint8_t shift_down)
{
    if (shift_down != 0U)
    {
        const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
        if ((entry == 0) || (entry->type != UI_SETTINGS_SAMPLE_ENTRY_FILE))
        {
            ui_page_settings_status("SELECT WAV");
            return;
        }
        ui_page_settings_wavetable_request_replace(g_ui_settings.sample_slot_selected, entry->path);
        return;
    }

    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (ui_page_settings_wavetable_backend_from_global(g_ui_settings.sample_slot_selected,
                                                       &backend_slot) == 0U)
    {
        ui_page_settings_status("SELECT WAVE");
        return;
    }
    wavetable_pool_clear(backend_slot);
    ui_page_settings_refresh_wavetable_slots();
    ui_page_settings_status("CLEAR OK");
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

static uint16_t ui_page_settings_multi_required_slots(const ui_settings_multi_entry_t *entry)
{
    if (entry == 0)
    {
        return 0U;
    }
    return (entry->slot_cost != 0U) ? entry->slot_cost : entry->wav_count;
}

static uint32_t ui_page_settings_multi_required_cost_bytes(const ui_settings_multi_entry_t *entry)
{
    const uint32_t slot_bytes = SAMPLE_PAGE_MIN_READY_PAGES * SAMPLE_PAGE_BYTES;
    return (uint32_t)ui_page_settings_multi_required_slots(entry) * slot_bytes;
}

static uint8_t ui_page_settings_multi_has_slot_capacity(const ui_settings_multi_entry_t *entry,
                                                        uint8_t target_slot)
{
    return sample_global_pool_validate_budget(SAMPLE_GLOBAL_KIND_MULTI,
                                              target_slot,
                                              ui_page_settings_multi_required_cost_bytes(entry));
}

static void ui_page_settings_flash_sample_header_slots(void)
{
    g_ui_settings.header_slot_flash_until_ms =
        HAL_GetTick() + UI_SETTINGS_HEADER_FLASH_DURATION_MS;
}

static void ui_page_settings_flash_sample_header_memory(void)
{
    g_ui_settings.header_mem_flash_until_ms =
        HAL_GetTick() + UI_SETTINGS_HEADER_FLASH_DURATION_MS;
}

static void ui_page_settings_multi_prepare_progress_cb(uint16_t done, uint16_t total, void *user)
{
    (void)user;
    g_ui_settings.multi_prepare_progress_done = done;
    g_ui_settings.multi_prepare_progress_total = (total == 0U) ? 1U : total;
    ui_page_settings_multi_prepare_flush_progress();
}

static void ui_page_settings_multi_prepare_begin(uint8_t slot,
                                                 const char *path,
                                                 ui_settings_multi_prepare_phase_t phase)
{
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARING;
    g_ui_settings.confirm_slot = slot;
    if (path != 0)
    {
        ui_page_settings_copy_bounded(g_ui_settings.confirm_path,
                                      sizeof(g_ui_settings.confirm_path),
                                      path);
    }
    g_ui_settings.multi_prepare_phase = (uint8_t)phase;
    g_ui_settings.multi_prepare_progress_done = 0U;
    g_ui_settings.multi_prepare_progress_total = 1U;
    g_ui_settings.status_line[0] = '\0';
    ui_page_settings_multi_prepare_flush_progress();
}

static void ui_page_settings_multi_prepare_finish(const char *status)
{
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
    g_ui_settings.confirm_path[0] = '\0';
    g_ui_settings.multi_prepare_phase = (uint8_t)UI_SETTINGS_MULTI_PREP_PHASE_NONE;
    g_ui_settings.multi_prepare_progress_done = 0U;
    g_ui_settings.multi_prepare_progress_total = 0U;
    if (status != 0)
    {
        ui_page_settings_status(status);
    }
}

static void ui_page_settings_multi_prepare_poll(void)
{
    if (g_ui_settings.sample_confirm != (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARING)
    {
        return;
    }

    const ui_settings_multi_prepare_phase_t phase =
        (ui_settings_multi_prepare_phase_t)g_ui_settings.multi_prepare_phase;
    if ((phase == UI_SETTINGS_MULTI_PREP_PHASE_SCAN)
        || (phase == UI_SETTINGS_MULTI_PREP_PHASE_COMMIT))
    {
        return;
    }

    multi_sample_load_diag_t diag;
    multi_sample_get_load_diag(&diag);
    const multi_sample_instrument_state_t state =
        multi_sample_pool_get_state(g_ui_settings.confirm_slot);

    if (state == MULTI_SAMPLE_INSTRUMENT_LOADING)
    {
        g_ui_settings.multi_prepare_phase = (uint8_t)UI_SETTINGS_MULTI_PREP_PHASE_PREPARE;
        g_ui_settings.multi_prepare_progress_done = diag.pages_ready;
        g_ui_settings.multi_prepare_progress_total =
            (diag.pages_requested == 0U) ? 1U : diag.pages_requested;
        return;
    }

    if (state == MULTI_SAMPLE_INSTRUMENT_READY)
    {
        g_ui_settings.multi_prepare_phase = (uint8_t)UI_SETTINGS_MULTI_PREP_PHASE_REFRESH;
        g_ui_settings.multi_prepare_progress_done = 1U;
        g_ui_settings.multi_prepare_progress_total = 1U;
        ui_page_settings_refresh_global_kind_slots(SAMPLE_GLOBAL_KIND_MULTI);
        uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        if (sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_MULTI,
                                               g_ui_settings.confirm_slot,
                                               &global_slot) != 0U)
        {
            g_ui_settings.sample_slot_selected = global_slot;
        }
        (void)ui_page_settings_multi_browser_refresh();
        ui_page_settings_multi_prepare_finish("LOAD OK");
        return;
    }

    if (state == MULTI_SAMPLE_INSTRUMENT_ERROR)
    {
        ui_page_settings_multi_prepare_finish(ui_page_settings_multi_load_error_label(diag.last_error));
    }
}

static uint32_t ui_page_settings_frames_to_prep_bytes(uint32_t frames)
{
    if (frames == 0U)
    {
        return 0U;
    }

    const uint32_t prep_frames = (frames < SAMPLE_PREP_MIN_READY_FRAMES)
        ? frames
        : SAMPLE_PREP_MIN_READY_FRAMES;
    const uint32_t pages = (prep_frames + SAMPLE_PAGE_FRAMES - 1U) / SAMPLE_PAGE_FRAMES;
    return pages * SAMPLE_PAGE_BYTES;
}

static uint32_t ui_page_settings_global_memory_used_bytes(void)
{
    return sample_global_pool_get_used_bytes();
}

static uint16_t ui_page_settings_global_entry_count_used(void)
{
    return sample_global_pool_get_used_entries();
}

static void ui_page_settings_draw_global_sample_header(const char *title)
{
    ui_page_settings_draw_sample_header(title,
                                        ui_page_settings_global_entry_count_used(),
                                        sample_global_pool_get_entry_capacity(),
                                        ui_page_settings_global_memory_used_bytes(),
                                        SAMPLE_GLOBAL_POOL_BUDGET_BYTES);
}

static void ui_page_settings_format_mb(char *out, uint32_t out_size, uint32_t bytes)
{
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    const uint32_t deci_mb = (uint32_t)(((uint64_t)bytes * 10ULL
                                         + (UI_SETTINGS_MB_BYTES / 2U))
                                        / UI_SETTINGS_MB_BYTES);
    (void)snprintf(out,
                   out_size,
                   "%lu.%lu",
                   (unsigned long)(deci_mb / 10U),
                   (unsigned long)(deci_mb % 10U));
}

static void ui_page_settings_draw_sample_header(const char *title,
                                                uint16_t used_slots,
                                                uint16_t max_slots,
                                                uint32_t used_bytes,
                                                uint32_t max_bytes)
{
    char used_mb[8];
    char max_mb[8];
    char slots[12];
    char mem[20];

    ui_page_settings_format_mb(used_mb, sizeof(used_mb), used_bytes);
    ui_page_settings_format_mb(max_mb, sizeof(max_mb), max_bytes);
    (void)snprintf(slots,
                   sizeof(slots),
                   "%u/%u",
                   (unsigned)used_slots,
                   (unsigned)max_slots);
    (void)snprintf(mem,
                   sizeof(mem),
                   "%s/%sMB",
                   used_mb,
                   max_mb);

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text(0U, 0U, title);
    drv_display_draw_line(UI_SETTINGS_HEADER_SEP1_X, 0, UI_SETTINGS_HEADER_SEP1_X, 5);
    const uint8_t slots_w = drv_display_text_width(slots);
    const uint32_t now = HAL_GetTick();
    const uint8_t flash_slots =
        ((int32_t)(g_ui_settings.header_slot_flash_until_ms - now) > 0)
        && (((now / 120U) & 1U) == 0U)
            ? 1U
            : 0U;
    if (flash_slots != 0U)
    {
        drv_display_fill_rect(UI_SETTINGS_HEADER_SLOT_X - 1U, 0, slots_w + 2U, 6);
        drv_display_draw_text_inverted(UI_SETTINGS_HEADER_SLOT_X, 0U, slots);
    }
    else
    {
        drv_display_draw_text(UI_SETTINGS_HEADER_SLOT_X, 0U, slots);
    }
    const uint8_t sep2_x = (uint8_t)(UI_SETTINGS_HEADER_SLOT_X + slots_w + 4U);
    drv_display_draw_line(sep2_x, 0, sep2_x, 5);
    const uint8_t mem_w = drv_display_text_width(mem);
    const uint8_t mem_x = (mem_w >= UI_SETTINGS_HEADER_MEM_RIGHT_X)
        ? 0U
        : (uint8_t)(UI_SETTINGS_HEADER_MEM_RIGHT_X - mem_w);
    const uint8_t flash_mem =
        ((int32_t)(g_ui_settings.header_mem_flash_until_ms - now) > 0)
        && (((now / 120U) & 1U) == 0U)
            ? 1U
            : 0U;
    if (flash_mem != 0U)
    {
        drv_display_fill_rect((mem_x > 0U) ? (uint8_t)(mem_x - 1U) : 0U,
                              0,
                              mem_w + 2U,
                              6);
        drv_display_draw_text_inverted(mem_x, 0U, mem);
    }
    else
    {
        drv_display_draw_text(mem_x, 0U, mem);
    }
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

    const multi_sample_import_result_t result =
        multi_sample_import_folder_with_progress(entry->path,
                                                 ui_page_settings_multi_prepare_progress_cb,
                                                 0);
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
    if ((entry == 0) || (entry->type != UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM))
    {
        return;
    }
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARE;
    g_ui_settings.confirm_slot = slot;
    ui_page_settings_copy_bounded(g_ui_settings.confirm_path,
                                  sizeof(g_ui_settings.confirm_path),
                                  entry->path);
    g_ui_settings.status_line[0] = '\0';
}

static void ui_page_settings_multi_confirm_replace(uint8_t slot)
{
    const ui_settings_multi_entry_t *const entry = ui_page_settings_multi_selected_entry();
    if (entry == 0)
    {
        return;
    }
    if (ui_page_settings_multi_has_slot_capacity(entry, slot) == 0U)
    {
        ui_page_settings_flash_sample_header_slots();
        ui_page_settings_status("POOL FULL");
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

static void ui_page_settings_multi_confirm_clear_indexes(void)
{
    if (g_ui_settings.multi_entry_count == 0U)
    {
        ui_page_settings_status("NO MULTI");
        return;
    }

    uint8_t has_index_target = 0U;
    for (uint16_t i = 0U; i < g_ui_settings.multi_entry_count; ++i)
    {
        const ui_settings_multi_entry_t *const entry = &g_ui_settings.multi_entries[i];
        if ((entry->type == UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
            && (entry->index_path[0] != '\0'))
        {
            has_index_target = 1U;
            break;
        }
    }

    if (has_index_target == 0U)
    {
        ui_page_settings_status("NO MULTI");
        return;
    }

    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_CLEAR_INDEX;
    g_ui_settings.confirm_slot = 0U;
    ui_page_settings_copy_bounded(g_ui_settings.confirm_path,
                                  sizeof(g_ui_settings.confirm_path),
                                  g_ui_settings.sample_dir);
    ui_page_settings_status("OK=YES RETURN=NO");
}

static void ui_page_settings_multi_load_entry_to_slot(uint8_t slot, const ui_settings_multi_entry_t *entry)
{
    const uint8_t already_blocking =
        (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARING) ? 1U : 0U;

    if ((entry == 0)
        || (entry->type != UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
        || (entry->index_path[0] == '\0'))
    {
        ui_page_settings_status("NO INDEX");
        return;
    }

    const int16_t existing = ui_page_settings_multi_find_loaded_path(entry->index_path);
    if (existing >= 0)
    {
        uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        if (sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_MULTI,
                                               (uint16_t)existing,
                                               &global_slot) != 0U)
        {
            g_ui_settings.sample_slot_selected = global_slot;
        }
        (void)ui_page_settings_multi_assign_active_track((uint16_t)existing, entry->index_path);
        ui_page_settings_status("REUSED");
        return;
    }

    if (ui_page_settings_multi_has_slot_capacity(entry, slot) == 0U)
    {
        char status[24];
        ui_page_settings_copy_bounded(status, sizeof(status), "FULL need ");
        ui_page_settings_append_u16(status,
                                    sizeof(status),
                                    ui_page_settings_multi_required_slots(entry));
        ui_page_settings_flash_sample_header_slots();
        ui_page_settings_status(status);
        return;
    }

    if (already_blocking == 0U)
    {
        ui_page_settings_multi_prepare_begin(slot,
                                             entry->path,
                                             UI_SETTINGS_MULTI_PREP_PHASE_COMMIT);
    }
    else
    {
        g_ui_settings.confirm_slot = slot;
        g_ui_settings.multi_prepare_phase = (uint8_t)UI_SETTINGS_MULTI_PREP_PHASE_COMMIT;
        g_ui_settings.multi_prepare_progress_done = 0U;
        g_ui_settings.multi_prepare_progress_total = 1U;
        ui_page_settings_multi_prepare_flush_progress();
    }

    if (multi_sample_pool_get_state(slot) != MULTI_SAMPLE_INSTRUMENT_EMPTY)
    {
        brick6_sampler_runtime_stop_multi_instrument(slot);
        (void)multi_sample_pool_clear_instrument(slot);
    }

    const multi_sample_load_result_t result = multi_sample_load_instrument(entry->index_path, slot);
    if ((result == MULTI_SAMPLE_LOAD_OK) || (result == MULTI_SAMPLE_LOAD_ALREADY_READY))
    {
        (void)multi_sample_pool_set_index_path(slot, entry->index_path);
        (void)ui_page_settings_multi_assign_active_track(slot, entry->index_path);
        g_ui_settings.multi_prepare_phase = (uint8_t)UI_SETTINGS_MULTI_PREP_PHASE_PREPARE;
        g_ui_settings.multi_prepare_progress_done = 0U;
        g_ui_settings.multi_prepare_progress_total = 1U;
        ui_page_settings_multi_prepare_poll();
    }
    else
    {
        ui_page_settings_multi_prepare_finish(ui_page_settings_multi_load_error_label(result));
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
    if (entry->type != UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
    {
        ui_page_settings_status("NO MULTI");
        return;
    }

    if (entry->prepared == 0U)
    {
        if (ui_page_settings_multi_has_slot_capacity(entry, slot) == 0U)
        {
            ui_page_settings_flash_sample_header_slots();
            ui_page_settings_status("POOL FULL");
            return;
        }
        ui_page_settings_multi_confirm_prepare(slot);
        return;
    }
    ui_page_settings_multi_load_entry_to_slot(slot, entry);
}

static uint8_t ui_page_settings_multi_enter_selected_folder(const ui_settings_multi_entry_t *entry)
{
    char old_dir[WAV_LOADER_CATALOG_PATH_MAX];

    if ((entry == 0)
        || ((entry->type != UI_SETTINGS_MULTI_ENTRY_NAV_FOLDER)
            && (entry->type != UI_SETTINGS_MULTI_ENTRY_EMPTY_FOLDER)))
    {
        return 0U;
    }

    (void)snprintf(old_dir, sizeof(old_dir), "%s", g_ui_settings.sample_dir);
    (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", entry->path);
    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    if (ui_page_settings_multi_browser_refresh() == 0U)
    {
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", old_dir);
        (void)ui_page_settings_multi_browser_refresh();
        return 0U;
    }
    if (g_ui_settings.multi_entry_count == 0U)
    {
        ui_page_settings_status("EMPTY");
    }
    return 1U;
}

static void ui_page_settings_multi_copy_left(uint8_t shift_down)
{
    const ui_settings_multi_entry_t *const entry = ui_page_settings_multi_selected_entry();
    if (entry == 0)
    {
        ui_page_settings_status("NO MULTI");
        return;
    }

    if (ui_page_settings_multi_enter_selected_folder(entry) != 0U)
    {
        return;
    }

    if (shift_down != 0U)
    {
        uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        if (ui_page_settings_multi_backend_from_global(g_ui_settings.sample_slot_selected,
                                                       &backend_slot) == 0U)
        {
            ui_page_settings_status("SELECT MULTI");
            return;
        }
        if (multi_sample_pool_get_state(backend_slot) != MULTI_SAMPLE_INSTRUMENT_EMPTY)
        {
            ui_page_settings_multi_confirm_replace((uint8_t)backend_slot);
            return;
        }
        ui_page_settings_multi_load_selected_to_slot((uint8_t)backend_slot);
        return;
    }

    const int16_t existing = ui_page_settings_multi_find_loaded_path(entry->index_path);
    if (existing >= 0)
    {
        uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        if (sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_MULTI,
                                               (uint16_t)existing,
                                               &global_slot) != 0U)
        {
            g_ui_settings.sample_slot_selected = global_slot;
        }
        (void)ui_page_settings_multi_assign_active_track((uint16_t)existing, entry->index_path);
        ui_page_settings_status("REUSED");
        return;
    }

    const int16_t free_slot = ui_page_settings_multi_find_free_slot();
    if (free_slot < 0)
    {
        ui_page_settings_flash_sample_header_slots();
        ui_page_settings_status("POOL FULL");
        return;
    }
    ui_page_settings_multi_load_selected_to_slot((uint8_t)free_slot);
}

static void ui_page_settings_multi_copy_right(uint8_t shift_down)
{
    if (shift_down != 0U)
    {
        uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        if (ui_page_settings_multi_backend_from_global(g_ui_settings.sample_slot_selected,
                                                       &backend_slot) == 0U)
        {
            ui_page_settings_status("SELECT MULTI");
            return;
        }
        if (multi_sample_pool_get_state(backend_slot) != MULTI_SAMPLE_INSTRUMENT_EMPTY)
        {
            ui_page_settings_multi_confirm_replace((uint8_t)backend_slot);
            return;
        }
        ui_page_settings_multi_load_selected_to_slot((uint8_t)backend_slot);
        return;
    }

    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (ui_page_settings_multi_backend_from_global(g_ui_settings.sample_slot_selected,
                                                   &backend_slot) == 0U)
    {
        ui_page_settings_status("SELECT MULTI");
        return;
    }
    ui_page_settings_multi_confirm_unload((uint8_t)backend_slot);
}

static const char *ui_page_settings_view_title(ui_settings_view_t view)
{
    switch (view)
    {
        case UI_SETTINGS_VIEW_ROOT:
            return "SETTINGS";
        case UI_SETTINGS_VIEW_PROJECT:
            return "PROJECT";
        case UI_SETTINGS_VIEW_SAMPLE:
            return "SAMPLE";
        case UI_SETTINGS_VIEW_SAMPLE_RAM:
            return "SAMPLE > RAM";
        case UI_SETTINGS_VIEW_WAVETABLE:
            return "SAMPLE > WAVE";
        case UI_SETTINGS_VIEW_SAMPLER:
            return "SAMPLE > STREAM";
        case UI_SETTINGS_VIEW_MULTI_SAMPLE:
            return "SAMPLE > MULTI";
        case UI_SETTINGS_VIEW_SAMPLER_SLOT:
            return "STREAM > SLOT";
        case UI_SETTINGS_VIEW_SAMPLER_CATALOG:
            return "STREAM > SD";
        case UI_SETTINGS_VIEW_PROJECT_LOAD:
            return "PROJECT > LOAD";
        case UI_SETTINGS_VIEW_PROJECT_SAVE_AS:
            return "PROJECT > SAVE AS";
        case UI_SETTINGS_VIEW_PROJECT_MANAGE:
            return "PROJECT > MANAGE";
        case UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT:
            return "MANAGE > SLOT";
#if defined(BRICK6_VARIANT_LOWCOST)
        case UI_SETTINGS_VIEW_CALIBRATION:
            return "CALIBRATION";
#endif
#if BRICK_TEST_BUILD
        case UI_SETTINGS_VIEW_TEST:
            return "TEST";
#if defined(BRICK6_VARIANT_LOWCOST)
        case UI_SETTINGS_VIEW_TEST_HALL:
            return "TEST > HALL";
#endif
        case UI_SETTINGS_VIEW_TEST_AUDIO:
            return "AUDIO TEST";
        case UI_SETTINGS_VIEW_TEST_AUDIO2:
            return "AUDIO TEST 2";
        case UI_SETTINGS_VIEW_TEST_MONKEY:
            return "MONKEY TEST";
#endif
        default:
            return "SETTINGS";
    }
}

static uint8_t ui_page_settings_view_item_count(ui_settings_view_t view)
{
    switch (view)
    {
        case UI_SETTINGS_VIEW_ROOT:
#if defined(BRICK6_VARIANT_LOWCOST) && BRICK_TEST_BUILD
            return 4U;
#elif defined(BRICK6_VARIANT_LOWCOST) || BRICK_TEST_BUILD
            return 3U;
#else
            return 2U;
#endif
        case UI_SETTINGS_VIEW_PROJECT:
            return 3U;
        case UI_SETTINGS_VIEW_SAMPLE:
            return 4U;
        case UI_SETTINGS_VIEW_SAMPLE_RAM:
        case UI_SETTINGS_VIEW_WAVETABLE:
            return 0U;
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
#if defined(BRICK6_VARIANT_LOWCOST)
        case UI_SETTINGS_VIEW_CALIBRATION:
            return 2U;
#endif
#if BRICK_TEST_BUILD
        case UI_SETTINGS_VIEW_TEST:
#if defined(BRICK6_VARIANT_LOWCOST)
            return 4U;
#else
            return 3U;
#endif
#if defined(BRICK6_VARIANT_LOWCOST)
        case UI_SETTINGS_VIEW_TEST_HALL:
            return 0U;
#endif
        case UI_SETTINGS_VIEW_TEST_AUDIO:
        case UI_SETTINGS_VIEW_TEST_AUDIO2:
        case UI_SETTINGS_VIEW_TEST_MONKEY:
            return 0U;
#endif
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
                return "PROJECT";
            }
#if defined(BRICK6_VARIANT_LOWCOST)
            if (index == 2U)
            {
                return "CALIBRATION";
            }
#endif
#if BRICK_TEST_BUILD
            return "TEST";
#else
            return "-";
#endif
        case UI_SETTINGS_VIEW_SAMPLE:
            if (index == 0U)
            {
                return "MULTI";
            }
            if (index == 1U)
            {
                return "RAM";
            }
            if (index == 2U)
            {
                return "WAVE";
            }
            return "STREAM";
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
                uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
                const uint16_t global_slot = g_ui_settings.sampler_slots[index];
                const sample_pool_slot_state_t state =
                    (ui_page_settings_stream_backend_from_global(global_slot, &backend_slot) != 0U)
                        ? sample_pool_get_state(backend_slot)
                        : SAMPLE_POOL_SLOT_ERROR;
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
                (void)snprintf(out, out_size, "SLOT %03u [%s]", (unsigned)global_slot, state_label);
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
#if defined(BRICK6_VARIANT_LOWCOST)
        case UI_SETTINGS_VIEW_CALIBRATION:
            return (index == 0U) ? "HALL KBD" : "HALL VEL";
#endif
#if BRICK_TEST_BUILD
        case UI_SETTINGS_VIEW_TEST:
#if defined(BRICK6_VARIANT_LOWCOST)
            if (index == 0U)
            {
                return "HALL";
            }
            if (index == 1U)
            {
                return "AUDIO";
            }
            return (index == 2U) ? "AUDIO 2" : "MONKEY";
#else
            if (index == 0U)
            {
                return "AUDIO";
            }
            return (index == 1U) ? "AUDIO 2" : "MONKEY";
#endif
#endif
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

void ui_page_settings_close_to_return_page(void)
{
    if (ui_page_settings_is_open() != 0U)
    {
        ui_page_settings_close();
    }
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
#if BRICK_TEST_BUILD
    if ((level != 0) && (level->view == UI_SETTINGS_VIEW_TEST_AUDIO))
    {
        audio_test_runner_cancel();
    }
    if ((level != 0) && (level->view == UI_SETTINGS_VIEW_TEST_AUDIO2))
    {
        audio_test2_cancel();
    }
#endif
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
                ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLE);
            }
            else if (level->selected_index == 1U)
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT);
            }
#if defined(BRICK6_VARIANT_LOWCOST)
            else if (level->selected_index == 2U)
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_CALIBRATION);
            }
#endif
            else
            {
#if BRICK_TEST_BUILD
                ui_page_settings_push(UI_SETTINGS_VIEW_TEST);
#endif
            }
            break;

#if defined(BRICK6_VARIANT_LOWCOST)
        case UI_SETTINGS_VIEW_CALIBRATION:
            if (level->selected_index == 0U)
            {
                ui_page_calibration_open(UI_PAGE_SETTINGS);
            }
            else
            {
                ui_page_user_calibration_open(UI_PAGE_SETTINGS);
            }
            break;
#endif

#if BRICK_TEST_BUILD
        case UI_SETTINGS_VIEW_TEST:
#if defined(BRICK6_VARIANT_LOWCOST)
            if (level->selected_index == 0U)
            {
                g_ui_settings.hall_test_key = 0U;
                g_ui_settings.hall_test_mux = 0U;
                g_ui_settings.encoder_accum[0U] = 0;
                g_ui_settings.encoder_accum[1U] = 0;
                ui_page_settings_push(UI_SETTINGS_VIEW_TEST_HALL);
            }
            else if (level->selected_index == 1U)
            {
                memset(g_ui_settings.encoder_accum, 0, sizeof(g_ui_settings.encoder_accum));
                ui_page_settings_push(UI_SETTINGS_VIEW_TEST_AUDIO);
                (void)audio_test_runner_start();
            }
            else if (level->selected_index == 2U)
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_TEST_AUDIO2);
            }
            else
#else
            if (level->selected_index == 0U)
            {
                memset(g_ui_settings.encoder_accum, 0, sizeof(g_ui_settings.encoder_accum));
                ui_page_settings_push(UI_SETTINGS_VIEW_TEST_AUDIO);
                (void)audio_test_runner_start();
            }
            else if (level->selected_index == 1U)
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_TEST_AUDIO2);
            }
            else
#endif
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_TEST_MONKEY);
            }
            break;
#endif

        case UI_SETTINGS_VIEW_SAMPLE:
            if (level->selected_index == 0U)
            {
                ui_page_settings_refresh_global_kind_slots(SAMPLE_GLOBAL_KIND_MULTI);
                ui_page_settings_multi_browser_enter_root();
                ui_page_settings_push(UI_SETTINGS_VIEW_MULTI_SAMPLE);
            }
            else if (level->selected_index == 1U)
            {
                ui_page_settings_refresh_ram_slots();
                ui_page_settings_sample_browser_enter_root();
                ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLE_RAM);
            }
            else if (level->selected_index == 2U)
            {
                ui_page_settings_refresh_wavetable_slots();
                ui_page_settings_wavetable_browser_enter_root();
                ui_page_settings_push(UI_SETTINGS_VIEW_WAVETABLE);
            }
            else
            {
                ui_page_settings_refresh_sampler_slots();
                ui_page_settings_sample_browser_enter_root();
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
                    g_ui_settings.return_page_id = UI_PAGE_TEMPLATE_CFG;
                    ui_page_set(UI_PAGE_TEMPLATE_CFG);
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

        case UI_SETTINGS_VIEW_SAMPLE_RAM:
            if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
            {
                ui_page_settings_ram_copy_left((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            else
            {
                ui_page_settings_ram_copy_right((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            break;

        case UI_SETTINGS_VIEW_WAVETABLE:
            if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
            {
                ui_page_settings_wavetable_copy_left((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            else
            {
                ui_page_settings_wavetable_copy_right((uint8_t)(button_down(BTN_SHIFT) != 0U));
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
#if BRICK_TEST_BUILD && defined(BRICK6_VARIANT_LOWCOST)
    g_ui_settings.hall_test_key = 0U;
    g_ui_settings.hall_test_mux = 0U;
#endif
    g_ui_settings.sampler_catalog_mode = UI_SETTINGS_SAMPLER_CATALOG_MODE_LOAD;
    g_ui_settings.sampler_slot_count = 0U;
    g_ui_settings.project_slot_count = 0U;
    g_ui_settings.preview_was_active = 0U;
    g_ui_settings.preview_stop_origin = UI_SETTINGS_PREVIEW_STOP_ORIGIN_NONE;
    g_ui_settings.convert_slot_valid = 0U;
    g_ui_settings.convert_slot = 0U;
    g_ui_settings.convert_path[0] = '\0';
    g_ui_settings.header_slot_flash_until_ms = 0U;
    g_ui_settings.header_mem_flash_until_ms = 0U;
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
#if BRICK_TEST_BUILD
    audio_test_runner_cancel();
    audio_test2_cancel();
#endif
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
#if BRICK_TEST_BUILD
    if ((monkey_test_is_active() != 0U) && (level != 0)
        && ((level->view == UI_SETTINGS_VIEW_PROJECT)
            || (level->view == UI_SETTINGS_VIEW_PROJECT_LOAD)
            || (level->view == UI_SETTINGS_VIEW_PROJECT_SAVE_AS)
            || (level->view == UI_SETTINGS_VIEW_PROJECT_MANAGE)
            || (level->view == UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT)
            || (level->view == UI_SETTINGS_VIEW_SAMPLE)
            || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)
            || (level->view == UI_SETTINGS_VIEW_WAVETABLE)
            || (level->view == UI_SETTINGS_VIEW_SAMPLER)
            || (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
            || (level->view == UI_SETTINGS_VIEW_SAMPLER_SLOT)
            || (level->view == UI_SETTINGS_VIEW_SAMPLER_CATALOG)))
    {
        if (ev->id == (uint8_t)BTN_SETTINGS)
        {
            ui_page_settings_back();
        }
        else
        {
            ui_page_settings_status("TEST SAFE");
        }
        return;
    }
    if ((level != 0) && (level->view == UI_SETTINGS_VIEW_TEST_AUDIO))
    {
        if (ev->id == (uint8_t)BTN_PAGE_1)
        {
            audio_test_runner_cancel();
            return;
        }
        if (ev->id == (uint8_t)BTN_SETTINGS)
        {
            audio_test_runner_cancel();
            ui_page_settings_back();
            return;
        }
        return;
    }
    if ((level != 0) && (level->view == UI_SETTINGS_VIEW_TEST_AUDIO2)
        && (ev->source == UI_EVENT_SOURCE_PHYSICAL))
    {
        if (ev->id == (uint8_t)BTN_PAGE_1)
        {
            audio_test2_cancel();
            return;
        }
        if (ev->id == (uint8_t)BTN_PAGE_2)
        {
            audio_test2_view_t view;
            audio_test2_get_view(&view);
            if ((view.state == AUDIO_TEST2_READY) || (view.state == AUDIO_TEST2_ERROR))
            {
                (void)audio_test2_start_internal();
            }
            else if (view.state == AUDIO_TEST2_LINE_READY)
            {
                (void)audio_test2_start_line();
            }
            else if (view.state == AUDIO_TEST2_HEADPHONE_READY)
            {
                (void)audio_test2_start_headphone();
            }
            return;
        }
        if (ev->id == (uint8_t)BTN_PAGE_3)
        {
            (void)audio_test2_start_line();
            return;
        }
        if (ev->id == (uint8_t)BTN_PAGE_4)
        {
            (void)audio_test2_start_headphone();
            return;
        }
        if (ev->id == (uint8_t)BTN_SETTINGS)
        {
            audio_test2_cancel();
            ui_page_settings_back();
            return;
        }
        return;
    }
    if ((level != 0) && (level->view == UI_SETTINGS_VIEW_TEST_MONKEY)
        && (ev->source == UI_EVENT_SOURCE_PHYSICAL))
    {
        if (ev->id == (uint8_t)BTN_PAGE_1)
        {
            if (monkey_test_is_active() != 0U)
            {
                monkey_test_stop();
            }
            else
            {
                monkey_test_start();
            }
            return;
        }
        if (ev->id == (uint8_t)BTN_PAGE_2)
        {
            (void)monkey_test_replay_last_failure();
            return;
        }
        if (ev->id == (uint8_t)BTN_PAGE_3)
        {
            (void)monkey_test_replay_fire_target();
            return;
        }
        if (ev->id == (uint8_t)BTN_SETTINGS)
        {
            monkey_test_stop();
            ui_page_settings_back();
            return;
        }
        return;
    }
#endif

    if ((level != 0)
        && ((level->view == UI_SETTINGS_VIEW_SAMPLER)
            || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)
            || (level->view == UI_SETTINGS_VIEW_WAVETABLE)
            || (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)))
    {
        if (g_ui_settings.sample_confirm != (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE)
        {
            if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARING)
            {
                return;
            }
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
            || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)
            || (level->view == UI_SETTINGS_VIEW_WAVETABLE))
        {
            if (ev->id == (uint8_t)BTN_PAGE_1)
            {
                if (level->view == UI_SETTINGS_VIEW_WAVETABLE)
                {
                    ui_page_settings_wavetable_browser_parent_or_exit();
                }
                else
                {
                    ui_page_settings_sample_browser_parent_or_exit();
                }
                return;
            }
            if ((ev->id == (uint8_t)BTN_PAGE_2)
                && (button_down(BTN_SHIFT) == 0U))
            {
                ui_page_settings_apply_action();
                return;
            }
            if (ev->id == (uint8_t)BTN_PAGE_3)
            {
                return;
            }
            if (ev->id == (uint8_t)BTN_PAGE_4)
            {
                if (level->view == UI_SETTINGS_VIEW_WAVETABLE)
                {
                    (void)ui_page_settings_wavetable_browser_refresh();
                }
                else
                {
                    ui_page_settings_sample_catalog_refresh_action((uint8_t)(button_down(BTN_SHIFT) != 0U));
                }
                return;
            }
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
                ui_page_settings_multi_browser_parent_or_exit();
                return;
            }
            if ((ev->id == (uint8_t)BTN_PAGE_2) && (button_down(BTN_SHIFT) == 0U))
            {
                ui_page_settings_apply_action();
                return;
            }
            if (ev->id == (uint8_t)BTN_PAGE_3)
            {
                ui_page_settings_multi_confirm_clear_indexes();
                return;
            }
            if (ev->id == (uint8_t)BTN_PAGE_4)
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
            const uint16_t slot = g_ui_settings.convert_slot;
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

static void ui_page_settings_sample_slot_label(uint16_t global_slot, char *out, uint32_t out_size, uint8_t max_px)
{
    char sample_name[SAMPLE_POOL_PATH_MAX];
    static const char k_loaded[] = "LOADED";
    static const char k_prep[] = "PREP";
    static const char k_error[] = "ERROR";
    static const char k_missing[] = "MISS";
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;

    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';
    if (out_size < 5U)
    {
        return;
    }

    (void)snprintf(out, out_size, "%03u ", (unsigned)global_slot);

    const uint8_t prefix_px = drv_display_text_width(out);
    const uint8_t name_px = (max_px > prefix_px) ? (uint8_t)(max_px - prefix_px) : 0U;

    if (ui_page_settings_stream_backend_from_global(global_slot, &backend_slot) == 0U)
    {
        ui_page_settings_fit_label(&out[4], (out_size > 4U) ? (out_size - 4U) : 0U, "BADREF", name_px);
        return;
    }

    const sample_pool_slot_state_t state = sample_pool_get_state(backend_slot);
    const sample_desc_t *const desc = sample_pool_get(backend_slot);
    const char *name = (desc != 0) ? strrchr(desc->path, '/') : 0;
    name = (name != 0) ? (name + 1) : ((desc != 0) ? desc->path : "");

    ui_page_settings_make_sample_label(sample_name, sizeof(sample_name), name, 0U);
    if (sample_name[0] == '\0')
    {
        const char *state_label = (state == SAMPLE_POOL_SLOT_LOADED) ? k_loaded
                                 : (state == SAMPLE_POOL_SLOT_PREPARING) ? k_prep
                                 : (state == SAMPLE_POOL_SLOT_ERROR) ? k_error
                                 : k_missing;
        ui_page_settings_fit_label(&out[4], (out_size > 4U) ? (out_size - 4U) : 0U, state_label, name_px);
        return;
    }

    ui_page_settings_fit_label(&out[4], (out_size > 4U) ? (out_size - 4U) : 0U, sample_name, name_px);
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

    if (entry->type == UI_SETTINGS_MULTI_ENTRY_NAV_FOLDER)
    {
        (void)snprintf(raw, sizeof(raw), "%s", entry->label);
    }
    else if (entry->type == UI_SETTINGS_MULTI_ENTRY_EMPTY_FOLDER)
    {
        (void)snprintf(raw, sizeof(raw), "%s EMPTY", entry->label);
    }
    else if (entry->prepared != 0U)
    {
        (void)snprintf(raw, sizeof(raw), "%s %03u", entry->label, (unsigned)entry->sample_count);
    }
    else
    {
        (void)snprintf(raw, sizeof(raw), "%s NEW", entry->label);
    }
    ui_page_settings_fit_label(out, out_size, raw, max_px);
}

static void ui_page_settings_multi_slot_label(uint16_t global_slot, char *out, uint32_t out_size, uint8_t max_px)
{
    char raw[48];
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    const multi_sample_instrument_t *instrument = 0;
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    if (ui_page_settings_multi_backend_from_global(global_slot, &backend_slot) != 0U)
    {
        instrument = multi_sample_pool_get_instrument(backend_slot);
    }

    if (instrument == 0)
    {
        (void)snprintf(raw, sizeof(raw), "%03u BADREF", (unsigned)global_slot);
    }
    else
    {
        (void)snprintf(raw,
                       sizeof(raw),
                       "%03u %s %03u",
                       (unsigned)global_slot,
                       instrument->name,
                       (unsigned)instrument->sample_count);
    }
    ui_page_settings_fit_label(out, out_size, raw, max_px);
}

static void ui_page_settings_ram_slot_label(uint16_t global_slot, char *out, uint32_t out_size, uint8_t max_px)
{
    char raw[48];
    char name_buf[24];
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    const sampler_ram_slot_t *ram = 0;
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    if (ui_page_settings_ram_backend_from_global(global_slot, &backend_slot) != 0U)
    {
        ram = sampler_ram_pool_get_slot(backend_slot);
    }

    if (ram == 0)
    {
        (void)snprintf(raw, sizeof(raw), "%03u BADREF", (unsigned)global_slot);
    }
    else if (ram->state == SAMPLER_RAM_SLOT_READY)
    {
        const char *name = strrchr(ram->path, '/');
        name = (name != 0) ? (name + 1) : ram->path;
        ui_page_settings_make_sample_label(name_buf, sizeof(name_buf), name, 0U);
        (void)snprintf(raw,
                       sizeof(raw),
                       "%03u %s",
                       (unsigned)global_slot,
                       (name_buf[0] != '\0') ? name_buf : "READY");
    }
    else
    {
        const char *state = (ram->state == SAMPLER_RAM_SLOT_LOADING) ? "LOAD"
                          : (ram->state == SAMPLER_RAM_SLOT_ERROR) ? "ERROR"
                          : "EMPTY";
        (void)snprintf(raw, sizeof(raw), "%03u %s", (unsigned)global_slot, state);
    }
    ui_page_settings_fit_label(out, out_size, raw, max_px);
}

static void ui_page_settings_wavetable_slot_label(uint16_t global_slot,
                                                  char *out,
                                                  uint32_t out_size,
                                                  uint8_t max_px)
{
    char raw[48];
    char name_buf[24];
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    const wavetable_slot_t *table = 0;
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    if (ui_page_settings_wavetable_backend_from_global(global_slot, &backend_slot) != 0U)
    {
        table = wavetable_pool_get_slot(backend_slot);
    }

    if (table == 0)
    {
        (void)snprintf(raw, sizeof(raw), "%03u BADREF", (unsigned)global_slot);
    }
    else if (table->state == WAVETABLE_SLOT_READY)
    {
        const char *name = strrchr(table->path, '/');
        name = (name != 0) ? (name + 1) : table->path;
        ui_page_settings_make_wavetable_label(name_buf, sizeof(name_buf), name, 0U);
        (void)snprintf(raw,
                       sizeof(raw),
                       "%03u %s",
                       (unsigned)global_slot,
                       (name_buf[0] != '\0') ? name_buf : "READY");
    }
    else
    {
        const char *state = (table->state == WAVETABLE_SLOT_LOADING) ? "LOAD"
                          : (table->state == WAVETABLE_SLOT_ERROR) ? "ERROR"
                          : "EMPTY";
        (void)snprintf(raw, sizeof(raw), "%03u %s", (unsigned)global_slot, state);
    }
    ui_page_settings_fit_label(out, out_size, raw, max_px);
}

static void ui_page_settings_draw_ram_name_label(void)
{
    char label_buf[24];
    const char *label = ui_page_settings_basename(g_ui_settings.sample_dir);
    const char *tag = "DIR";
    const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();

    if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS)
    {
        uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const sampler_ram_slot_t *const ram =
            (ui_page_settings_ram_backend_from_global(g_ui_settings.sample_slot_selected,
                                                      &backend_slot) != 0U)
                ? sampler_ram_pool_get_slot(backend_slot)
                : 0;
        if ((ram != 0) && (ram->path[0] != '\0'))
        {
            const char *name = strrchr(ram->path, '/');
            name = (name != 0) ? (name + 1) : ram->path;
            ui_page_settings_make_sample_label(label_buf, sizeof(label_buf), name, 0U);
            label = (label_buf[0] != '\0') ? label_buf : "READY";
        }
        else
        {
            label = "EMPTY";
        }
        ui_page_settings_draw_browser_context_label("SLOT", label);
        return;
    }

    if (entry != 0)
    {
        label = entry->label;
        tag = (entry->type == UI_SETTINGS_SAMPLE_ENTRY_FILE) ? "WAV" : "DIR";
    }
    ui_page_settings_draw_browser_context_label(tag, label);
}

static void ui_page_settings_draw_wavetable_name_label(void)
{
    char label_buf[24];
    const char *label = ui_page_settings_basename(g_ui_settings.sample_dir);
    const char *tag = "DIR";
    const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();

    if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS)
    {
        uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const wavetable_slot_t *const table =
            (ui_page_settings_wavetable_backend_from_global(g_ui_settings.sample_slot_selected,
                                                            &backend_slot) != 0U)
                ? wavetable_pool_get_slot(backend_slot)
                : 0;
        if ((table != 0) && (table->path[0] != '\0'))
        {
            const char *name = strrchr(table->path, '/');
            name = (name != 0) ? (name + 1) : table->path;
            ui_page_settings_make_wavetable_label(label_buf, sizeof(label_buf), name, 0U);
            label = (label_buf[0] != '\0') ? label_buf : "READY";
        }
        else
        {
            label = "EMPTY";
        }
        ui_page_settings_draw_browser_context_label("SLOT", label);
        return;
    }

    if (entry != 0)
    {
        label = entry->label;
        tag = (entry->type == UI_SETTINGS_SAMPLE_ENTRY_FILE) ? "WAV" : "DIR";
    }
    ui_page_settings_draw_browser_context_label(tag, label);
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

static void ui_page_settings_draw_progress_bar(uint8_t x,
                                               uint8_t y,
                                               uint8_t w,
                                               uint8_t h,
                                               uint16_t done,
                                               uint16_t total)
{
    if ((w < 3U) || (h < 3U))
    {
        return;
    }

    if (total == 0U)
    {
        total = 1U;
    }
    if (done > total)
    {
        done = total;
    }

    drv_display_draw_rect(x, y, w, h);
    const uint8_t inner_w = (uint8_t)(w - 2U);
    const uint8_t fill_w = (uint8_t)(((uint32_t)inner_w * done) / total);
    if (fill_w != 0U)
    {
        drv_display_fill_rect((int)x + 1, (int)y + 1, fill_w, (int)h - 2);
    }
}

static void ui_page_settings_draw_page_footer_ex(const char *page3_label,
                                                 const char *page4_label)
{
    drv_display_set_font(&FONT_4X6);
    ui_page_settings_draw_centered_label(0U, 32U, UI_SETTINGS_FOOTER_LABEL_Y, "RETURN");
    ui_page_settings_draw_centered_label(32U, 32U, UI_SETTINGS_FOOTER_LABEL_Y, "OK");
    ui_page_settings_draw_centered_label(64U, 32U, UI_SETTINGS_FOOTER_LABEL_Y, page3_label);
    ui_page_settings_draw_centered_label(96U, 32U, UI_SETTINGS_FOOTER_LABEL_Y, page4_label);
}

static void ui_page_settings_draw_page_footer(const char *page4_label)
{
    ui_page_settings_draw_page_footer_ex("-", page4_label);
}

static void ui_page_settings_draw_sample_footer(void)
{
    ui_page_settings_draw_page_footer((button_down(BTN_SHIFT) != 0U) ? "REBUILD" : "REFRESH");
}

static const char *ui_page_settings_basename(const char *path)
{
    if ((path == 0) || (path[0] == '\0'))
    {
        return "-";
    }

    const char *const slash = strrchr(path, '/');
    return ((slash != 0) && (slash[1] != '\0')) ? (slash + 1) : path;
}

static void ui_page_settings_draw_browser_context_label(const char *tag, const char *label)
{
    char line[32];
    char tag_buf[6];

    drv_display_set_font(&FONT_4X6);
    ui_page_settings_fit_label(tag_buf, sizeof(tag_buf), tag, 16U);
    const uint8_t tag_box_w = (uint8_t)(drv_display_text_width(tag_buf) + 4U);
    drv_display_fill_rect(0, UI_SETTINGS_SAMPLE_BROWSER_PATH_LABEL_Y - 1U, tag_box_w, 7);
    drv_display_draw_text_inverted(2U, UI_SETTINGS_SAMPLE_BROWSER_PATH_LABEL_Y, tag_buf);
    ui_page_settings_fit_label(line, sizeof(line), label, 106U);
    drv_display_draw_text((uint8_t)(tag_box_w + 4U), UI_SETTINGS_SAMPLE_BROWSER_PATH_LABEL_Y, line);
    drv_display_draw_line(0,
                          UI_SETTINGS_SAMPLE_BROWSER_PATH_LINE_Y,
                          127,
                          UI_SETTINGS_SAMPLE_BROWSER_PATH_LINE_Y);
}

static void ui_page_settings_draw_stream_name_label(void)
{
    char slot_label[24];
    const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
    const char *label = ui_page_settings_basename(g_ui_settings.sample_dir);
    const char *tag = "DIR";

    if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS)
    {
        uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const sample_desc_t *const desc =
            (ui_page_settings_stream_backend_from_global(g_ui_settings.sample_slot_selected,
                                                         &backend_slot) != 0U)
                ? sample_pool_get(backend_slot)
                : 0;
        const char *name = (desc != 0) ? strrchr(desc->path, '/') : 0;
        name = (name != 0) ? (name + 1) : ((desc != 0) ? desc->path : "");
        ui_page_settings_make_sample_label(slot_label, sizeof(slot_label), name, 0U);
        label = (slot_label[0] != '\0') ? slot_label : "EMPTY";
        tag = "SLOT";
        ui_page_settings_draw_browser_context_label(tag, label);
        return;
    }

    if (entry != 0)
    {
        label = entry->label;
        tag = (entry->type == UI_SETTINGS_SAMPLE_ENTRY_FILE) ? "WAV" : "DIR";
    }
    ui_page_settings_draw_browser_context_label(tag, label);
}

static void ui_page_settings_draw_multi_name_label(void)
{
    const char *label = "NO MULTI";
    const char *tag = "INS";
    const ui_settings_multi_entry_t *const entry = ui_page_settings_multi_selected_entry();

    if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS)
    {
        uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const multi_sample_instrument_t *const instrument =
            (ui_page_settings_multi_backend_from_global(g_ui_settings.sample_slot_selected,
                                                        &backend_slot) != 0U)
                ? multi_sample_pool_get_instrument(backend_slot)
                : 0;
        label = (instrument != 0) ? instrument->name : "EMPTY";
        ui_page_settings_draw_browser_context_label("SLOT", label);
        return;
    }

    if (entry != 0)
    {
        label = entry->label;
        tag = (entry->type == UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM) ? "INS"
            : (entry->type == UI_SETTINGS_MULTI_ENTRY_NAV_FOLDER) ? "DIR"
            : "EMPTY";
    }
    ui_page_settings_draw_browser_context_label(tag, label);
}

static const char *ui_page_settings_multi_prepare_phase_label(void)
{
    switch ((ui_settings_multi_prepare_phase_t)g_ui_settings.multi_prepare_phase)
    {
        case UI_SETTINGS_MULTI_PREP_PHASE_SCAN:
            return "Scan multi";
        case UI_SETTINGS_MULTI_PREP_PHASE_COMMIT:
            return "Commit multi";
        case UI_SETTINGS_MULTI_PREP_PHASE_PREPARE:
            return "Prepare samples";
        case UI_SETTINGS_MULTI_PREP_PHASE_REFRESH:
            return "Refresh multi";
        default:
            return "Preparing multi";
    }
}

static void ui_page_settings_draw_sample_split_position(uint16_t sample_total, uint16_t slot_total)
{
    enum
    {
        UI_SETTINGS_SPLIT_X = 60,
        UI_SETTINGS_SPLIT_Y0 = UI_SETTINGS_SAMPLE_BROWSER_TEXT_Y0 - 1U,
        UI_SETTINGS_SPLIT_Y1 = 51,
        UI_SETTINGS_SPLIT_H = UI_SETTINGS_SPLIT_Y1 - UI_SETTINGS_SPLIT_Y0,
        UI_SETTINGS_SPLIT_CURSOR_MAX_H = 9,
        UI_SETTINGS_SPLIT_CURSOR_MIN_H = 3,
        UI_SETTINGS_SPLIT_CURSOR_REF_ITEMS = 64
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
        selected = ui_page_settings_filtered_index_for_global(g_ui_settings.sample_slot_selected);
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

    const uint16_t ref_total = (total > (uint16_t)UI_SETTINGS_SPLIT_CURSOR_REF_ITEMS)
        ? total
        : (uint16_t)UI_SETTINGS_SPLIT_CURSOR_REF_ITEMS;
    uint8_t cursor_h =
        (uint8_t)(((uint32_t)UI_SETTINGS_SPLIT_CURSOR_MAX_H
                   * UI_SETTINGS_SPLIT_CURSOR_REF_ITEMS
                   + ((uint32_t)ref_total / 2U))
                  / ref_total);
    if (cursor_h > (uint8_t)UI_SETTINGS_SPLIT_CURSOR_MAX_H)
    {
        cursor_h = (uint8_t)UI_SETTINGS_SPLIT_CURSOR_MAX_H;
    }
    if (cursor_h < (uint8_t)UI_SETTINGS_SPLIT_CURSOR_MIN_H)
    {
        cursor_h = (uint8_t)UI_SETTINGS_SPLIT_CURSOR_MIN_H;
    }
    if ((cursor_h & 1U) == 0U)
    {
        cursor_h--;
    }

    const uint16_t travel = (uint16_t)(UI_SETTINGS_SPLIT_H - cursor_h + 1U);
    const uint16_t top =
        (uint16_t)(UI_SETTINGS_SPLIT_Y0
                   + ((((uint32_t)selected * travel) + ((uint32_t)(total - 1U) / 2U))
                      / (uint32_t)(total - 1U)));
    const uint16_t bottom = (uint16_t)(top + cursor_h - 1U);
    const uint16_t mid = (uint16_t)(top + (cursor_h / 2U));
    drv_display_clear_rect(UI_SETTINGS_SPLIT_X - 1, (int)top, 3, cursor_h);
    drv_display_draw_line(UI_SETTINGS_SPLIT_X - 1, (int)top, UI_SETTINGS_SPLIT_X - 1, (int)bottom);
    drv_display_draw_line(UI_SETTINGS_SPLIT_X + 1, (int)top, UI_SETTINGS_SPLIT_X + 1, (int)bottom);
    drv_display_draw_pixel(UI_SETTINGS_SPLIT_X, (int)top, true);
    drv_display_draw_pixel(UI_SETTINGS_SPLIT_X, (int)bottom, true);
    if (cursor_h <= 3U)
    {
        drv_display_draw_pixel(UI_SETTINGS_SPLIT_X, (int)mid, false);
    }
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

    ui_page_settings_multi_prepare_poll();

    ui_page_settings_draw_global_sample_header("MULTI");
    drv_display_draw_line(0, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y, 127, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y);
    ui_page_settings_draw_multi_name_label();

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARE)
    {
        drv_display_set_font(&FONT_5X7);
        ui_page_settings_draw_centered_label(0U, 128U, 34U, "Prepare multi ?");
        ui_page_settings_draw_page_footer("-");
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_CLEAR_INDEX)
    {
        drv_display_set_font(&FONT_5X7);
        ui_page_settings_draw_centered_label(0U, 128U, 30U, "Clear indexes ?");
        drv_display_set_font(&FONT_4X6);
        ui_page_settings_draw_centered_label(0U, 128U, 42U, "WAV files stay");
        ui_page_settings_draw_page_footer("-");
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARING)
    {
        drv_display_set_font(&FONT_5X7);
        ui_page_settings_draw_centered_label(0U,
                                             128U,
                                             30U,
                                             ui_page_settings_multi_prepare_phase_label());
        ui_page_settings_draw_progress_bar(8U,
                                           56U,
                                           112U,
                                           6U,
                                           g_ui_settings.multi_prepare_progress_done,
                                           g_ui_settings.multi_prepare_progress_total);
        drv_display_set_font(&FONT_5X7);
        return;
    }

    ui_page_settings_draw_sample_split_position(g_ui_settings.multi_entry_count,
                                                g_ui_settings.sampler_slot_count);
    drv_display_set_font(&FONT_4X6);

    const uint8_t visible_lines = UI_SETTINGS_SAMPLE_BROWSER_VISIBLE_LINES;
    const uint16_t selected_right_index =
        ui_page_settings_filtered_index_for_global(g_ui_settings.sample_slot_selected);
    g_ui_settings.sample_left_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_left_scroll,
                                                                     g_ui_settings.sample_selected,
                                                                     g_ui_settings.multi_entry_count,
                                                                     visible_lines);
    g_ui_settings.sample_right_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_right_scroll,
                                                                      selected_right_index,
                                                                      g_ui_settings.sampler_slot_count,
                                                                      visible_lines);

    for (uint8_t line = 0U; line < visible_lines; ++line)
    {
        const uint8_t y = (uint8_t)(UI_SETTINGS_SAMPLE_BROWSER_TEXT_Y0
                                    + (line * UI_SETTINGS_SAMPLE_BROWSER_TEXT_PITCH));
        const uint16_t left_index = (uint16_t)(g_ui_settings.sample_left_scroll + line);
        const uint16_t right_index = (uint16_t)(g_ui_settings.sample_right_scroll + line);

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
                drv_display_fill_rect(0, y - 1U, 58, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
        }

        if (right_index < g_ui_settings.sampler_slot_count)
        {
            char right[32];
            const uint16_t global_slot = g_ui_settings.sampler_slots[right_index];
            ui_page_settings_multi_slot_label(global_slot,
                                              right,
                                              sizeof(right),
                                              (uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_W);
            if ((global_slot == g_ui_settings.sample_slot_selected)
                && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS))
            {
                drv_display_fill_rect(62, y - 1U, 66, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
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

    ui_page_settings_draw_page_footer_ex("CLEAR", "-");
    drv_display_set_font(&FONT_5X7);
}

static void ui_page_settings_multi_prepare_flush_progress(void)
{
    drv_display_clear();
    ui_page_settings_render_multi_browser();
    drv_display_update();
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

    ui_page_settings_draw_global_sample_header("STREAM");
    drv_display_draw_line(0, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y, 127, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y);
    ui_page_settings_draw_stream_name_label();
    ui_page_settings_draw_sample_split_position(g_ui_settings.sample_child_count,
                                                g_ui_settings.sampler_slot_count);
    drv_display_set_font(&FONT_4X6);

    const uint8_t visible_lines = UI_SETTINGS_SAMPLE_BROWSER_VISIBLE_LINES;
    const uint16_t selected_right_index =
        ui_page_settings_filtered_index_for_global(g_ui_settings.sample_slot_selected);
    g_ui_settings.sample_left_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_left_scroll,
                                                                     g_ui_settings.sample_selected,
                                                                     g_ui_settings.sample_child_count,
                                                                     visible_lines);
    g_ui_settings.sample_right_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_right_scroll,
                                                                      selected_right_index,
                                                                      g_ui_settings.sampler_slot_count,
                                                                      visible_lines);
    const uint16_t left_start = g_ui_settings.sample_left_scroll;
    const uint8_t right_start = g_ui_settings.sample_right_scroll;

    for (uint8_t line = 0U; line < visible_lines; ++line)
    {
        const uint8_t y = (uint8_t)(UI_SETTINGS_SAMPLE_BROWSER_TEXT_Y0
                                    + (line * UI_SETTINGS_SAMPLE_BROWSER_TEXT_PITCH));
        const uint16_t left_index = (uint16_t)(left_start + line);
        const uint16_t right_index = (uint16_t)(right_start + line);

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
                drv_display_fill_rect(0, y - 1U, 58, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
        }

        if (right_index < g_ui_settings.sampler_slot_count)
        {
            char right[32];
            const uint16_t global_slot = g_ui_settings.sampler_slots[right_index];
            ui_page_settings_sample_slot_label(global_slot,
                                               right,
                                               sizeof(right),
                                               (uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_W);
            if ((global_slot == g_ui_settings.sample_slot_selected)
                && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS))
            {
                drv_display_fill_rect(62, y - 1U, 66, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
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

static void ui_page_settings_render_ram_browser(void)
{
    enum
    {
        UI_SETTINGS_SAMPLE_LEFT_TEXT_X = 1,
        UI_SETTINGS_SAMPLE_LEFT_TEXT_W = 58,
        UI_SETTINGS_SAMPLE_RIGHT_TEXT_X = 64
    };

    ui_page_settings_draw_global_sample_header("RAM");
    drv_display_draw_line(0, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y, 127, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y);
    ui_page_settings_draw_ram_name_label();
    ui_page_settings_draw_sample_split_position(g_ui_settings.sample_child_count,
                                                g_ui_settings.sampler_slot_count);
    drv_display_set_font(&FONT_4X6);

    const uint8_t visible_lines = UI_SETTINGS_SAMPLE_BROWSER_VISIBLE_LINES;
    const uint16_t selected_right_index =
        ui_page_settings_filtered_index_for_global(g_ui_settings.sample_slot_selected);
    g_ui_settings.sample_left_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_left_scroll,
                                                                     g_ui_settings.sample_selected,
                                                                     g_ui_settings.sample_child_count,
                                                                     visible_lines);
    g_ui_settings.sample_right_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_right_scroll,
                                                                      selected_right_index,
                                                                      g_ui_settings.sampler_slot_count,
                                                                      visible_lines);
    for (uint8_t line = 0U; line < visible_lines; ++line)
    {
        const uint8_t y = (uint8_t)(UI_SETTINGS_SAMPLE_BROWSER_TEXT_Y0
                                    + (line * UI_SETTINGS_SAMPLE_BROWSER_TEXT_PITCH));
        const uint16_t left_index = (uint16_t)(g_ui_settings.sample_left_scroll + line);
        const uint16_t right_index = (uint16_t)(g_ui_settings.sample_right_scroll + line);

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
                drv_display_fill_rect(0, y - 1U, 58, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
        }

        if (right_index >= g_ui_settings.sampler_slot_count)
        {
            continue;
        }
        const uint16_t global_slot = g_ui_settings.sampler_slots[right_index];
        char right[24];
        ui_page_settings_ram_slot_label(global_slot, right, sizeof(right), 62U);
        if ((global_slot == g_ui_settings.sample_slot_selected)
            && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS))
        {
            drv_display_fill_rect(62, y - 1U, 66, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
            drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
        }
        else
        {
            drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
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

static void ui_page_settings_render_wavetable_browser(void)
{
    enum
    {
        UI_SETTINGS_SAMPLE_LEFT_TEXT_X = 1,
        UI_SETTINGS_SAMPLE_LEFT_TEXT_W = 58,
        UI_SETTINGS_SAMPLE_RIGHT_TEXT_X = 64
    };

    ui_page_settings_draw_global_sample_header("WAVE");
    drv_display_draw_line(0, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y, 127, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y);
    ui_page_settings_draw_wavetable_name_label();
    ui_page_settings_draw_sample_split_position(g_ui_settings.sample_child_count,
                                                g_ui_settings.sampler_slot_count);
    drv_display_set_font(&FONT_4X6);

    const uint8_t visible_lines = UI_SETTINGS_SAMPLE_BROWSER_VISIBLE_LINES;
    const uint16_t selected_right_index =
        ui_page_settings_filtered_index_for_global(g_ui_settings.sample_slot_selected);
    g_ui_settings.sample_left_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_left_scroll,
                                                                     g_ui_settings.sample_selected,
                                                                     g_ui_settings.sample_child_count,
                                                                     visible_lines);
    g_ui_settings.sample_right_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_right_scroll,
                                                                      selected_right_index,
                                                                      g_ui_settings.sampler_slot_count,
                                                                      visible_lines);
    for (uint8_t line = 0U; line < visible_lines; ++line)
    {
        const uint8_t y = (uint8_t)(UI_SETTINGS_SAMPLE_BROWSER_TEXT_Y0
                                    + (line * UI_SETTINGS_SAMPLE_BROWSER_TEXT_PITCH));
        const uint16_t left_index = (uint16_t)(g_ui_settings.sample_left_scroll + line);
        const uint16_t right_index = (uint16_t)(g_ui_settings.sample_right_scroll + line);

        if (left_index < g_ui_settings.sample_entry_count)
        {
            char left[32];
            const ui_settings_sample_entry_t *const entry = &g_ui_settings.sample_entries[left_index];
            ui_page_settings_fit_label(left,
                                       sizeof(left),
                                       entry->label,
                                       (uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_W);
            if ((left_index == g_ui_settings.sample_selected)
                && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY))
            {
                drv_display_fill_rect(0, y - 1U, 58, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
        }

        if (right_index >= g_ui_settings.sampler_slot_count)
        {
            continue;
        }
        const uint16_t global_slot = g_ui_settings.sampler_slots[right_index];
        char right[24];
        ui_page_settings_wavetable_slot_label(global_slot, right, sizeof(right), 62U);
        if ((global_slot == g_ui_settings.sample_slot_selected)
            && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS))
        {
            drv_display_fill_rect(62, y - 1U, 66, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
            drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
        }
        else
        {
            drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
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

#if BRICK_TEST_BUILD && defined(BRICK6_VARIANT_LOWCOST)
static void ui_page_settings_render_test_hall(void)
{
    char line[24];
    const uint8_t key = g_ui_settings.hall_test_key;
    const uint8_t mux = g_ui_settings.hall_test_mux;
    const uint16_t raw = hall_adc_get_raw(key);
    hall_velocity_debug_t debug = {0};
    hall_engine_get_velocity_debug(key, &debug);
    const uint8_t calibration_valid =
        ((debug.calibrated != 0U) && (debug.range_valid != 0U)) ? 1U : 0U;

    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text(0U, 0U, "TEST > HALL");
    drv_display_draw_line(0, 9, 127, 9);

    (void)snprintf(line,
                   sizeof(line),
                   "HALL %02u / 24",
                   (unsigned)(key + 1U));
    drv_display_draw_text(0U, 12U, line);
    (void)snprintf(line,
                   sizeof(line),
                   "M%u %lu.%lums",
                   (unsigned)mux,
                   (unsigned long)(debug.sample_period_us / 1000U),
                   (unsigned long)((debug.sample_period_us % 1000U) / 100U));
    drv_display_draw_text(70U, 12U, line);

    drv_display_set_font(&FONT_4X6);
    (void)snprintf(line, sizeof(line), "RAW %u", (unsigned)raw);
    drv_display_draw_text(0U, 22U, line);
    (void)snprintf(line, sizeof(line), "ENG %u", (unsigned)debug.raw_current);
    drv_display_draw_text(64U, 22U, line);

    if (calibration_valid != 0U)
    {
        (void)snprintf(line, sizeof(line), "RAW LOW %u", (unsigned)debug.min_current);
        drv_display_draw_text(0U, 32U, line);
        (void)snprintf(line, sizeof(line), "RAW HIGH %u", (unsigned)debug.max_current);
        drv_display_draw_text(0U, 40U, line);
    }
    else
    {
        drv_display_draw_text(0U, 32U, "RAW LOW ---");
        drv_display_draw_text(0U, 40U, "RAW HIGH ---");
    }

    (void)snprintf(line,
                   sizeof(line),
                   "PRESS %s",
                   (debug.state != 0U) ? "ON" : "OFF");
    drv_display_draw_text(0U, 48U, line);
    (void)snprintf(line,
                   sizeof(line),
                   "VEL %u",
                   (unsigned)debug.velocity);
    drv_display_draw_text(72U, 48U, line);

    (void)snprintf(line,
                   sizeof(line),
                   "M%u %u %u %u",
                   (unsigned)mux,
                   (unsigned)hall_adc_get_mux_raw(0U, mux),
                   (unsigned)hall_adc_get_mux_raw(1U, mux),
                   (unsigned)hall_adc_get_mux_raw(2U, mux));
    drv_display_draw_text(0U, 56U, line);

}
#endif

#if BRICK_TEST_BUILD
static void ui_page_settings_render_test_audio(void)
{
    char line[32];
    audio_test_runner_view_t view;
    audio_test_runner_get_view(&view);

    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text(0U, 0U, "AUDIO TEST");
    drv_display_set_font(&FONT_4X6);
    (void)snprintf(line, sizeof(line), "AUTO %03u/%03u",
                   (unsigned)view.test_index, (unsigned)view.test_total);
    drv_display_draw_text(0U, 14U, line);
    drv_display_draw_text(0U, 26U,
        (view.state == AUDIO_TEST_RUNNER_VOLUME_WARNING) ? "TURN VOL DOWN"
                                                        : view.test_name);
    drv_display_draw_rect(0, 38, 120, 8);
    if (view.progress_12 != 0U)
    {
        drv_display_fill_rect(2, 40, (int16_t)(view.progress_12 * 9U), 4);
    }
    drv_display_draw_text(0U, 50U, view.status);
    (void)snprintf(line, sizeof(line), "EST %02lu:%02lu",
                   (unsigned long)(AUDIO_TEST_RUNNER_ESTIMATED_DURATION_MS / 60000U),
                   (unsigned long)((AUDIO_TEST_RUNNER_ESTIMATED_DURATION_MS / 1000U) % 60U));
    drv_display_draw_text(0U, 58U, line);
    drv_display_draw_text(82U, 58U, "P1 STOP");
}

static void ui_page_settings_render_test_audio2(void)
{
    char line[32];
    audio_test2_view_t view;
    audio_test2_get_view(&view);

    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text(0U, 0U, "AUDIO TEST 2");
    drv_display_set_font(&FONT_4X6);

    if ((view.state == AUDIO_TEST2_REFERENCE)
        || (view.state == AUDIO_TEST2_INTERNAL)
        || (view.state == AUDIO_TEST2_VERIFY))
    {
        drv_display_draw_text(0U, 11U,
            (view.state == AUDIO_TEST2_INTERNAL) ? "PHASE 1 INTERNAL"
                                                : "PHASE 1 PREPARE");
    }
    else if ((view.state == AUDIO_TEST2_LINE_READY)
             || (view.state == AUDIO_TEST2_COUNTDOWN_LINE)
             || (view.state == AUDIO_TEST2_LINE))
    {
        drv_display_draw_text(0U, 11U, "PHASE 2 LINE");
    }
    else if ((view.state == AUDIO_TEST2_HEADPHONE_READY)
             || (view.state == AUDIO_TEST2_COUNTDOWN_HEADPHONE)
             || (view.state == AUDIO_TEST2_HEADPHONE))
    {
        drv_display_draw_text(0U, 11U, "PHASE 3 HEADPHONE");
    }
    else
    {
        drv_display_draw_text(0U, 11U,
            (view.state == AUDIO_TEST2_DONE) ? "AUDIO TEST 2 DONE" : "READY");
    }

    if (view.countdown != 0U)
    {
        (void)snprintf(line, sizeof(line), "SILENT START IN %u",
                       (unsigned)view.countdown);
        drv_display_draw_text(0U, 21U, line);
    }
    else
    {
        (void)snprintf(line, sizeof(line), "%03lu / %03lu SEC",
                       (unsigned long)(view.frame / AUDIO_TEST2_SAMPLE_RATE),
                       (unsigned long)AUDIO_TEST2_DURATION_SECONDS);
        drv_display_draw_text(0U, 21U, line);
    }
    drv_display_draw_text(0U, 31U, view.section);
    drv_display_draw_text(0U, 41U, view.status);
    (void)snprintf(line, sizeof(line), "SD%lu OV%lu CRC %08lX",
                   (unsigned long)view.sd_errors,
                   (unsigned long)view.overruns,
                   (unsigned long)((view.state <= AUDIO_TEST2_REFERENCE)
                                       ? view.reference_crc : view.internal_crc));
    drv_display_draw_text(0U, 50U, line);
    drv_display_draw_text(0U, 58U, "P1 X P2 OK P3 L P4 H");
}

static void ui_page_settings_render_test_monkey(void)
{
    char line[28];
    monkey_test_view_t view;
    monkey_test_get_view(&view);

    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text(0U, 0U, "MONKEY TEST");
    drv_display_set_font(&FONT_4X6);
    const char *state_label =
        (view.state == MONKEY_TEST_STATE_RUNNING) ? "RUN"
        : (view.state == MONKEY_TEST_STATE_REPLAYING) ? "REPLAY"
        : (view.state == MONKEY_TEST_STATE_REPLAY_PAUSED) ? "ARMED"
        : (view.state == MONKEY_TEST_STATE_REPLAY_TARGET_DONE) ? "FIRED"
        : (view.state == MONKEY_TEST_STATE_STOPPED) ? "STOP" : "READY";
    (void)snprintf(line, sizeof(line), "%s %02lu:%02lu", state_label,
                   (unsigned long)(view.elapsed_ms / 60000U),
                   (unsigned long)((view.elapsed_ms / 1000U) % 60U));
    drv_display_draw_text(0U, 14U, line);
    (void)snprintf(line, sizeof(line), "SEED %08lX",
                   (unsigned long)view.seed);
    drv_display_draw_text(0U, 26U, line);
    if ((view.state == MONKEY_TEST_STATE_REPLAYING)
        || (view.state == MONKEY_TEST_STATE_REPLAY_PAUSED)
        || (view.state == MONKEY_TEST_STATE_REPLAY_TARGET_DONE))
    {
        (void)snprintf(line, sizeof(line), "RP %lu/%lu",
                       (unsigned long)view.action_count,
                       (unsigned long)view.replay_target_index);
    }
    else
    {
        (void)snprintf(line, sizeof(line), "ACT %lu %s",
                       (unsigned long)view.action_count,
                       monkey_test_action_type_label(view.last_action_type));
    }
    drv_display_draw_text(0U, 38U, line);
    (void)snprintf(line, sizeof(line), "W%lu E%lu C%lu",
                   (unsigned long)view.warning_count,
                   (unsigned long)view.error_count,
                   (unsigned long)view.crash_count);
    drv_display_draw_text(0U, 50U, line);
    drv_display_draw_text(0U, 58U,
                          monkey_test_issue_label(view.last_issue));
    if (view.state == MONKEY_TEST_STATE_REPLAY_PAUSED)
    {
        drv_display_draw_text(70U, 58U, "P1 X P3 FIRE");
    }
    else if (monkey_test_is_active() != 0U)
    {
        drv_display_draw_text(88U, 58U, "P1 STOP");
    }
    else if (view.recovery_available != 0U)
    {
        drv_display_draw_text(62U, 58U, "P1 NEW P2 RPLY");
    }
    else
    {
        drv_display_draw_text(88U, 58U, "P1 START");
    }
}
#endif

static void ui_page_settings_render(void)
{
    ui_settings_menu_level_t *const level = ui_page_settings_current_level();
    if (level == 0)
    {
        drv_display_set_font(&FONT_5X7);
        drv_display_draw_text(0U, 0U, "SETTINGS");
        drv_display_set_font(&FONT_4X6);
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
    if (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)
    {
        ui_page_settings_render_ram_browser();
        return;
    }
    if (level->view == UI_SETTINGS_VIEW_WAVETABLE)
    {
        ui_page_settings_render_wavetable_browser();
        return;
    }
#if BRICK_TEST_BUILD && defined(BRICK6_VARIANT_LOWCOST)
    if (level->view == UI_SETTINGS_VIEW_TEST_HALL)
    {
        ui_page_settings_render_test_hall();
        return;
    }
#endif
#if BRICK_TEST_BUILD
    if (level->view == UI_SETTINGS_VIEW_TEST_AUDIO)
    {
        ui_page_settings_render_test_audio();
        return;
    }
    if (level->view == UI_SETTINGS_VIEW_TEST_AUDIO2)
    {
        ui_page_settings_render_test_audio2();
        return;
    }
    if (level->view == UI_SETTINGS_VIEW_TEST_MONKEY)
    {
        ui_page_settings_render_test_monkey();
        return;
    }
#endif

    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text(0U, 0U, ui_page_settings_view_title(level->view));
    drv_display_draw_line(0, 9, 127, 9);
    drv_display_set_font(&FONT_4X6);

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
            drv_display_fill_rect(0, y - 1U, 128, 8);
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

void ui_page_settings_open_sample_browser(uint8_t return_page_id)
{
    g_ui_settings.return_page_id = return_page_id;
    ui_page_set(UI_PAGE_SETTINGS);
    g_ui_settings.depth = 0U;
    ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLE);
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

#if BRICK_TEST_BUILD
    if (level->view == UI_SETTINGS_VIEW_TEST_AUDIO)
    {
        return;
    }
    if (level->view == UI_SETTINGS_VIEW_TEST_AUDIO2)
    {
        return;
    }
    if (level->view == UI_SETTINGS_VIEW_TEST_MONKEY)
    {
        return;
    }
#endif

#if BRICK_TEST_BUILD && defined(BRICK6_VARIANT_LOWCOST)
    if (level->view == UI_SETTINGS_VIEW_TEST_HALL)
    {
        if (encoder > 1U)
        {
            return;
        }

        g_ui_settings.encoder_accum[encoder] =
            (int16_t)(g_ui_settings.encoder_accum[encoder] + delta);
        const int16_t step =
            (int16_t)(g_ui_settings.encoder_accum[encoder] / UI_SETTINGS_ENCODER_DIVIDER);
        g_ui_settings.encoder_accum[encoder] =
            (int16_t)(g_ui_settings.encoder_accum[encoder]
                      - (step * UI_SETTINGS_ENCODER_DIVIDER));
        if (step != 0)
        {
            if (encoder == 0U)
            {
                int32_t key = (int32_t)g_ui_settings.hall_test_key + step;
                if (key < 0)
                {
                    key = 0;
                }
                else if (key >= HALL_KEY_COUNT)
                {
                    key = HALL_KEY_COUNT - 1U;
                }
                g_ui_settings.hall_test_key = (uint8_t)key;
            }
            else
            {
                int32_t mux = (int32_t)g_ui_settings.hall_test_mux + step;
                if (mux < 0)
                {
                    mux = 0;
                }
                else if (mux >= 8)
                {
                    mux = 7;
                }
                g_ui_settings.hall_test_mux = (uint8_t)mux;
            }
        }
        return;
    }
#endif

    if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
        || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)
        || (level->view == UI_SETTINGS_VIEW_WAVETABLE)
        || (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE))
    {
        if ((level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
            && (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARING))
        {
            return;
        }

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
                if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
                    || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)
                    || (level->view == UI_SETTINGS_VIEW_WAVETABLE))
                {
                    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
                }
            }
            g_ui_settings.sample_selected = (uint16_t)index;
            if (((level->view == UI_SETTINGS_VIEW_SAMPLER)
                 || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM))
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
            if (g_ui_settings.sampler_slot_count == 0U)
            {
                return;
            }
            int32_t index =
                (int32_t)ui_page_settings_filtered_index_for_global(g_ui_settings.sample_slot_selected);
            const int32_t slot_count = (int32_t)g_ui_settings.sampler_slot_count;
            index += step;
            if (index < 0)
            {
                index = 0;
            }
            else if (index >= slot_count)
            {
                index = slot_count - 1;
            }
            const uint16_t new_global = g_ui_settings.sampler_slots[(uint16_t)index];
            if (new_global != g_ui_settings.sample_slot_selected)
            {
                if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
                    || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)
                    || (level->view == UI_SETTINGS_VIEW_WAVETABLE))
                {
                    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
                }
            }
            g_ui_settings.sample_slot_selected = new_global;
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
