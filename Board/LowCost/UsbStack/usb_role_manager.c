#include "usb_role_manager.h"

#include "fusb302.h"
#include "i2c.h"
#include "main.h"
#include "usb_device.h"
#include "usb_host.h"

#define USB_HOST_POWER_SETTLE_MS 200U
#define USB_FUSB_RETRY_MS        100U
#define USB_FUSB_POLL_MS         100U

typedef struct
{
    usb_role_manager_role_t active;
    usb_role_manager_role_t requested;
    uint8_t initialized;
    uint8_t host_fault;
    uint8_t host_power_waiting;
    uint8_t fusb_retry_waiting;
    volatile uint8_t host_flag_event_pending;
    uint32_t host_power_deadline;
    uint32_t fusb_retry_deadline;
    uint32_t fusb_poll_deadline;
} usb_role_manager_ctx_t;

static usb_role_manager_ctx_t g_usb_role;

static uint8_t deadline_reached(uint32_t deadline)
{
    return ((int32_t)(HAL_GetTick() - deadline) >= 0) ? 1U : 0U;
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
        (void)usb_device_stop();
    }
    if (g_usb_role.host_power_waiting != 0U)
    {
        usb_host_power_off();
        g_usb_role.host_power_waiting = 0U;
    }
    g_usb_role.active = USB_ROLE_MANAGER_NONE;
}

static void apply_role(usb_role_manager_role_t requested)
{
    if ((requested == g_usb_role.active)
            && (g_usb_role.host_power_waiting == 0U))
    {
        g_usb_role.requested = requested;
        return;
    }

    if ((requested == USB_ROLE_MANAGER_HOST)
            && (host_fault_active() != 0U))
    {
        stop_active_role();
        g_usb_role.host_fault = 1U;
        g_usb_role.requested = requested;
        return;
    }

    stop_active_role();
    g_usb_role.host_fault = 0U;
    g_usb_role.requested = requested;

    if (requested == USB_ROLE_MANAGER_DEVICE)
    {
        if (usb_device_start() != 0U)
        {
            g_usb_role.active = USB_ROLE_MANAGER_DEVICE;
        }
    }
    else if (requested == USB_ROLE_MANAGER_HOST)
    {
        if (usb_host_prepare() != 0U)
        {
            g_usb_role.host_power_waiting = 1U;
            g_usb_role.host_power_deadline =
                HAL_GetTick() + USB_HOST_POWER_SETTLE_MS;
        }
        else
        {
            usb_host_power_off();
        }
    }
}

void usb_role_manager_init(void)
{
    if (g_usb_role.initialized != 0U)
    {
        return;
    }

    g_usb_role = (usb_role_manager_ctx_t){0};
    usb_host_power_off();
    if (fusb302_init(&hi2c1) == FUSB302_STATUS_OK)
    {
        g_usb_role.initialized = 1U;
        g_usb_role.fusb_poll_deadline = HAL_GetTick();
        apply_role(role_from_fusb302(fusb302_cached_role()));
    }
}

void usb_role_manager_process(void)
{
    if (g_usb_role.initialized == 0U)
    {
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

    /* INT_N is level-signalled.  A falling edge can be missed when the line
     * is already asserted as EXTI is armed, so retain the EXTI latch as the
     * fast path and use the physical level as the recovery path. */
    const uint8_t fusb_attention =
        (fusb302_irq_pending()
         || (HAL_GPIO_ReadPin(FUSB302_INT_N_GPIO_Port,
                              FUSB302_INT_N_Pin) == GPIO_PIN_RESET)) ? 1U : 0U;
    /* Reconcile the authoritative registers at a bounded rate as well.  DRP
     * can complete after fusb302_init() sampled TOGSS=RUNNING, and INT_N is
     * not a durable indication once an interrupt source has been consumed. */
    const uint8_t fusb_poll_due =
        deadline_reached(g_usb_role.fusb_poll_deadline);
    if ((fusb_attention != 0U) || (fusb_poll_due != 0U))
    {
        if ((g_usb_role.fusb_retry_waiting != 0U)
                && (deadline_reached(g_usb_role.fusb_retry_deadline) == 0U))
        {
            return;
        }
        const fusb302_status_t status = (fusb_attention != 0U)
            ? fusb302_handle_interrupt()
            : fusb302_refresh_state();
        if (status != FUSB302_STATUS_OK)
        {
            g_usb_role.fusb_retry_waiting = 1U;
            g_usb_role.fusb_retry_deadline = HAL_GetTick() + USB_FUSB_RETRY_MS;
            return;
        }
        g_usb_role.fusb_retry_waiting = 0U;
        g_usb_role.fusb_poll_deadline = HAL_GetTick() + USB_FUSB_POLL_MS;
    }

    const usb_role_manager_role_t requested =
        role_from_fusb302(fusb302_cached_role());

    if (g_usb_role.host_power_waiting != 0U)
    {
        if (requested != USB_ROLE_MANAGER_HOST)
        {
            apply_role(requested);
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
        }
        return;
    }

    if ((requested != g_usb_role.requested)
            || (requested != g_usb_role.active))
    {
        apply_role(requested);
    }
}

void usb_role_manager_shutdown(void)
{
    stop_active_role();
    usb_host_power_off();
    g_usb_role.requested = USB_ROLE_MANAGER_NONE;
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
