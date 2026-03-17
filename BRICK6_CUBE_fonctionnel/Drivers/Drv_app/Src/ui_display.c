#include "ui_display.h"

#include <string.h>

#include "font5x7.h"
#include "gpio.h"
#include "spi.h"
#include "stm32h7xx_hal.h"

extern SPI_HandleTypeDef hspi5;

static uint8_t display_buffer[DISPLAY_BUFFER_SIZE];
static uint8_t display_hw_init_pending = 0U;

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

void ui_display_init(void)
{
    oled_set_cs(GPIO_PIN_SET);
    oled_set_dc(GPIO_PIN_SET);
    oled_set_reset(GPIO_PIN_SET);

    HAL_Delay(1U);
    oled_set_reset(GPIO_PIN_RESET);
    HAL_Delay(10U);
    oled_set_reset(GPIO_PIN_SET);
    HAL_Delay(10U);

    display_clear();
    display_hw_init_pending = 1U;
    display_update();
}

void display_clear(void)
{
    (void)memset(display_buffer, 0, sizeof(display_buffer));
}

void set_pixel(uint8_t x, uint8_t y, uint8_t on)
{
    if ((x >= DISPLAY_WIDTH) || (y >= DISPLAY_HEIGHT))
    {
        return;
    }

    const uint16_t index = (uint16_t)x + ((uint16_t)(y >> 3U) * DISPLAY_WIDTH);
    const uint8_t mask = (uint8_t)(1U << (y & 0x07U));

    if (on != 0U)
    {
        display_buffer[index] |= mask;
    }
    else
    {
        display_buffer[index] &= (uint8_t)~mask;
    }
}

void draw_char(uint8_t x, uint8_t y, char c)
{
    if ((c < 32) || (c > 126))
    {
        c = '?';
    }

    const uint8_t *glyph = font5x7[(uint8_t)c - 32U];

    for (uint8_t col = 0U; col < 5U; col++)
    {
        const uint8_t bits = glyph[col];
        for (uint8_t row = 0U; row < 7U; row++)
        {
            if ((bits & (uint8_t)(1U << row)) != 0U)
            {
                set_pixel((uint8_t)(x + col), (uint8_t)(y + row), 1U);
            }
        }
    }
}

void draw_text(uint8_t x, uint8_t y, const char *text)
{
    if (text == NULL)
    {
        return;
    }

    uint8_t cursor_x = x;
    while (*text != '\0')
    {
        draw_char(cursor_x, y, *text);
        cursor_x = (uint8_t)(cursor_x + 6U);
        text++;
        if (cursor_x >= (DISPLAY_WIDTH - 5U))
        {
            break;
        }
    }
}

void draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    if ((w == 0U) || (h == 0U))
    {
        return;
    }

    for (uint8_t i = 0U; i < w; i++)
    {
        set_pixel((uint8_t)(x + i), y, 1U);
        set_pixel((uint8_t)(x + i), (uint8_t)(y + h - 1U), 1U);
    }

    for (uint8_t i = 0U; i < h; i++)
    {
        set_pixel(x, (uint8_t)(y + i), 1U);
        set_pixel((uint8_t)(x + w - 1U), (uint8_t)(y + i), 1U);
    }
}

void fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    if ((w == 0U) || (h == 0U))
    {
        return;
    }

    for (uint8_t yy = 0U; yy < h; yy++)
    {
        for (uint8_t xx = 0U; xx < w; xx++)
        {
            set_pixel((uint8_t)(x + xx), (uint8_t)(y + yy), 1U);
        }
    }
}

void display_update(void)
{
    if (display_hw_init_pending != 0U)
    {
        static const uint8_t init_cmds[] = {
            0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
            0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0x7F,
            0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0x8D, 0x14,
            0xAF
        };

        oled_set_dc(GPIO_PIN_RESET);
        oled_set_cs(GPIO_PIN_RESET);
        (void)HAL_SPI_Transmit(&hspi5, (uint8_t *)init_cmds, (uint16_t)sizeof(init_cmds), HAL_MAX_DELAY);
        oled_set_cs(GPIO_PIN_SET);

        display_hw_init_pending = 0U;
    }

    for (uint8_t page = 0U; page < DISPLAY_PAGES; page++)
    {
        uint8_t cmd;

        oled_set_dc(GPIO_PIN_RESET);
        oled_set_cs(GPIO_PIN_RESET);

        cmd = (uint8_t)(0xB0U + page);
        (void)HAL_SPI_Transmit(&hspi5, &cmd, 1U, HAL_MAX_DELAY);
        cmd = 0x00U;
        (void)HAL_SPI_Transmit(&hspi5, &cmd, 1U, HAL_MAX_DELAY);
        cmd = 0x10U;
        (void)HAL_SPI_Transmit(&hspi5, &cmd, 1U, HAL_MAX_DELAY);

        oled_set_cs(GPIO_PIN_SET);

        oled_set_dc(GPIO_PIN_SET);
        oled_set_cs(GPIO_PIN_RESET);
        (void)HAL_SPI_Transmit(&hspi5, &display_buffer[(uint16_t)page * DISPLAY_WIDTH], DISPLAY_WIDTH, HAL_MAX_DELAY);
        oled_set_cs(GPIO_PIN_SET);
    }
}

const uint8_t *display_get_buffer(void)
{
    return display_buffer;
}

void ui_display_begin_frame(void)
{
    display_clear();
}

void ui_display_end_frame(void)
{
    /* Intentionally empty: flush is scheduled from superloop. */
}

void ui_display_set_font_default(void)
{
    /* Kept for compatibility with legacy call sites. */
}
