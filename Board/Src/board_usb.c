#include "Board/board_usb.h"

#include <stddef.h>
#include <stdint.h>

#include "usb_device.h"
#include "usb_host.h"
#include "usb_role_manager.h"
#include <host/usbh.h>

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
        usb_host_process();
    }
}

uint8_t board_usb_next_deadline_ms(uint32_t now_ms,
                                   uint32_t *deadline_ms)
{
    uint32_t next_deadline;
    uint32_t remaining_ms;
    uint8_t valid = 0U;

    if (deadline_ms == NULL) {
        return 0U;
    }

    if (usb_role_manager_next_deadline_ms(now_ms, &next_deadline) != 0U) {
        valid = 1U;
    }

    if ((usb_role_manager_is_host_active() != 0U)
        && tuh_task_next_deadline_ms(&remaining_ms)) {
        const uint32_t candidate = now_ms + remaining_ms;
        if ((valid == 0U)
            || ((int32_t)(candidate - now_ms)
                < (int32_t)(next_deadline - now_ms))) {
            next_deadline = candidate;
            valid = 1U;
        }
    }

    if (valid != 0U) {
        *deadline_ms = next_deadline;
    }
    return valid;
}
