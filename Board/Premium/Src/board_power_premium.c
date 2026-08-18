#include "Board/board_power.h"

#include "stm32h7xx_hal.h"

void board_power_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

void board_power_hold_enable_after_boot_press(void)
{
}

uint8_t board_power_shutdown_request_poll(uint32_t now_ms)
{
    (void)now_ms;
    return 0U;
}

void board_power_shutdown_cut(void)
{
}

void board_power_usb_host_off(void)
{
}
