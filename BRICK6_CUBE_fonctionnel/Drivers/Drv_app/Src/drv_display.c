/**
 * @file drv_display.c
 */

#include "drv_display.h"

#include "spi.h"
#include "gpio.h"
#include "sdram.h"

#include <string.h>
#include <stdio.h>

/* SPI5 handle CubeMX */
extern SPI_HandleTypeDef hspi5;

/* ====================================================================== */
/* VARIABLES INTERNES                                                     */
/* ====================================================================== */

static uint8_t buffer[OLED_WIDTH * OLED_HEIGHT / 8] SDRAM_BSS;
static const font_t *current_font = NULL;

/* dirty pages tracking */
static uint8_t dirty_pages = 0;

/* ====================================================================== */
/* GPIO UTILITIES                                                         */
/* ====================================================================== */

static inline void cs_low(void)
{
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_RESET);
}

static inline void cs_high(void)
{
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
}

static inline void dc_cmd(void)
{
    HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_RESET);
}

static inline void dc_data(void)
{
    HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_SET);
}

/* ====================================================================== */
/* SPI UTILITIES                                                          */
/* ====================================================================== */

static void send_cmd(uint8_t cmd)
{
    dc_cmd();
    cs_low();
    HAL_SPI_Transmit(&hspi5, &cmd, 1, 10);
    cs_high();
}

static void send_data(const uint8_t *data, size_t len)
{
    dc_data();
    cs_low();
    HAL_SPI_Transmit(&hspi5, (uint8_t*)data, len, 100);
    cs_high();
}

/* ====================================================================== */
/* FRAMEBUFFER                                                            */
/* ====================================================================== */

uint8_t* drv_display_get_buffer(void)
{
    return buffer;
}

/* ====================================================================== */
/* PIXELS                                                                 */
/* ====================================================================== */

static inline void set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH ||
        y < 0 || y >= OLED_HEIGHT)
        return;

    const int index = x + (y >> 3) * OLED_WIDTH;
    const uint8_t mask = (uint8_t)(1U << (y & 7));

    uint8_t old = buffer[index];

    if (on)
        buffer[index] |= mask;
    else
        buffer[index] &= (uint8_t)~mask;

    if (buffer[index] != old)
    {
        dirty_pages |= (1U << (y >> 3));
    }
}

void drv_display_draw_pixel(int x, int y, bool on)
{
    set_pixel(x, y, on);
}

/* ====================================================================== */
/* PRIMITIVES                                                             */
/* ====================================================================== */

void drv_display_draw_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    for (int ix = 0; ix < w; ix++)
    {
        set_pixel(x + ix, y, true);
        set_pixel(x + ix, y + h - 1, true);
    }

    for (int iy = 0; iy < h; iy++)
    {
        set_pixel(x, y + iy, true);
        set_pixel(x + w - 1, y + iy, true);
    }
}

void drv_display_fill_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    for (int iy = 0; iy < h; iy++)
    {
        for (int ix = 0; ix < w; ix++)
        {
            set_pixel(x + ix, y + iy, true);
        }
    }
}

void drv_display_clear_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    for (int iy = 0; iy < h; iy++)
    {
        for (int ix = 0; ix < w; ix++)
        {
            set_pixel(x + ix, y + iy, false);
        }
    }
}

/* ====================================================================== */
/* TEXTE                                                                  */
/* ====================================================================== */

void drv_display_set_font(const font_t *font)
{
    current_font = font;
}

static inline uint8_t font_advance(const font_t *f)
{
    return (uint8_t)(f->width + f->spacing);
}

void drv_display_draw_char(uint8_t x, uint8_t y, char c)
{
    if (!current_font)
        return;

    if ((uint8_t)c < current_font->first ||
        (uint8_t)c > current_font->last)
        c = '?';

    for (uint8_t col = 0; col < current_font->width; col++)
    {
        uint8_t bits = current_font->get_col(c, col);

        for (uint8_t row = 0; row < current_font->height; row++)
        {
            if (bits & (1U << row))
                set_pixel(x + col, y + row, true);
        }
    }
}

void drv_display_draw_text(uint8_t x, uint8_t y, const char *txt)
{
    if (!current_font || !txt)
        return;

    const uint8_t adv = font_advance(current_font);

    while (*txt && x < OLED_WIDTH)
    {
        drv_display_draw_char(x, y, *txt++);
        x = (uint8_t)(x + adv);
    }
}

void drv_display_draw_number(uint8_t x, uint8_t y, int num)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", num);
    drv_display_draw_text(x, y, buf);
}

/* ====================================================================== */
/* CLEAR / UPDATE                                                         */
/* ====================================================================== */

void drv_display_clear(void)
{
    memset(buffer, 0x00, sizeof(buffer));
    dirty_pages = 0xFF;
}

void drv_display_update(void)
{
    if (dirty_pages == 0)
        return;

    for (uint8_t page = 0; page < 8; page++)
    {
        if (!(dirty_pages & (1U << page)))
            continue;

        send_cmd(0xB0 + page);
        send_cmd(0x00);
        send_cmd(0x10);

        send_data(&buffer[page * OLED_WIDTH], OLED_WIDTH);
    }

    dirty_pages = 0;
}

/* ====================================================================== */
/* INIT                                                                   */
/* ====================================================================== */

void drv_display_init(void)
{
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_SET);
    HAL_Delay(50);

    send_cmd(0xAE);
    send_cmd(0xD5); send_cmd(0x80);
    send_cmd(0xA8); send_cmd(0x3F);
    send_cmd(0xD3); send_cmd(0x00);
    send_cmd(0x40);

    send_cmd(0x20); send_cmd(0x02);
    send_cmd(0xA1);
    send_cmd(0xC8);
    send_cmd(0xDA); send_cmd(0x12);
    send_cmd(0xAF);

    drv_display_clear();
    drv_display_update();

    extern const font_t FONT_5X7;
    current_font = &FONT_5X7;
}
