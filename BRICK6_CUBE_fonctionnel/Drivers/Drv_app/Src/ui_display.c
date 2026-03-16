#include "ui_display.h"

#include "u8g2_port.h"

static u8g2_t g_u8g2;

void ui_display_init(void)
{
    u8g2_Setup_ssd1309_128x64_noname2_f(
        &g_u8g2,
        U8G2_R0,
        u8x8_byte_stm32_spi_hw,
        u8x8_gpio_and_delay_stm32
    );

    u8g2_InitDisplay(&g_u8g2);
    u8g2_SetPowerSave(&g_u8g2, 0);

    /* Hardcoded bring-up pattern: if this is visible, SPI/GPIO path is alive. */
    u8g2_ClearBuffer(&g_u8g2);
    u8g2_DrawFrame(&g_u8g2, 0U, 0U, 128U, 64U);
    u8g2_DrawBox(&g_u8g2, 2U, 2U, 16U, 16U);
    u8g2_SetFont(&g_u8g2, u8g2_font_5x8_tf);
    u8g2_DrawStr(&g_u8g2, 24U, 12U, "U8G2 OK");
    u8g2_DrawStr(&g_u8g2, 24U, 24U, "SSD1309 n2");
    u8g2_SendBuffer(&g_u8g2);
}

void ui_display_begin_frame(void)
{
    u8g2_ClearBuffer(&g_u8g2);
}

void ui_display_end_frame(void)
{
    u8g2_SendBuffer(&g_u8g2);
}

void ui_display_set_font_default(void)
{
    u8g2_SetFont(&g_u8g2, u8g2_font_5x8_tf);
}

u8g2_t *ui_display_get_u8g2(void)
{
    return &g_u8g2;
}
