#ifndef USB_ROLE_MANAGER_H
#define USB_ROLE_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    USB_ROLE_MANAGER_NONE = 0,
    USB_ROLE_MANAGER_DEVICE,
    USB_ROLE_MANAGER_HOST,
    USB_ROLE_MANAGER_UNKNOWN
} usb_role_manager_role_t;

void usb_role_manager_init(void);
void usb_role_manager_process(void);
void usb_role_manager_shutdown(void);
usb_role_manager_role_t usb_role_manager_active_role(void);
uint8_t usb_role_manager_is_device_active(void);
uint8_t usb_role_manager_is_host_active(void);
uint8_t usb_role_manager_host_fault_active(void);
void usb_role_irq_dispatch(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_ROLE_MANAGER_H */
