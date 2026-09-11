#include "Board/board_power.h"

#include "main.h"
#include "stm32h7xx_hal.h"
#include "usb_role_manager.h"

#define POWER_BOOT_PRESS_MS 1000UL
#define POWER_OFF_HOLD_MS 2000UL
#define POWER_BUTTON_DEBOUNCE_MS 20UL
#define BOOTLOADER_BOOT0_CHARGE_MS 10UL
void board_power_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

uint8_t board_power_shutdown_request_poll(uint32_t now_ms)
{
    static uint8_t initialized = 0U;
    static uint8_t raw_state = 0U;
    static uint8_t stable_state = 0U;
    static uint8_t armed = 0U;
    static uint8_t hold_active = 0U;
    static uint8_t request_fired = 0U;
    static uint32_t raw_changed_ms = 0U;
    static uint32_t hold_start_ms = 0U;
    const uint8_t sampled_state =
        (HAL_GPIO_ReadPin(POWER_BUTTON_SENSE_GPIO_Port,
                          POWER_BUTTON_SENSE_Pin) == GPIO_PIN_SET) ? 1U : 0U;

    if (initialized == 0U)
    {
        initialized = 1U;
        raw_state = sampled_state;
        stable_state = sampled_state;
        raw_changed_ms = now_ms;
    }
    else if (sampled_state != raw_state)
    {
        raw_state = sampled_state;
        raw_changed_ms = now_ms;
    }

    if ((stable_state != raw_state)
        && ((uint32_t)(now_ms - raw_changed_ms) >= POWER_BUTTON_DEBOUNCE_MS))
    {
        stable_state = raw_state;
    }

    if (armed == 0U)
    {
        if (stable_state == 0U) armed = 1U;
        return 0U;
    }

    if (stable_state == 0U)
    {
        hold_active = 0U;
        request_fired = 0U;
        hold_start_ms = 0U;
        return 0U;
    }

    if (hold_active == 0U)
    {
        hold_active = 1U;
        hold_start_ms = now_ms;
        return 0U;
    }

    if ((request_fired == 0U)
        && ((uint32_t)(now_ms - hold_start_ms) >= POWER_OFF_HOLD_MS))
    {
        request_fired = 1U;
        return 1U;
    }
    return 0U;
}

void board_power_usb_host_off(void)
{
    usb_role_manager_shutdown();
}

void board_power_shutdown_cut(void)
{
    HAL_GPIO_WritePin(POWER_HOLD_GPIO_Port, POWER_HOLD_Pin, GPIO_PIN_RESET);
}

void board_power_enter_rom_dfu(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = BOOTLOADER_TRIGGER_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOOTLOADER_TRIGGER_GPIO_Port, &gpio);

    HAL_GPIO_WritePin(BOOTLOADER_TRIGGER_GPIO_Port,
                      BOOTLOADER_TRIGGER_Pin,
                      GPIO_PIN_SET);
    HAL_Delay(BOOTLOADER_BOOT0_CHARGE_MS);
    NVIC_SystemReset();

    for (;;)
    {
    }
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

        if ((now_ms - high_start_ms) >= POWER_BOOT_PRESS_MS)
        {
            HAL_GPIO_WritePin(POWER_HOLD_GPIO_Port, POWER_HOLD_Pin, GPIO_PIN_SET);
            return;
        }
    }
}
