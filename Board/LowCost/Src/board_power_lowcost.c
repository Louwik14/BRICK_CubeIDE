#include "Board/board_power.h"

#include "main.h"
#include "stm32h7xx_hal.h"

#define POWER_LONG_PRESS_MS 3000UL
void board_power_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

void board_power_hold_enable_after_boot_press(void)
{
    uint32_t high_start_ms = 0U;
    uint8_t high_seen = 0U;

    HAL_GPIO_WritePin(POWER_HOLD_GPIO_Port, POWER_HOLD_Pin, GPIO_PIN_RESET);

    for (;;)
    {
        const uint32_t now_ms = HAL_GetTick();
        const GPIO_PinState button_state =
            HAL_GPIO_ReadPin(POWER_BUTTON_SENSE_GPIO_Port, POWER_BUTTON_SENSE_Pin);

        if (button_state != GPIO_PIN_SET)
        {
            HAL_GPIO_WritePin(POWER_HOLD_GPIO_Port, POWER_HOLD_Pin, GPIO_PIN_RESET);
            high_seen = 0U;
            high_start_ms = 0U;
            continue;
        }

        if (high_seen == 0U)
        {
            high_seen = 1U;
            high_start_ms = now_ms;
            continue;
        }

        if ((now_ms - high_start_ms) >= POWER_LONG_PRESS_MS)
        {
            HAL_GPIO_WritePin(POWER_HOLD_GPIO_Port, POWER_HOLD_Pin, GPIO_PIN_SET);
            return;
        }
    }
}
