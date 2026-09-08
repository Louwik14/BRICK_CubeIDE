#ifndef BRICK6_USB_HOST_H_
#define BRICK6_USB_HOST_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t usb_host_start(void);
uint8_t usb_host_prepare(void);
uint8_t usb_host_stop(void);
uint8_t usb_host_is_started(void);
uint8_t usb_host_is_ready(void);
void usb_host_process(void);
void usb_host_irq(void);
void usb_host_power_on(void);
void usb_host_power_off(void);

#ifdef __cplusplus
}
#endif

#endif /* BRICK6_USB_HOST_H_ */
