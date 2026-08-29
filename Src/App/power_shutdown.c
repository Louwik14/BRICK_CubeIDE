#include "App/power_shutdown.h"

#include "Audio/audio.h"
#include "Board/board_power.h"
#include "drv_display.h"

uint8_t power_shutdown_service(uint32_t now_ms)
{
    static uint8_t shutdown_requested = 0U;

    if (shutdown_requested != 0U) return 1U;
    if (board_power_shutdown_request_poll(now_ms) == 0U) return 0U;

    shutdown_requested = 1U;
    audio_stop();
    drv_display_off();
    board_power_usb_host_off();
    board_power_shutdown_cut();
    return 1U;
}
