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
#include "Platform/memory_layout.h"
#include "Storage/wav_convert.h"
#include "Storage/audio_recorder.h"
#include "Storage/project_product.h"
#include "Storage/project_load_quiesce.h"
#include "SD/sd_scheduler_runtime.h"
#include "Platform/brick_build_config.h"
#include "Storage/project_control.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Sampler/sample_cache.h"
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
    char convert_path[SAMPLE_CLASSIC_PATH_MAX];
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
    uint8_t multi_clear_failed;
    uint8_t multi_clear_mounted;
    uint16_t multi_clear_index;
    uint16_t multi_clear_deleted;
    uint16_t sample_parent_id;
    uint16_t sampler_slots[SAMPLE_GLOBAL_POOL_FINAL_SLOTS];
    uint16_t sampler_slot_count;
    uint8_t project_slots[PROJECT_PRODUCT_SLOT_COUNT];
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
static void ui_page_settings_multi_clear_service(void);
static const char *ui_page_settings_multi_load_error_label(multi_sample_load_result_t result);
static void ui_page_settings_flash_sample_header_slots(void);
static void ui_page_settings_flash_sample_header_memory(void);
static uint32_t ui_page_settings_multi_sample_prep_bytes(
    const multi_sample_index_sample_t *sample);
static uint32_t ui_page_settings_multi_slot_bytes(void);
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


/* Settings navigation, asset browsers/actions and rendering remain in their original sequence.
 * Private fragments share this translation unit to preserve UI state and call order. */

#include "Settings/ui_settings_catalog_browser.inc"

#include "Settings/ui_settings_sample_assets.inc"

#include "Settings/ui_settings_multi_assets.inc"

#include "Settings/ui_settings_navigation.inc"

#include "Settings/ui_settings_render.inc"

#include "Settings/ui_settings_public.inc"
