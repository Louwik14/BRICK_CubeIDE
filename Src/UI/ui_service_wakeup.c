#include "UI/ui_service_wakeup.h"

#include "cmsis_os.h"

extern osThreadId_t UI_SERVICEHandle;

static volatile uint8_t g_ui_dirty;
static volatile uint8_t g_ui_led_dirty;

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
    const uint8_t dirty = g_ui_dirty;
    g_ui_dirty = 0U;
    return dirty;
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
    const uint8_t dirty = g_ui_led_dirty;
    g_ui_led_dirty = 0U;
    return dirty;
}

uint8_t ui_service_led_dirty_is_set(void)
{
    return g_ui_led_dirty;
}
