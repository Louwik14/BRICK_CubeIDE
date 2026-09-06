#include "App/power_shutdown.h"

#include "Audio/audio.h"
#include "Board/board_power.h"
#include "usb_role_manager.h"
#include "drv_display.h"

static uint8_t g_shutdown_requested;

void power_shutdown_init(void)
{
    g_shutdown_requested = 0U;
    board_power_shutdown_init();
}

void power_shutdown_sample(uint32_t now_ms)
{
    board_power_shutdown_sample(now_ms);
}

uint8_t power_shutdown_process_deadline(uint32_t now_ms)
{
    if (g_shutdown_requested != 0U)
    {
        if (usb_role_manager_shutdown_complete() != 0U)
        {
            board_power_shutdown_cut();
        }
        return 1U;
    }
    if (board_power_shutdown_process_deadline(now_ms) == 0U)
        return 0U;

    g_shutdown_requested = 1U;
    audio_stop();
    drv_display_off();
    board_power_usb_host_off();
    if (usb_role_manager_shutdown_complete() != 0U)
    {
        board_power_shutdown_cut();
    }
    return 1U;
}

uint8_t power_shutdown_next_deadline(uint32_t now_ms,
                                     uint32_t *out_deadline_ms)
{
    if (g_shutdown_requested != 0U)
        return 0U;
    return board_power_shutdown_next_deadline(now_ms, out_deadline_ms);
}

uint8_t power_shutdown_is_active(void)
{
    return g_shutdown_requested;
}
