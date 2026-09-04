#include "Board/board_usb.h"

#include "usb_device.h"
#include "usb_host.h"
#include "usb_role_manager.h"

void board_usb_device_init(void)
{
    usb_role_manager_init();
}

void board_usb_host_init(void)
{
    usb_role_manager_init();
}

void board_usb_host_process(void)
{
    usb_role_manager_process();
    if (usb_role_manager_is_device_active() != 0U) {
        usb_device_process();
    } else if (usb_role_manager_is_host_active() != 0U) {
        usb_host_tasklet_poll_bounded(8U);
    }
}
