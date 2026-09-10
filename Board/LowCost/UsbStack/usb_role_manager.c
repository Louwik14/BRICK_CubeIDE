#include "usb_role_manager.h"

#include "fusb302.h"
#include "i2c.h"
#include "main.h"
#include "usb_device.h"
#include "usb_host.h"
#include "Platform/idle_latency_diag.h"

#define USB_HOST_POWER_SETTLE_MS 200U
#define USB_FUSB_RETRY_MS        100U
#define USB_FUSB_WATCHDOG_MS     5000U
#define USB_ROLE_RETRY_MS        100U

typedef enum
{
    USB_FUSB_RETRY_NONE = 0,
    USB_FUSB_RETRY_INIT,
    USB_FUSB_RETRY_INTERRUPT,
    USB_FUSB_RETRY_WATCHDOG,
    USB_FUSB_RETRY_RESTART
} usb_fusb_retry_kind_t;

typedef struct
{
    usb_role_manager_role_t active;
    usb_role_manager_role_t requested;
    uint8_t initialized;
    uint8_t fusb_ready;
    uint8_t host_fault;
    uint8_t host_power_waiting;
    uint8_t fusb_retry_waiting;
    uint8_t fusb_int_low_serviced;
    uint8_t role_retry_waiting;
    usb_fusb_retry_kind_t fusb_retry_kind;
    volatile uint8_t host_flag_event_pending;
    uint32_t host_power_deadline;
    uint32_t fusb_retry_deadline;
    uint32_t fusb_watchdog_deadline;
    uint32_t role_retry_deadline;
} usb_role_manager_ctx_t;

static usb_role_manager_ctx_t g_usb_role;

static uint8_t deadline_reached(uint32_t deadline)
{
    return ((int32_t)(HAL_GetTick() - deadline) >= 0) ? 1U : 0U;
}

static void schedule_fusb_retry(usb_fusb_retry_kind_t kind)
{
    g_usb_role.fusb_retry_waiting = 1U;
    g_usb_role.fusb_retry_kind = kind;
    g_usb_role.fusb_retry_deadline = HAL_GetTick() + USB_FUSB_RETRY_MS;
}

static usb_role_manager_role_t role_from_fusb302(fusb302_role_t role)
{
    switch (role)
    {
        case FUSB302_ROLE_DEVICE: return USB_ROLE_MANAGER_DEVICE;
        case FUSB302_ROLE_HOST: return USB_ROLE_MANAGER_HOST;
        case FUSB302_ROLE_NONE: return USB_ROLE_MANAGER_NONE;
        default: return USB_ROLE_MANAGER_UNKNOWN;
    }
}

static uint8_t host_fault_active(void)
{
    return (HAL_GPIO_ReadPin(HOST_FLAG_GPIO_Port, HOST_FLAG_Pin)
            == GPIO_PIN_RESET) ? 1U : 0U;
}

static uint8_t take_host_flag_event(void)
{
    const uint32_t primask = __get_PRIMASK();
    uint8_t pending;
    __disable_irq();
    pending = g_usb_role.host_flag_event_pending;
    g_usb_role.host_flag_event_pending = 0U;
    __DMB();
    __set_PRIMASK(primask);
    return pending;
}

static void stop_active_role(void)
{
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    if (g_usb_role.active == USB_ROLE_MANAGER_HOST)
    {
        (void)usb_host_stop();
    }
    else if (g_usb_role.active == USB_ROLE_MANAGER_DEVICE)
    {
        g_idle_latency_diag.usb_device_stop_count++;
        (void)usb_device_stop();
    }
    if (g_usb_role.host_power_waiting != 0U)
    {
        usb_host_power_off();
        g_usb_role.host_power_waiting = 0U;
    }
    g_usb_role.active = USB_ROLE_MANAGER_NONE;
}

