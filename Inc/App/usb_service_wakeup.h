#ifndef BRICK6_USB_SERVICE_WAKEUP_H
#define BRICK6_USB_SERVICE_WAKEUP_H

#include <stdint.h>

/* USB_SERVICE doorbells are hints only; USB/TinyUSB state remains authoritative. */
#define USB_SERVICE_WAKE_WORK (1UL << 0)

void usb_service_wakeup(uint32_t flags);

#endif /* BRICK6_USB_SERVICE_WAKEUP_H */
