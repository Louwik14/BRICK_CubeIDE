#include "Board/board_power.h"

#include "stm32h7xx_hal.h"

void board_power_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

void board_power_hold_enable_after_boot_press(void)
{
}
