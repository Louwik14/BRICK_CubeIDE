#include "Board/board_usb.h"

#include "usb_device.h"
#include "usb_host.h"

void board_usb_device_init(void)
{
    MX_USB_DEVICE_Init();
}

void board_usb_host_init(void)
{
    MX_USB_HOST_Init();
}

void board_usb_host_process(void)
{
    MX_USB_HOST_Process();
}