static uint8_t apply_role(usb_role_manager_role_t requested)
{
    if ((requested == g_usb_role.active)
            && (g_usb_role.host_power_waiting == 0U))
    {
        g_usb_role.requested = requested;
        return 1U;
    }

    if ((requested == USB_ROLE_MANAGER_HOST)
            && (host_fault_active() != 0U))
    {
        stop_active_role();
        g_usb_role.host_fault = 1U;
        g_usb_role.requested = requested;
        return 0U;
    }

    stop_active_role();
    g_usb_role.host_fault = 0U;
    g_usb_role.requested = requested;

    if (requested == USB_ROLE_MANAGER_DEVICE)
    {
        if (usb_device_start() != 0U)
        {
            g_usb_role.active = USB_ROLE_MANAGER_DEVICE;
            g_idle_latency_diag.usb_device_start_count++;
            return 1U;
        }
        return 0U;
    }
    else if (requested == USB_ROLE_MANAGER_HOST)
    {
        if (usb_host_prepare() != 0U)
        {
            g_usb_role.host_power_waiting = 1U;
            g_usb_role.host_power_deadline =
                HAL_GetTick() + USB_HOST_POWER_SETTLE_MS;
            return 1U;
        }
        else
        {
            usb_host_power_off();
            return 0U;
        }
    }
    return 1U;
}

void usb_role_manager_init(void)
{
    if (g_usb_role.initialized != 0U)
    {
        return;
    }

    g_usb_role = (usb_role_manager_ctx_t){0};
    g_usb_role.initialized = 1U;
    usb_host_power_off();
    if (fusb302_init(&hi2c1) == FUSB302_STATUS_OK)
    {
        g_usb_role.fusb_ready = 1U;
        g_usb_role.fusb_watchdog_deadline =
            HAL_GetTick() + USB_FUSB_WATCHDOG_MS;
        if (apply_role(role_from_fusb302(fusb302_cached_role())) == 0U)
        {
            g_usb_role.role_retry_waiting = 1U;
            g_usb_role.role_retry_deadline = HAL_GetTick() + USB_ROLE_RETRY_MS;
        }
    }
    else
    {
        schedule_fusb_retry(USB_FUSB_RETRY_INIT);
    }
}

