#include "drv_display.h"

#include "gpio.h"
#include "spi.h"
#include "u8g2.h"

#include <stdio.h>

extern SPI_HandleTypeDef hspi5;

const font_t FONT_5X7 = { .id = 0U };
const font_t FONT_4X6 = { .id = 1U };

static u8g2_t g_u8g2;
static const uint8_t *g_active_font = u8g2_font_5x7_tr;

uint8_t u8x8_byte_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;

    switch (msg)
    {
    case U8X8_MSG_BYTE_SEND:
        HAL_SPI_Transmit(&hspi5, (uint8_t *)arg_ptr, arg_int, 100U);
        break;

    case U8X8_MSG_BYTE_START_TRANSFER:
        HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_RESET);
        break;

    case U8X8_MSG_BYTE_END_TRANSFER:
        HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
        break;

    case U8X8_MSG_BYTE_INIT:
    default:
        break;
    }

    return 1;
}

uint8_t u8x8_gpio_and_delay_stm32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    (void)arg_ptr;

    switch (msg)
    {
    case U8X8_MSG_GPIO_DC:
        HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, arg_int ? GPIO_PIN_SET : GPIO_PIN_RESET);
        break;

    case U8X8_MSG_GPIO_CS:
        HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, arg_int ? GPIO_PIN_SET : GPIO_PIN_RESET);
        break;

    case U8X8_MSG_GPIO_RESET:
        HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, arg_int ? GPIO_PIN_SET : GPIO_PIN_RESET);
        break;

    case U8X8_MSG_DELAY_MILLI:
        HAL_Delay(arg_int);
        break;

    case U8X8_MSG_DELAY_10MICRO:
    case U8X8_MSG_DELAY_100NANO:
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
    default:
        break;
    }

    return 1;
}

void drv_display_init(void)
{
    u8g2_Setup_ssd1309_128x64_noname0_f(&g_u8g2, U8G2_R0, u8x8_byte_hw_spi, u8x8_gpio_and_delay_stm32);
    u8g2_InitDisplay(&g_u8g2);
    u8g2_SetPowerSave(&g_u8g2, 0);
    u8g2_SetFontMode(&g_u8g2, 1);
    u8g2_SetDrawColor(&g_u8g2, 1);
    u8g2_SetFont(&g_u8g2, g_active_font);
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_SendBuffer(&g_u8g2);
}

void drv_display_clear(void)
{
    u8g2_ClearBuffer(&g_u8g2);
}

void drv_display_update(void)
{
    u8g2_SendBuffer(&g_u8g2);
}

uint8_t* drv_display_get_buffer(void)
{
    return u8g2_GetBufferPtr(&g_u8g2);
}

void drv_display_set_font(const font_t *font)
{
    if (font == &FONT_4X6)
    {
        g_active_font = u8g2_font_tom_thumb_4x6_tr;
    }
    else
    {
        g_active_font = u8g2_font_5x7_tr;
    }

    u8g2_SetFont(&g_u8g2, g_active_font);
}

void drv_display_draw_pixel(int x, int y, bool on)
{
    u8g2_SetDrawColor(&g_u8g2, on ? 1U : 0U);
    u8g2_DrawPixel(&g_u8g2, x, y);
    u8g2_SetDrawColor(&g_u8g2, 1U);
}

void drv_display_draw_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }

    u8g2_DrawFrame(&g_u8g2, x, y, w, h);
}

void drv_display_fill_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }

    u8g2_DrawBox(&g_u8g2, x, y, w, h);
}

void drv_display_clear_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }

    u8g2_SetDrawColor(&g_u8g2, 0U);
    u8g2_DrawBox(&g_u8g2, x, y, w, h);
    u8g2_SetDrawColor(&g_u8g2, 1U);
}

void drv_display_draw_char(uint8_t x, uint8_t y, char c)
{
    char txt[2] = { c, '\0' };
    u8g2_DrawStr(&g_u8g2, x, y + u8g2_GetAscent(&g_u8g2), txt);
}

void drv_display_draw_text(uint8_t x, uint8_t y, const char *txt)
{
    if (txt == NULL)
    {
        return;
    }

    u8g2_DrawStr(&g_u8g2, x, y + u8g2_GetAscent(&g_u8g2), txt);
}

void drv_display_draw_number(uint8_t x, uint8_t y, int num)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", num);
    drv_display_draw_text(x, y, buf);
}
