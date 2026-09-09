#include "Board/board_usb.h"

#include "main.h"
#include "usb_role_manager.h"
#include "usb_device.h"
#include "usb_host.h"
#include "midi_host.h"
#include "usb_audio.h"

#define BOARD_USB_AUDIO_SERVICE_HZ 1000U

static uint32_t g_board_usb_last_device_service_cycles;

uint32_t tusb_time_millis_api(void)
{
    return HAL_GetTick();
}

void board_usb_device_init(void)
{
    usb_role_manager_init();
}

void board_usb_host_init(void)
{
    usb_role_manager_init();
}

void board_usb_process(void)
{
    usb_role_manager_process();
    if (usb_role_manager_is_device_active() != 0U)
    {
        usb_device_process();
        g_board_usb_last_device_service_cycles = DWT->CYCCNT;
    }
    else if (usb_role_manager_is_host_active() != 0U)
    {
        usb_host_process();
        midi_host_poll_bounded(8U);
    }
}

void board_usb_service_if_due(void)
{
    uint32_t now;
    uint32_t deadline_cycles;

    if ((usb_role_manager_is_device_active() == 0U)
        || (usb_audio_audio_input_active() == 0U)) {
        return;
    }

    now = DWT->CYCCNT;
    deadline_cycles = SystemCoreClock / BOARD_USB_AUDIO_SERVICE_HZ;
    if ((g_board_usb_last_device_service_cycles == 0U)
        || ((now - g_board_usb_last_device_service_cycles)
            >= deadline_cycles)) {
        usb_device_process();
        g_board_usb_last_device_service_cycles = DWT->CYCCNT;
    }
}
