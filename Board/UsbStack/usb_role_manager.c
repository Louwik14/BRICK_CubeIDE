#include "usb_role_manager.h"

#include <stddef.h>

#include "fusb302.h"
#include "i2c.h"
#include "main.h"
#include "midi.h"
#include "usb_device.h"
#include "usb_host.h"

typedef struct {
    usb_role_manager_role_t active;
    usb_role_manager_role_t requested;
    uint8_t initialized;
    uint8_t host_fault;
    uint8_t shutdown_requested;
    uint8_t host_power_waiting;
    volatile uint8_t host_flag_event_pending;
    uint32_t host_power_deadline;
} usb_role_manager_ctx_t;

#define USB_HOST_POWER_SETTLE_MS 200U

static usb_role_manager_ctx_t g_usb_role;

static uint8_t deadline_reached(uint32_t deadline)
{
    return ((int32_t)(HAL_GetTick() - deadline) >= 0) ? 1U : 0U;
}

static usb_role_manager_role_t role_from_fusb302(fusb302_role_t role)
{
    switch (role) {
    case FUSB302_ROLE_DEVICE:
        return USB_ROLE_MANAGER_DEVICE;
    case FUSB302_ROLE_HOST:
        return USB_ROLE_MANAGER_HOST;
    case FUSB302_ROLE_NONE:
        return USB_ROLE_MANAGER_NONE;
    case FUSB302_ROLE_UNKNOWN:
    default:
        return USB_ROLE_MANAGER_UNKNOWN;
    }
}

static uint8_t host_fault_active(void)
{
    return (HAL_GPIO_ReadPin(HOST_FLAG_GPIO_Port, HOST_FLAG_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
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

static uint8_t stop_active_role(void)
{
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    if (g_usb_role.active == USB_ROLE_MANAGER_HOST) {
        if (usb_host_stop() == 0U) {
            return 0U;
        }
        usb_host_power_off();
    } else if (g_usb_role.active == USB_ROLE_MANAGER_DEVICE) {
        if (usb_device_stop() == 0U) {
            return 0U;
        }
    }
    if (g_usb_role.host_power_waiting != 0U) {
        usb_host_power_off();
        g_usb_role.host_power_waiting = 0U;
    }
    g_usb_role.active = USB_ROLE_MANAGER_NONE;
    return 1U;
}

static void apply_role(usb_role_manager_role_t requested)
{
    if ((requested == USB_ROLE_MANAGER_HOST) && (host_fault_active() != 0U)) {
        if (stop_active_role() == 0U) {
            return;
        }
        g_usb_role.host_fault = 1U;
        g_usb_role.requested = requested;
        return;
    }

    g_usb_role.host_fault = 0U;
    if ((requested == g_usb_role.active) && (g_usb_role.host_power_waiting == 0U)) {
        return;
    }

    if ((requested == USB_ROLE_MANAGER_HOST)
        && (g_usb_role.host_power_waiting != 0U)) {
        g_usb_role.requested = requested;
        return;
    }

    if (stop_active_role() == 0U) {
        return;
    }
    g_usb_role.requested = requested;

    if (requested == USB_ROLE_MANAGER_DEVICE) {
        if (usb_device_start() != 0U) {
            g_usb_role.active = USB_ROLE_MANAGER_DEVICE;
        }
    } else if (requested == USB_ROLE_MANAGER_HOST) {
        if (usb_host_prepare() != 0U) {
            g_usb_role.host_power_waiting = 1U;
            g_usb_role.host_power_deadline = HAL_GetTick() + USB_HOST_POWER_SETTLE_MS;
        } else {
            usb_host_power_off();
        }
    }
}

void usb_role_manager_init(void)
{
    g_usb_role = (usb_role_manager_ctx_t){0};
    usb_host_power_off();

    if (fusb302_init(&hi2c1) == FUSB302_STATUS_OK) {
        fusb302_role_t role = FUSB302_ROLE_NONE;
        g_usb_role.initialized = 1U;
        if (fusb302_read_role(&role) == FUSB302_STATUS_OK) {
            g_usb_role.requested = role_from_fusb302(role);
        }
    }
}

void usb_role_manager_process(void)
{
    const uint8_t host_flag_event = take_host_flag_event();

    if (g_usb_role.shutdown_requested != 0U) {
        if (stop_active_role() != 0U) {
            usb_host_power_off();
            g_usb_role.active = USB_ROLE_MANAGER_NONE;
            g_usb_role.requested = USB_ROLE_MANAGER_NONE;
            g_usb_role.initialized = 0U;
        }
        return;
    }

    if (g_usb_role.initialized == 0U) {
        return;
    }

    if ((host_flag_event != 0U)
        && ((g_usb_role.active == USB_ROLE_MANAGER_HOST)
            || (g_usb_role.host_power_waiting != 0U)
            || (g_usb_role.requested == USB_ROLE_MANAGER_HOST))) {
        if (host_fault_active() != 0U) {
            g_usb_role.host_fault = 1U;
            (void)stop_active_role();
            return;
        }
        g_usb_role.host_fault = 0U;
    }

    if (fusb302_irq_pending()) {
        if (fusb302_handle_interrupt() != FUSB302_STATUS_OK) {
            if (stop_active_role() == 0U) {
                return;
            }
            usb_host_power_off();
            return;
        }
    }

    usb_role_manager_role_t requested =
        role_from_fusb302(fusb302_cached_role());
    if (g_usb_role.host_power_waiting != 0U) {
        if (requested != USB_ROLE_MANAGER_HOST) {
            apply_role(requested);
            return;
        }
        if (deadline_reached(g_usb_role.host_power_deadline) == 0U) {
            return;
        }
        if (usb_host_start() != 0U) {
            g_usb_role.host_power_waiting = 0U;
            g_usb_role.active = USB_ROLE_MANAGER_HOST;
        } else {
            g_usb_role.host_power_waiting = 0U;
            usb_host_power_off();
        }
        return;
    }

    if ((requested != g_usb_role.requested) || (requested != g_usb_role.active)) {
        apply_role(requested);
    }
}

void usb_role_manager_shutdown(void)
{
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    usb_host_power_off();
    g_usb_role.shutdown_requested = 1U;
    midi_usb_transport_quiesce_begin();
    midi_usb_transport_quiesce_control_ack();
}

uint8_t usb_role_manager_shutdown_complete(void)
{
    /* HOST_EN is already OFF synchronously; USB teardown must not gate
     * POWER_HOLD release. */
    return (g_usb_role.shutdown_requested != 0U) ? 1U : 0U;
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

uint8_t usb_role_manager_work_pending(void)
{
    return (g_usb_role.host_power_waiting != 0U)
        && (deadline_reached(g_usb_role.host_power_deadline) != 0U);
}

uint8_t usb_role_manager_next_deadline_ms(uint32_t now_ms,
                                          uint32_t *deadline_ms)
{
    (void)now_ms;
    if ((deadline_ms == NULL) || (g_usb_role.host_power_waiting == 0U)) {
        return 0U;
    }

    *deadline_ms = g_usb_role.host_power_deadline;
    return 1U;
}

void usb_role_manager_host_flag_irq(void)
{
    g_usb_role.host_flag_event_pending = 1U;
    __DMB();
}

void usb_role_irq_dispatch(void)
{
    if (usb_role_manager_is_host_active() != 0U) {
        usb_host_irq();
    } else if (usb_role_manager_is_device_active() != 0U) {
        usb_device_irq();
    }
}
