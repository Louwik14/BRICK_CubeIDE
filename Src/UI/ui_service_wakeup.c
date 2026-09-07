#include "UI/ui_service_wakeup.h"

#include "cmsis_os.h"
#include "stm32h7xx.h"

extern osThreadId_t UI_SERVICEHandle;

static volatile uint8_t g_ui_dirty;
static volatile uint8_t g_ui_led_dirty;
static volatile uint8_t g_ui_asset_receipts_invalidation_pending;
static volatile uint8_t g_ui_project_progress_pending;
static volatile uint8_t g_ui_settings_progress_pending;
static volatile uint8_t g_ui_audio_rec_data_pending;

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
