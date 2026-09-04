#include "App/usb_service_wakeup.h"

#include "cmsis_os.h"
#include "tusb.h"

extern osThreadId_t USB_SERVICEHandle;

void usb_service_wakeup(uint32_t flags)
{
    if ((flags == 0U) || (USB_SERVICEHandle == NULL))
    {
        return;
    }

    if (osKernelGetState() != osKernelRunning)
    {
        return;
    }

    (void)osThreadFlagsSet(USB_SERVICEHandle, flags);
}

void tud_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr)
{
    (void)rhport;
    (void)eventid;
    (void)in_isr;
    usb_service_wakeup(USB_SERVICE_WAKE_WORK);
}

void tuh_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr)
{
    (void)rhport;
    (void)eventid;
    (void)in_isr;
    usb_service_wakeup(USB_SERVICE_WAKE_WORK);
}