void usb_role_manager_process(void)
{
    if (g_usb_role.initialized == 0U)
    {
        return;
    }

    if (g_usb_role.fusb_ready == 0U)
    {
        if ((g_usb_role.fusb_retry_waiting == 0U)
            || (deadline_reached(g_usb_role.fusb_retry_deadline) == 0U))
        {
            return;
        }
        g_idle_latency_diag.usb_retry_count++;
        if (fusb302_init(&hi2c1) != FUSB302_STATUS_OK)
        {
            g_idle_latency_diag.usb_refresh_error_count++;
            schedule_fusb_retry(USB_FUSB_RETRY_INIT);
            return;
        }
        g_idle_latency_diag.usb_refresh_ok_count++;
        g_usb_role.fusb_ready = 1U;
        g_usb_role.fusb_retry_waiting = 0U;
        g_usb_role.fusb_retry_kind = USB_FUSB_RETRY_NONE;
        g_usb_role.fusb_watchdog_deadline =
            HAL_GetTick() + USB_FUSB_WATCHDOG_MS;
        if (apply_role(role_from_fusb302(fusb302_cached_role())) == 0U)
        {
            g_usb_role.role_retry_waiting = 1U;
            g_usb_role.role_retry_deadline = HAL_GetTick() + USB_ROLE_RETRY_MS;
        }
        return;
    }

    if ((take_host_flag_event() != 0U)
            && ((g_usb_role.active == USB_ROLE_MANAGER_HOST)
                || (g_usb_role.host_power_waiting != 0U)))
    {
        if (host_fault_active() != 0U)
        {
            g_usb_role.host_fault = 1U;
            stop_active_role();
            return;
        }
        g_usb_role.host_fault = 0U;
    }

    if ((g_usb_role.active == USB_ROLE_MANAGER_HOST)
            && (host_fault_active() != 0U))
    {
        g_usb_role.host_fault = 1U;
        stop_active_role();
        return;
    }

    const uint8_t fusb_int_low =
        (HAL_GPIO_ReadPin(FUSB302_INT_N_GPIO_Port,
                          FUSB302_INT_N_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
    if (fusb_int_low != 0U)
        g_idle_latency_diag.usb_int_low_count++;
    else
        g_usb_role.fusb_int_low_serviced = 0U;

    /* EXTI is the normal path.  A level that remains low is retried at 100 ms;
     * the only unconditional reconciliation is the 5 s watchdog. */
    const uint8_t fusb_attention =
        (fusb302_irq_pending()
         || ((fusb_int_low != 0U)
             && (g_usb_role.fusb_int_low_serviced == 0U))) ? 1U : 0U;
    const uint8_t fusb_retry_due =
        ((g_usb_role.fusb_retry_waiting != 0U)
         && (deadline_reached(g_usb_role.fusb_retry_deadline) != 0U)) ? 1U : 0U;
    const uint8_t fusb_watchdog_due =
        deadline_reached(g_usb_role.fusb_watchdog_deadline);
    usb_fusb_retry_kind_t operation = USB_FUSB_RETRY_NONE;
    if (fusb_retry_due != 0U)
        operation = g_usb_role.fusb_retry_kind;
    else if (g_usb_role.fusb_retry_waiting == 0U)
    {
        if (fusb_attention != 0U)
            operation = USB_FUSB_RETRY_INTERRUPT;
        else if (fusb_watchdog_due != 0U)
            operation = USB_FUSB_RETRY_WATCHDOG;
    }

    if (operation != USB_FUSB_RETRY_NONE)
    {
        uint32_t events = FUSB302_EVENT_NONE;
        if (operation == USB_FUSB_RETRY_INTERRUPT)
            g_idle_latency_diag.usb_attention_count++;
        else if (operation == USB_FUSB_RETRY_WATCHDOG)
            g_idle_latency_diag.usb_periodic_poll_count++;
        if (fusb_retry_due != 0U)
            g_idle_latency_diag.usb_retry_count++;
        fusb302_status_t status;
        if (operation == USB_FUSB_RETRY_INTERRUPT)
            status = fusb302_handle_interrupt(&events);
        else if (operation == USB_FUSB_RETRY_WATCHDOG)
            status = fusb302_watchdog(&events);
        else
            status = fusb302_restart_drp();
        if (status != FUSB302_STATUS_OK)
        {
            g_idle_latency_diag.usb_refresh_error_count++;
            schedule_fusb_retry(operation);
            return;
        }
        g_idle_latency_diag.usb_refresh_ok_count++;
        g_usb_role.fusb_retry_waiting = 0U;
        g_usb_role.fusb_retry_kind = USB_FUSB_RETRY_NONE;
        g_usb_role.fusb_watchdog_deadline =
            HAL_GetTick() + USB_FUSB_WATCHDOG_MS;
        if (operation == USB_FUSB_RETRY_RESTART)
        {
            g_idle_latency_diag.usb_drp_restart_count++;
            g_usb_role.fusb_int_low_serviced = 0U;
            return;
        }
        g_usb_role.fusb_int_low_serviced =
            (HAL_GPIO_ReadPin(FUSB302_INT_N_GPIO_Port,
                              FUSB302_INT_N_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
        if (g_usb_role.fusb_int_low_serviced != 0U)
            schedule_fusb_retry(USB_FUSB_RETRY_INTERRUPT);

        if ((events & (FUSB302_EVENT_DETACH | FUSB302_EVENT_RESET)) != 0U)
        {
            if ((events & FUSB302_EVENT_DETACH) != 0U)
                g_idle_latency_diag.usb_detach_count++;
            if ((events & FUSB302_EVENT_RESET) != 0U)
                g_idle_latency_diag.usb_watchdog_recovery_count++;
            stop_active_role();
            g_usb_role.requested = USB_ROLE_MANAGER_NONE;
            g_usb_role.role_retry_waiting = 0U;
            status = fusb302_restart_drp();
            if (status != FUSB302_STATUS_OK)
            {
                g_idle_latency_diag.usb_refresh_error_count++;
                schedule_fusb_retry(USB_FUSB_RETRY_RESTART);
            }
            else
            {
                g_idle_latency_diag.usb_drp_restart_count++;
                g_usb_role.fusb_retry_waiting = 0U;
                g_usb_role.fusb_retry_kind = USB_FUSB_RETRY_NONE;
                g_usb_role.fusb_int_low_serviced = 0U;
                g_usb_role.fusb_watchdog_deadline =
                    HAL_GetTick() + USB_FUSB_WATCHDOG_MS;
            }
            return;
        }
        if ((events & FUSB302_EVENT_ATTACH) != 0U)
            g_idle_latency_diag.usb_attach_count++;
        if ((events & FUSB302_EVENT_ERROR) != 0U)
            schedule_fusb_retry(USB_FUSB_RETRY_WATCHDOG);
    }

    const usb_role_manager_role_t requested =
        role_from_fusb302(fusb302_cached_role());

    if (g_usb_role.host_power_waiting != 0U)
    {
        if (requested != USB_ROLE_MANAGER_HOST)
        {
            g_usb_role.role_retry_waiting = 0U;
            (void)apply_role(requested);
            return;
        }
        if (deadline_reached(g_usb_role.host_power_deadline) == 0U)
        {
            return;
        }
        g_usb_role.host_power_waiting = 0U;
        if (usb_host_start() != 0U)
        {
            g_usb_role.active = USB_ROLE_MANAGER_HOST;
        }
        else
        {
            usb_host_power_off();
            g_usb_role.role_retry_waiting = 1U;
            g_usb_role.role_retry_deadline = HAL_GetTick() + USB_ROLE_RETRY_MS;
        }
        return;
    }

    if ((requested != g_usb_role.requested)
            || (requested != g_usb_role.active))
    {
        if (requested != g_usb_role.requested)
            g_usb_role.role_retry_waiting = 0U;
        if ((g_usb_role.role_retry_waiting != 0U)
            && (deadline_reached(g_usb_role.role_retry_deadline) == 0U))
            return;
        if (apply_role(requested) != 0U)
        {
            g_usb_role.role_retry_waiting = 0U;
        }
        else
        {
            g_usb_role.role_retry_waiting = 1U;
            g_usb_role.role_retry_deadline = HAL_GetTick() + USB_ROLE_RETRY_MS;
        }
    }
}

void usb_role_manager_shutdown(void)
{
    stop_active_role();
    usb_host_power_off();
    g_usb_role.requested = USB_ROLE_MANAGER_NONE;
    g_usb_role.fusb_ready = 0U;
    g_usb_role.initialized = 0U;
}

usb_role_manager_role_t usb_role_manager_active_role(void)
{
    return g_usb_role.active;
}

uint8_t usb_role_manager_is_device_active(void)
{
    return (g_usb_role.active == USB_ROLE_MANAGER_DEVICE) ? 1U : 0U;
}

uint8_t usb_role_manager_is_host_active(void)
{
    return (g_usb_role.active == USB_ROLE_MANAGER_HOST) ? 1U : 0U;
}

uint8_t usb_role_manager_host_fault_active(void)
{
    return g_usb_role.host_fault;
}

void usb_role_manager_host_flag_irq(void)
{
    g_usb_role.host_flag_event_pending = 1U;
    __DMB();
}

void usb_role_irq_dispatch(void)
{
    if (usb_role_manager_is_host_active() != 0U)
    {
        usb_host_irq();
    }
    else if (usb_role_manager_is_device_active() != 0U)
    {
        usb_device_irq();
    }
}
