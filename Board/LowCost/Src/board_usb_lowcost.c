#include "Board/board_usb.h"

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
}
