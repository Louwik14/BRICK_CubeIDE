#include "usb_role_manager.h"

#include "fusb302.h"
#include "i2c.h"
#include "main.h"
#include "usb_device.h"
#include "usb_host.h"

extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
extern HCD_HandleTypeDef hhcd_USB_OTG_FS;

typedef struct {
    usb_role_manager_role_t active;
    usb_role_manager_role_t requested;
    uint8_t initialized;
    uint8_t host_fault;
} usb_role_manager_ctx_t;

static usb_role_manager_ctx_t g_usb_role;

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

static void host_power_off(void)
{
    HAL_GPIO_WritePin(HOST_EN_GPIO_Port, HOST_EN_Pin, GPIO_PIN_SET);
}

static void stop_active_role(void)
{
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    if (g_usb_role.active == USB_ROLE_MANAGER_HOST) {
        (void)usb_host_stop();
        host_power_off();
    } else if (g_usb_role.active == USB_ROLE_MANAGER_DEVICE) {
        (void)usb_device_stop();
    }
    g_usb_role.active = USB_ROLE_MANAGER_NONE;
}

static void apply_role(usb_role_manager_role_t requested)
{
    if ((requested == USB_ROLE_MANAGER_HOST) && (host_fault_active() != 0U)) {
        stop_active_role();
        g_usb_role.host_fault = 1U;
        g_usb_role.requested = requested;
        return;
    }

    g_usb_role.host_fault = 0U;
    if (requested == g_usb_role.active) {
        return;
    }

    stop_active_role();
    g_usb_role.requested = requested;

    if (requested == USB_ROLE_MANAGER_DEVICE) {
        if (usb_device_start() != 0U) {
            g_usb_role.active = USB_ROLE_MANAGER_DEVICE;
        }
    } else if (requested == USB_ROLE_MANAGER_HOST) {
        if (usb_host_start() != 0U) {
            g_usb_role.active = USB_ROLE_MANAGER_HOST;
        } else {
            host_power_off();
        }
    }
}

void usb_role_manager_init(void)
{
    g_usb_role = (usb_role_manager_ctx_t){0};
    host_power_off();

    if (fusb302_init(&hi2c1) == FUSB302_STATUS_OK) {
        fusb302_role_t role = FUSB302_ROLE_NONE;
        (void)fusb302_read_role(&role);
        g_usb_role.initialized = 1U;
        apply_role(role_from_fusb302(role));
    }
}

void usb_role_manager_process(void)
{
    if (g_usb_role.initialized == 0U) {
        return;
    }

    if ((g_usb_role.active == USB_ROLE_MANAGER_HOST) && (host_fault_active() != 0U)) {
        g_usb_role.host_fault = 1U;
        stop_active_role();
        return;
    }

    if (fusb302_handle_interrupt() != FUSB302_STATUS_OK) {
        stop_active_role();
        host_power_off();
        return;
    }

    fusb302_role_t role = FUSB302_ROLE_NONE;
    if (fusb302_read_role(&role) != FUSB302_STATUS_OK) {
        stop_active_role();
        host_power_off();
        return;
    }

    usb_role_manager_role_t requested = role_from_fusb302(role);
    if ((requested != g_usb_role.requested) || (requested != g_usb_role.active)) {
        apply_role(requested);
    }
}

void usb_role_manager_shutdown(void)
{
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    host_power_off();
    if (g_usb_role.active == USB_ROLE_MANAGER_HOST) {
        (void)usb_host_stop();
    } else if (g_usb_role.active == USB_ROLE_MANAGER_DEVICE) {
        (void)usb_device_stop();
    }
    host_power_off();
    g_usb_role.active = USB_ROLE_MANAGER_NONE;
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

void usb_role_irq_dispatch(void)
{
    if (usb_role_manager_is_host_active() != 0U) {
        HAL_HCD_IRQHandler(&hhcd_USB_OTG_FS);
    } else if (usb_role_manager_is_device_active() != 0U) {
        HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
    }
}
