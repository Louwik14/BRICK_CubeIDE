#include "u8g2_port.h"

#include "gpio.h"
#include "spi.h"

extern SPI_HandleTypeDef hspi5;

static inline void busy_wait_cycles(uint32_t cycles)
{
    while (cycles--)
    {
        __NOP();
    }
}

static inline void oled_set_cs(GPIO_PinState state)
{
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, state);
}

static inline void oled_set_dc(GPIO_PinState state)
{
    HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, state);
}

static inline void oled_set_reset(GPIO_PinState state)
{
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, state);
}

uint8_t u8x8_byte_stm32_spi_hw(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;

    switch (msg)
    {
        case U8X8_MSG_BYTE_INIT:
            break;

        case U8X8_MSG_BYTE_SEND:
            (void)HAL_SPI_Transmit(&hspi5, (uint8_t *)arg_ptr, arg_int, HAL_MAX_DELAY);
            break;

        case U8X8_MSG_BYTE_SET_DC:
            oled_set_dc(arg_int ? GPIO_PIN_SET : GPIO_PIN_RESET);
            break;

        case U8X8_MSG_BYTE_START_TRANSFER:
            oled_set_cs(GPIO_PIN_RESET);
            busy_wait_cycles(32U);
            break;

        case U8X8_MSG_BYTE_END_TRANSFER:
            busy_wait_cycles(32U);
            oled_set_cs(GPIO_PIN_SET);
            break;

        default:
            return 0;
    }

    return 1;
}

uint8_t u8x8_gpio_and_delay_stm32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    (void)arg_ptr;

    switch (msg)
    {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            oled_set_cs(GPIO_PIN_SET);
            oled_set_dc(GPIO_PIN_SET);
            oled_set_reset(GPIO_PIN_SET);
            break;

        case U8X8_MSG_DELAY_MILLI:
            HAL_Delay(arg_int);
            break;

        case U8X8_MSG_DELAY_10MICRO:
            busy_wait_cycles((SystemCoreClock / 1000000U) * (uint32_t)arg_int * 10U);
            break;

        case U8X8_MSG_DELAY_100NANO:
            busy_wait_cycles(((SystemCoreClock / 10000000U) + 1U) * (uint32_t)arg_int);
            break;

        case U8X8_MSG_DELAY_NANO:
            busy_wait_cycles(((SystemCoreClock / 1000000000U) + 1U) * (uint32_t)arg_int);
            break;

        case U8X8_MSG_GPIO_CS:
            oled_set_cs(arg_int ? GPIO_PIN_SET : GPIO_PIN_RESET);
            break;

        case U8X8_MSG_GPIO_DC:
            oled_set_dc(arg_int ? GPIO_PIN_SET : GPIO_PIN_RESET);
            break;

        case U8X8_MSG_GPIO_RESET:
            oled_set_reset(arg_int ? GPIO_PIN_SET : GPIO_PIN_RESET);
            break;

        default:
            break;
    }

    return 1;
}
