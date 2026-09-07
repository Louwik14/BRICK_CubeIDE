#include "UI/ui_service_wakeup.h"

#include "cmsis_os.h"
#include "IPC/ui_visible_data.h"
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"

extern osThreadId_t UI_SERVICEHandle;

static volatile uint8_t g_ui_dirty;
static volatile uint8_t g_ui_led_dirty;
static volatile uint8_t g_ui_asset_receipts_invalidation_pending;
static volatile uint8_t g_ui_project_progress_pending;
static volatile uint8_t g_ui_settings_progress_pending;
static volatile uint8_t g_ui_audio_rec_data_pending;
static volatile uint8_t g_ui_cpu_load_visible;
static uint8_t g_ui_visible_data_version_valid[UI_VISIBLE_DATA_COUNT];
static uint32_t g_ui_visible_data_version[UI_VISIBLE_DATA_COUNT];
static uint32_t g_ui_cpu_load_notify_ms;

#define UI_CPU_LOAD_NOTIFY_MIN_PERIOD_MS 100U

static uint8_t ui_service_flag_take(volatile uint8_t *flag)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint8_t pending = *flag;
    *flag = 0U;
    __set_PRIMASK(primask);
    return pending;
}

void ui_service_wakeup(uint32_t flags)
{
    if ((flags == 0U) || (UI_SERVICEHandle == NULL))
    {
        return;
    }

    if (osKernelGetState() != osKernelRunning)
    {
        return;
    }

    (void)osThreadFlagsSet(UI_SERVICEHandle, flags);
}

void ui_service_dirty_set(void)
{
    g_ui_dirty = 1U;
    ui_service_wakeup(UI_SERVICE_WAKE_DIRTY);
}

uint8_t ui_service_dirty_take(void)
{
    return ui_service_flag_take(&g_ui_dirty);
}

uint8_t ui_service_dirty_is_set(void)
{
    return g_ui_dirty;
}

void ui_service_led_dirty_set(void)
{
    g_ui_led_dirty = 1U;
    ui_service_wakeup(UI_SERVICE_WAKE_LED);
}

uint8_t ui_service_led_dirty_take(void)
{
    return ui_service_flag_take(&g_ui_led_dirty);
}

uint8_t ui_service_led_dirty_is_set(void)
{
    return g_ui_led_dirty;
}

void ui_service_asset_receipts_invalidate(void)
{
    g_ui_asset_receipts_invalidation_pending = 1U;
    ui_service_wakeup(UI_SERVICE_WAKE_INPUT);
}

uint8_t ui_service_asset_receipts_invalidation_take(void)
{
    return ui_service_flag_take(&g_ui_asset_receipts_invalidation_pending);
}

uint8_t ui_service_asset_receipts_invalidation_is_pending(void)
{
    return g_ui_asset_receipts_invalidation_pending;
}

void ui_service_project_progress_notify(void)
{
    g_ui_project_progress_pending = 1U;
    ui_service_wakeup(UI_SERVICE_WAKE_INPUT);
}

uint8_t ui_service_project_progress_take(void)
{
    return ui_service_flag_take(&g_ui_project_progress_pending);
}

uint8_t ui_service_project_progress_is_pending(void)
{
    return g_ui_project_progress_pending;
}

void ui_service_settings_progress_notify(void)
{
    g_ui_settings_progress_pending = 1U;
    ui_service_wakeup(UI_SERVICE_WAKE_INPUT);
}

uint8_t ui_service_settings_progress_take(void)
{
    return ui_service_flag_take(&g_ui_settings_progress_pending);
}

uint8_t ui_service_settings_progress_is_pending(void)
{
    return g_ui_settings_progress_pending;
}

void ui_service_audio_rec_data_notify(void)
{
    g_ui_audio_rec_data_pending = 1U;
    ui_service_wakeup(UI_SERVICE_WAKE_INPUT);
}

uint8_t ui_service_audio_rec_data_take(void)
{
    return ui_service_flag_take(&g_ui_audio_rec_data_pending);
}

uint8_t ui_service_audio_rec_data_is_pending(void)
{
    return g_ui_audio_rec_data_pending;
}

void ui_visible_data_cpu_set_visible(uint8_t visible)
{
    g_ui_cpu_load_visible = (visible != 0U) ? 1U : 0U;
    if (visible == 0U)
        g_ui_visible_data_version_valid[UI_VISIBLE_DATA_CPU_LOAD] = 0U;
}

void ui_visible_data_notify(ui_visible_data_kind_t kind, uint32_t version)
{
    if (kind >= UI_VISIBLE_DATA_COUNT)
        return;
    if ((kind == UI_VISIBLE_DATA_CPU_LOAD) && (g_ui_cpu_load_visible == 0U))
        return;
    if ((g_ui_visible_data_version_valid[kind] != 0U)
            && (g_ui_visible_data_version[kind] == version))
        return;
    if (kind == UI_VISIBLE_DATA_CPU_LOAD)
    {
        const uint32_t now_ms = HAL_GetTick();
        if ((g_ui_visible_data_version_valid[kind] != 0U)
                && ((uint32_t)(now_ms - g_ui_cpu_load_notify_ms)
                    < UI_CPU_LOAD_NOTIFY_MIN_PERIOD_MS))
            return;
        g_ui_cpu_load_notify_ms = now_ms;
    }
    g_ui_visible_data_version[kind] = version;
    g_ui_visible_data_version_valid[kind] = 1U;
    ui_service_dirty_set();
}
