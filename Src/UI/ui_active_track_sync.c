#include "ui_active_track_sync.h"

#include "ui_core.h"
#include "UI/ui_service_wakeup.h"
#include "stm32h7xx.h"

static volatile uint8_t g_ui_active_track_sync_pending;

void ui_active_track_sync_notify_product_changed(void)
{
    g_ui_active_track_sync_pending = 1U;
    __DMB();
    ui_service_wakeup(UI_SERVICE_WAKE_INPUT);
}

uint8_t ui_active_track_sync_is_pending(void)
{
    return g_ui_active_track_sync_pending;
}

void ui_active_track_sync_process_pending(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint8_t pending = g_ui_active_track_sync_pending;
    g_ui_active_track_sync_pending = 0U;
    __DMB();
    __set_PRIMASK(primask);

    if (pending != 0U)
    {
        ui_core_reconcile_current_product_context();
    }
}
